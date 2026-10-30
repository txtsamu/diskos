/* Host test for the outbound line's stall reopen (tx_open .. tx_drain_locked in ipc.c) against a fake mq_*: an ORPHANED
 * /player (the player restarted and recreated it, nothing reads the old queue) must not keep the line stuck until the 30 s
 * health tick. The whole of ipc.c is compiled into this translation unit (so the test sees its statics, and nothing is
 * sliced out or redeclared); only the mq_* calls are faked. Build: make -C tests ipc_tx_test */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <mqueue.h>

/* ---- fake queues: fd 1 = the old (orphaned) /player, fd 2.. = what mq_open("/player") hands out next ---- */
static int old_full = 1, new_full = 0, opens, closes, enoent;
static char sent_old[64][16], sent_new[64][16];
static int n_old, n_new;
mqd_t mq_open(const char *name, int oflag, ...){ (void)name; (void)oflag; opens++; return enoent ? (mqd_t)-1 : (mqd_t)2; }
int mq_close(mqd_t q){ (void)q; closes++; return 0; }
int mq_send(mqd_t q, const char *m, size_t len, unsigned prio){
    (void)prio; (void)len;
    int full = (q == 1) ? old_full : new_full;
    if(full){ errno = EAGAIN; return -1; }
    if(q == 1) snprintf(sent_old[n_old++], 16, "%.12s", m); else snprintf(sent_new[n_new++], 16, "%.12s", m);
    return 0;
}
int mq_getattr(mqd_t q, struct mq_attr *a){ (void)q; memset(a, 0, sizeof *a); return 0; }

#include "../ipc.c"

static int fails;
#define CHECK(c, msg) do{ if(!(c)){ printf("FAIL %s (line %d)\n", msg, __LINE__); fails++; } }while(0)
static void reset(void){
    old_full = 1; new_full = 0; opens = closes = n_old = n_new = enoent = 0; g_txh = g_txn = 0; g_send_err = 0;
    g_tx = (mqd_t)1;                                  /* starts on the old queue */
    g_rx_frames = 1;                                  /* the player has answered before: 6 s hold */
}
static void enq(const char *f, uint32_t age_ms){
    txe_t *e = &g_txq[(g_txh + g_txn) % TX_MAX];
    snprintf(e->f, sizeof e->f, "%s", f); e->t0 = e->s0 = tx_ms() - age_ms; e->quiet = 0; g_txn++;
}
static void drain(void){ pthread_mutex_lock(&g_txmu); tx_drain_locked(); pthread_mutex_unlock(&g_txmu); }

int main(void){
    CHECK(!tx_stall_reopen(1000, 0) && tx_stall_reopen(2000, 0), "threshold");
    CHECK(tx_stall_reopen(5, 0xFFFFF000u), "wraps");

    /* orphan: three frames stuck >2 s on the dead queue go out IN ORDER on the reopened one, none on the dead one */
    reset(); enq("0100AAAA", 2500); enq("0103BBBB", 2400); enq("0201CCCC", 2300);
    drain();
    CHECK(g_txn == 0, "line empties after reopen");
    CHECK(n_new == 3 && !strcmp(sent_new[0], "0100AAAA") && !strcmp(sent_new[1], "0103BBBB") && !strcmp(sent_new[2], "0201CCCC"), "order kept");
    CHECK(n_old == 0 && opens == 1 && closes == 1, "dead fd closed once, reopened once");
    CHECK(!g_send_err, "no toast");

    /* a short stall must not touch the fd (a busy player drains on its own) */
    reset(); enq("0100AAAA", 500); drain();
    CHECK(g_txn == 1 && opens == 0 && closes == 0, "no reopen under 2 s");

    /* busy-but-healthy: the reopened queue is full too -> frame stays held, and no reopen storm on the next passes */
    reset(); new_full = 1; enq("0100AAAA", 2500); drain(); drain(); drain();
    CHECK(g_txn == 1 && opens == 1 && closes == 1, "one reopen per stall window");
    g_txq[g_txh].s0 -= 2500; drain();                                   /* another 2 s of stall -> one more reopen */
    CHECK(g_txn == 1 && opens == 2, "reopens again after another window");
    new_full = 0; drain();
    CHECK(g_txn == 0 && n_new == 1, "delivered once it drains");

    /* still undeliverable at the hold limit: dropped and reported as before */
    reset(); new_full = 1; enq("0100AAAA", 7000); drain();
    CHECK(g_txn == 0 && g_send_err == 1, "hold expiry still fails the frame");

    /* new /player not created yet (ENOENT right after a restart): frame waits, then goes out when it appears */
    reset(); enoent = 1; enq("0100AAAA", 2500); drain();
    CHECK(g_txn == 1 && n_new == 0, "waits while /player is absent");
    enoent = 0; drain();
    CHECK(g_txn == 0 && n_new == 1, "sent once /player is back");

    printf(fails ? "FAILED\n" : "ok\n"); return fails != 0;
}
