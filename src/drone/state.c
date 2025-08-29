#include "state.h"

DW2_State dw2_next(DW2_State s, DW2_Event e){
    switch(s){
        case DRN_INACTIVO:
            if(e==EVT_DESPEGAR) return DRN_DESPEGADO;
            break;
        case DRN_DESPEGADO:
            if(e==EVT_LLEGUE_ENSAMBLE) return DRN_EN_ORBITA;
            if(e==EVT_SIN_COMBUSTIBLE||e==EVT_ABORTAR) return DRN_FINALIZADO;
            break;
        case DRN_EN_ORBITA:
            if(e==EVT_ENJAMBRE_COMPLETO) return DRN_EN_RUTA;
            if(e==EVT_SIN_COMBUSTIBLE||e==EVT_ABORTAR) return DRN_FINALIZADO;
            break;
        case DRN_EN_RUTA:
            if(e==EVT_LLEGADA_OBJETIVO_ATK || e==EVT_LLEGADA_OBJETIVO_CAM) return DRN_FINALIZADO;
            if(e==EVT_SIN_COMBUSTIBLE||e==EVT_ABORTAR) return DRN_FINALIZADO;
            break;
        case DRN_FINALIZADO:
        default:
            return DRN_FINALIZADO;
    }
    return s;
}
