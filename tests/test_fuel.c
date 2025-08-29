#include <stdio.h>
#include "../src/drone/fuel.h"

int main(void){
    int f=3;
    f=fuel_step(f,1); if(f!=2){ fprintf(stderr,"fuel step1 failed (%d)\n",f); return 1; }
    f=fuel_step(f,0); if(f!=2){ fprintf(stderr,"fuel idle failed (%d)\n",f); return 1; }
    f=fuel_step(f,1); f=fuel_step(f,1); if(f!=0){ fprintf(stderr,"fuel to zero failed (%d)\n",f); return 1; }
    f=fuel_step(f,1); if(f!=0){ fprintf(stderr,"fuel stays zero failed (%d)\n",f); return 1; }
    printf("test_fuel: OK\n");
    return 0;
}
