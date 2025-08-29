#include "reassemble.h"

size_t reasm_alternate_order(int center, int n, int *out){
    size_t w=0; if(n<=1) return 0;
    for(int r=1; r<n; ++r){
        int left  = center - r;
        int right = center + r;
        if(left >= 0)  out[w++] = left;
        if(right < n)  out[w++] = right;
    }
    return w;
}
