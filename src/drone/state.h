#ifndef DW2_STATE_H
#define DW2_STATE_H

typedef enum { DRN_INACTIVO=0, DRN_DESPEGADO, DRN_EN_ORBITA, DRN_EN_RUTA, DRN_FINALIZADO } DW2_State;
typedef enum {
    EVT_NINGUNO=0,
    EVT_DESPEGAR,
    EVT_LLEGUE_ENSAMBLE,
    EVT_ENJAMBRE_COMPLETO,
    EVT_LLEGADA_OBJETIVO_ATK,
    EVT_LLEGADA_OBJETIVO_CAM,
    EVT_SIN_COMBUSTIBLE,
    EVT_ABORTAR
} DW2_Event;

// Deterministic transition function used for tests/documentation
DW2_State dw2_next(DW2_State s, DW2_Event e);

#endif // DW2_STATE_H
