#include <stdio.h>
#include <stdint.h>
#include "../src/net/heartbeat.h"

int main(void){
    // timeout check
    uint64_t t0=1000, t1=1000+1999; if(hb_should_timeout(t0,t1,2)) { fprintf(stderr,"premature timeout\n"); return 1; }
    t1=1000+2000; if(!hb_should_timeout(t0,t1,2)) { fprintf(stderr,"timeout not detected\n"); return 1; }

    // reconnect 50%: run many trials, expect roughly half successes
    unsigned rng=12345; int succ=0, trials=1000;
    for(int i=0;i<trials;i++){ succ += hb_reconnect50(&rng); }
    // deterministically expect around 500; allow 10% band
    if(succ<400 || succ>600){ fprintf(stderr,"reconnect50 skewed: %d\n",succ); return 1; }
    printf("test_heartbeat: OK (%d/1000)\n", succ);
    return 0;
}
