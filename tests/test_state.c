#include <stdio.h>
#include "../src/drone/state.h"

int main(void){
    DW2_State s=DRN_INACTIVO;
    s=dw2_next(s,EVT_DESPEGAR); if(s!=DRN_DESPEGADO){ fprintf(stderr,"to DESPEGADO fail\n"); return 1; }
    s=dw2_next(s,EVT_LLEGUE_ENSAMBLE); if(s!=DRN_EN_ORBITA){ fprintf(stderr,"to ORBITA fail\n"); return 1; }
    s=dw2_next(s,EVT_ENJAMBRE_COMPLETO); if(s!=DRN_EN_RUTA){ fprintf(stderr,"to RUTA fail\n"); return 1; }
    s=dw2_next(s,EVT_LLEGADA_OBJETIVO_ATK); if(s!=DRN_FINALIZADO){ fprintf(stderr,"to FINALIZADO fail\n"); return 1; }
    printf("test_state: OK\n");
    return 0;
}
