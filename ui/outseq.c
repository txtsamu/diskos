/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "outseq.h"
#include "ipc.h"
#include <unistd.h>

/* Frames (stock V2.57, see the OUTPUT-ROUTE notes in modes.c): 0666 <n> set out device (6 local analog, 4 SPDIF_TX,
 * 3 USB_HOST), 0642 <n> gadget role (0 none, 5 USB host), 0657 8 player work mode LOCALPLAYER. A 0666 not followed by
 * 0657 8 leaves the Player in NO_WORK_MODE, so the sequence always ends there. */
#define F_ROUTE_INTERNAL "0666000C0006"
#define F_ROUTE_SPDIF    "0666000C0004"
#define F_ROUTE_USB      "0666000C0003"
#define F_GADGET_NONE    "0642000C0000"
#define F_GADGET_HOST    "0642000C0005"
#define F_GADGET_USBDAC  "0642000C0002"
#define F_GADGET_STORAGE "0642000C0001"
#define F_BT_RX_PREP     "0818000C0000"
#define F_WORKMODE_USBDAC "0657000C0001"
#define F_WORKMODE_BTRX  "0657000C0006"
#define F_WORKMODE_LOCAL "0657000C0008"
#define F_ROUTE_BT       "0666000C0002"
#define F_BT_INIT        "06c1000C0000"

/* MUST: a failure stops the sequence. SOFT: a failure is remembered (the result is not OK) and the sequence goes on.
 * IGNORE: a failure does not matter to the result. */
typedef enum { MUST, SOFT, IGNORE } policy_t;
typedef struct {
    const char *frame;
    unsigned    settle_us;   /* usleep after this frame landed */
    policy_t    policy;
    int         route_moved; /* the Player's route has changed once this frame landed */
} step_t;

#define N(a) ((int)(sizeof(a)/sizeof((a)[0])))

