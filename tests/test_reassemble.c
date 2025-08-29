#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/command/reassemble.h"

static int expect_order(int center,int n,const int *exp,size_t nexp){
    int *out = (int*)malloc(sizeof(int)*(n>1?n-1:0));
    size_t m = reasm_alternate_order(center,n,out);
    int ok = (m==nexp) && (memcmp(out,exp,sizeof(int)*nexp)==0);
    free(out); return ok;
}

int main(void){
    { // center 3 of 7 -> 2,4,1,5,0,6
        int exp[]={2,4,1,5,0,6};
        if(!expect_order(3,7,exp,6)){ fprintf(stderr,"reasm order 3/7 failed\n"); return 1; }
    }
    { // center 0 of 4 -> 1,2,3
        int exp[]={1,2,3};
        if(!expect_order(0,4,exp,3)){ fprintf(stderr,"reasm order 0/4 failed\n"); return 1; }
    }
    { // center 6 of 7 -> 5,4,3,2,1,0
        int exp[]={5,4,3,2,1,0};
        if(!expect_order(6,7,exp,6)){ fprintf(stderr,"reasm order 6/7 failed\n"); return 1; }
    }
    printf("test_reassemble: OK\n");
    return 0;
}
