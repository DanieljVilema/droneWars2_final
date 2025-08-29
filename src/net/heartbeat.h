#ifndef DW2_HEARTBEAT_H
#define DW2_HEARTBEAT_H

#include <stdint.h>
#include <stddef.h>

// Milliseconds since epoch (monotonic best-effort)
uint64_t hb_now_ms(void);

// Returns 1 if now_ms - last_ms >= z_seconds*1000
int hb_should_timeout(uint64_t last_ms, uint64_t now_ms, unsigned z_seconds);

// Deterministic 50% reconnect helper using a simple LCG RNG state
// Returns 1 on success, 0 on failure. Update rng_state on each call.
int hb_reconnect50(unsigned *rng_state);

// Formats a compact heartbeat message: "HB:<id>:<ms>"
// Returns number of bytes written (excluding NUL) or negative on error.
int hb_format(char *buf, size_t n, int drone_id, uint64_t ms);

#endif // DW2_HEARTBEAT_H
