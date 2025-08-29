#ifndef DW2_REASSEMBLE_H
#define DW2_REASSEMBLE_H

#include <stddef.h>

// Generate the alternating search order around index `center` within [0, n-1].
// Writes up to n-1 indices into out[]. Returns the count written.
size_t reasm_alternate_order(int center, int n, int *out);

#endif // DW2_REASSEMBLE_H
