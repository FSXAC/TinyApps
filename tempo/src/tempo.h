/*
 * Tap-tempo logic, SDL-free so it can be unit tested.
 * Times are nanoseconds from a monotonic clock (SDL3 event timestamps).
 */
#ifndef TEMPO_H
#define TEMPO_H

#include <stdbool.h>
#include <stdint.h>

#define TEMPO_WINDOW 16                   /* BPM is averaged over the last N taps (tracks drift) */
#define TEMPO_RESET_NS 2000000000ull      /* a gap longer than this starts a new count (min ~30 BPM) */

typedef struct {
    uint64_t t[TEMPO_WINDOW]; /* ring buffer of tap times */
    int count;                /* taps since the last reset */
} Tempo;

void tempo_reset(Tempo *tp);
/* True once the gap since the last tap exceeds TEMPO_RESET_NS (the next tap restarts). */
bool tempo_expired(const Tempo *tp, uint64_t now);
/* Records a tap, first resetting if the previous run expired. */
void tempo_tap(Tempo *tp, uint64_t now);
uint64_t tempo_last(const Tempo *tp);
/* Average BPM over the window; 0 with fewer than 2 taps. */
double tempo_bpm(const Tempo *tp);

#endif
