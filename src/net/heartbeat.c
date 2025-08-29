#define _GNU_SOURCE
#include "heartbeat.h"
#include <time.h>
#include <stdio.h>

uint64_t hb_now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000ull + (uint64_t)ts.tv_nsec/1000000ull;
}

int hb_should_timeout(uint64_t last_ms, uint64_t now_ms, unsigned z_seconds){
    if (now_ms < last_ms) return 0; // clock skew/overflow: avoid spurious timeout
    uint64_t z = (uint64_t)z_seconds * 1000ull;
    return (now_ms - last_ms) >= z;
}

int hb_reconnect50(unsigned *rng_state){
    // xorshift32 or LCG; keep it simple and deterministic per-seed
    unsigned x = *rng_state;
    x = x * 1103515245u + 12345u; // LCG
    *rng_state = x;
    return (x & 1u) ? 1 : 0; // ~50%
}

int hb_format(char *buf, size_t n, int drone_id, uint64_t ms){
    if(!buf||n==0) return -1;
    int w = snprintf(buf, n, "HB:%d:%llu", drone_id, (unsigned long long)ms);
    if(w < 0 || (size_t)w >= n) return -1;
    return w;
}
