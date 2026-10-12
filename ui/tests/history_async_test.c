/* Real SQLite contention: the production enqueue must not inherit DB waits. */
#include <assert.h>
static char test_db_path[1024];
#define DB_PATH test_db_path
#include "../musicdb.c"

static long long millis(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int main(int argc, char **argv){
    assert(argc == 2); snprintf(test_db_path, sizeof test_db_path, "%s", argv[1]);
    sqlite3 *d; assert(sqlite3_open(test_db_path, &d) == SQLITE_OK);
    assert(sqlite3_exec(d, "CREATE TABLE PLAY_STATS(PATH TEXT PRIMARY KEY,PLAYS INTEGER,LAST_PLAYED INTEGER);"
                       "BEGIN EXCLUSIVE;", NULL, NULL, NULL) == SQLITE_OK);
    long long before = millis();
    mdb_record_play("/music/Artist's album/one.flac");
    mdb_record_play("/music/Artist's album/one.flac");
    mdb_record_play("/music/two.flac");
    assert(millis() - before < 50); /* held exclusive lock cannot stall UI */
    usleep(120000);
    assert(sqlite3_exec(d, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK);
    int plays = 0, rows = 0;
    for(int i = 0; i < 300; i++){
        sqlite3_stmt *st;
        assert(sqlite3_prepare_v2(d, "SELECT COUNT(*),SUM(PLAYS) FROM PLAY_STATS;", -1, &st, NULL) == SQLITE_OK);
        if(sqlite3_step(st) == SQLITE_ROW){ rows = sqlite3_column_int(st, 0); plays = sqlite3_column_int(st, 1); }
        sqlite3_finalize(st); if(plays == 3) break; usleep(10000);
    }
    assert(plays == 3 && rows == 2);
    pthread_mutex_lock(&history_mu); assert(history_count == 0); pthread_mutex_unlock(&history_mu);
    struct timespec a, b;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a); usleep(200000); clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &b);
    long long cpu = (b.tv_sec - a.tv_sec) * 1000000000LL + b.tv_nsec - a.tv_nsec;
    assert(cpu < 20000000); /* dormant worker, no polling loop */
    sqlite3_close(d);
    puts("PASS: nonblocking history under real SQLite contention; FIFO counts and idle sleep");
    return 0;
}
