#ifndef DW2_FUEL_H
#define DW2_FUEL_H

// Decrement fuel by 1 if moving. Returns new fuel (min 0). When fuel hits 0, caller should trigger self-destruction.
static inline int fuel_step(int current, int moving){
    if(current <= 0) return 0;
    if(moving){
        --current;
        if(current < 0) current = 0;
    }
    return current;
}

#endif // DW2_FUEL_H
