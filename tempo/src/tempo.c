#include "tempo.h"

void tempo_reset(Tempo *tp) { tp->count = 0; }

uint32_t tempo_last(const Tempo *tp) {
    return tp->count ? tp->t[(tp->count - 1) % TEMPO_WINDOW] : 0;
}

bool tempo_expired(const Tempo *tp, uint32_t now) {
    return tp->count > 0 && (uint32_t)(now - tempo_last(tp)) > TEMPO_RESET_MS;
}

void tempo_tap(Tempo *tp, uint32_t now) {
    if (tempo_expired(tp, now)) tempo_reset(tp);
    tp->t[tp->count % TEMPO_WINDOW] = now;
    tp->count++;
}

double tempo_bpm(const Tempo *tp) {
    if (tp->count < 2) return 0;
    int n = tp->count < TEMPO_WINDOW ? tp->count : TEMPO_WINDOW;
    uint32_t first = tp->t[(tp->count - n) % TEMPO_WINDOW];
    uint32_t span = tempo_last(tp) - first;
    if (span == 0) return 0;
    return 60000.0 * (n - 1) / span;
}