static const step_t FULL_INTERNAL[] = { { F_ROUTE_INTERNAL, 65000, MUST, 1 }, { F_GADGET_NONE, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t FULL_SPDIF[]    = { { F_ROUTE_SPDIF,    65000, MUST, 1 }, { F_GADGET_NONE, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t FULL_USB[]      = { { F_ROUTE_USB,          0, MUST, 1 }, { F_GADGET_HOST, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t QUICK_INTERNAL[] = { { F_ROUTE_INTERNAL, 0, MUST, 1 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t QUICK_SPDIF[]    = { { F_ROUTE_SPDIF,    0, MUST, 1 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t QUICK_USB[]      = { { F_ROUTE_USB,      0, MUST, 1 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };

/* boot: internal/SPDIF send the gadget frame before the route (the opposite order to FULL_*); USB pairs 0666 3 with 0642 5 */
static const step_t BOOT_INTERNAL[] = { { F_GADGET_NONE, 0, MUST, 0 }, { F_ROUTE_INTERNAL, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };
static const step_t BOOT_SPDIF[]    = { { F_GADGET_NONE, 0, MUST, 0 }, { F_ROUTE_SPDIF,    0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };
static const step_t BOOT_USB[]      = { { F_ROUTE_USB, 0, MUST, 0 }, { F_GADGET_HOST, 0, SOFT, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };

static const step_t PRE_INTERNAL[] = { { F_ROUTE_INTERNAL, 0, SOFT, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };
static const step_t PRE_SPDIF[]    = { { F_ROUTE_SPDIF,    0, SOFT, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };
static const step_t PRE_USB[]      = { { F_ROUTE_USB, 0, SOFT, 0 }, { F_GADGET_HOST, 0, SOFT, 0 }, { F_WORKMODE_LOCAL, 0, SOFT, 0 } };

/* The one runner. A MUST failure before or at the route-moved step is REFUSED, after it MOVED; a sequence with no
 * route-moved step only ever reports OK or REFUSED. */
static outseq_rc run(const step_t *s, int n){
    int moved = 0, soft_failed = 0;
    for(int i = 0; i < n; i++){
        if(ipc_send_cmd(s[i].frame) < 0){
            if(s[i].policy == MUST) return moved ? OUTSEQ_MOVED : OUTSEQ_REFUSED;
            if(s[i].policy == SOFT) soft_failed = 1;
            continue;
        }
        if(s[i].route_moved) moved = 1;
        if(s[i].settle_us) usleep(s[i].settle_us);
    }
    return soft_failed ? (moved ? OUTSEQ_MOVED : OUTSEQ_REFUSED) : OUTSEQ_OK;
}

/* source modes: every one starts from the internal route; a failed 0818 skips the gadget frame */
static const step_t SRC_LOCAL[]   = { { F_ROUTE_INTERNAL, 0, MUST, 1 }, { F_GADGET_NONE, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };
static const step_t SRC_USB_DAC[] = { { F_ROUTE_INTERNAL, 0, MUST, 1 }, { F_GADGET_USBDAC, 0, MUST, 0 }, { F_WORKMODE_USBDAC, 0, MUST, 0 } };   /* work mode 1, not 8: with 8 the host plays but the Player never reads it */
static const step_t SRC_BT_RX[]   = { { F_ROUTE_INTERNAL, 0, MUST, 1 }, { F_BT_RX_PREP, 0, MUST, 0 }, { F_GADGET_NONE, 0, MUST, 0 }, { F_WORKMODE_BTRX, 0, MUST, 0 } };
static const step_t SRC_STORAGE[] = { { F_ROUTE_INTERNAL, 0, MUST, 1 }, { F_GADGET_STORAGE, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };

/* leaving a Bluetooth sink: the UI does not track an output route for it, so no step is route-moved */
static const step_t ANALOG[] = { { F_ROUTE_INTERNAL, 0, MUST, 0 }, { F_WORKMODE_LOCAL, 0, MUST, 0 } };

typedef struct { const step_t *s; int n; } seq_t;
#define SEQ(a) { (a), N(a) }
static const seq_t FULL[3]  = { SEQ(FULL_INTERNAL),  SEQ(FULL_SPDIF),  SEQ(FULL_USB) };
static const seq_t QUICK[3] = { SEQ(QUICK_INTERNAL), SEQ(QUICK_SPDIF), SEQ(QUICK_USB) };
static const seq_t BOOT[3]  = { SEQ(BOOT_INTERNAL),  SEQ(BOOT_SPDIF),  SEQ(BOOT_USB) };
static const seq_t PRE[3]   = { SEQ(PRE_INTERNAL),   SEQ(PRE_SPDIF),   SEQ(PRE_USB) };

static const seq_t SRC[4] = { SEQ(SRC_LOCAL), SEQ(SRC_USB_DAC), SEQ(SRC_BT_RX), SEQ(SRC_STORAGE) };

static int valid(outseq_route_t r){ return r >= OUTSEQ_INTERNAL && r <= OUTSEQ_USB; }
static outseq_rc run_seq(const seq_t *q){ return run(q->s, q->n); }

outseq_rc outseq_route(outseq_route_t target, outseq_form_t form){
    if(!valid(target)) return OUTSEQ_REFUSED;
    return run_seq(&(form == OUTSEQ_QUICK ? QUICK : FULL)[target]);
}

outseq_rc outseq_local_boot(outseq_route_t current){
    return run_seq(&BOOT[valid(current) ? current : OUTSEQ_INTERNAL]);
}

void outseq_local_preamble(outseq_route_t current){
    (void)run_seq(&PRE[valid(current) ? current : OUTSEQ_INTERNAL]);
}

outseq_rc outseq_source(outseq_src_t mode){
    if(mode < OUTSEQ_SRC_LOCAL || mode > OUTSEQ_SRC_USB_STORAGE) return OUTSEQ_REFUSED;
    return run_seq(&SRC[mode]);
}

outseq_rc outseq_analog(void){
    return run(ANALOG, N(ANALOG));
}

/* Local playback to a Bluetooth sink: stock pre-stop, normalise gadget and work mode, start the Player's BT subsystem,
 * move to the BT sink, re-assert the work mode, then the 06b3 codec frame (the last step, supplied by the caller). */
outseq_rc outseq_bt_sink(const char *codec_frame){
    if(!codec_frame || !codec_frame[0]) return OUTSEQ_REFUSED;
    const step_t seq[] = {
        { F_ROUTE_INTERNAL, 0, MUST,   0 },
        { F_GADGET_NONE,    0, IGNORE, 0 },
        { F_WORKMODE_LOCAL, 0, IGNORE, 0 },
        { F_BT_INIT,        0, IGNORE, 0 },
        { F_ROUTE_BT,       0, MUST,   1 },
        { F_WORKMODE_LOCAL, 0, IGNORE, 0 },
        { codec_frame,      0, MUST,   0 },
    };
    return run(seq, N(seq));
}

void outseq_assert_workmode(void){
    static const step_t seq[] = { { F_WORKMODE_LOCAL, 0, IGNORE, 0 } };
    (void)run(seq, N(seq));
}
