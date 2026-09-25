#include "tempo.h"

void tempo_reset(Tempo *tp) { tp->count = 0; }

uint64_t tempo_last(const Tempo *tp) {
    return tp->count ? tp->t[(tp->count - 1) % TEMPO_WINDOW] : 0;
}

bool tempo_expired(const Tempo *tp, uint64_t now) {
    return tp->count > 0 && now > tempo_last(tp) && now - tempo_last(tp) > TEMPO_RESET_NS;
}

void tempo_tap(Tempo *tp, uint64_t now) {
    if (tempo_expired(tp, now)) tempo_reset(tp);
    tp->t[tp->count % TEMPO_WINDOW] = now;
    tp->count++;
}

double tempo_bpm(const Tempo *tp) {
    if (tp->count < 2) return 0;
    int n = tp->count < TEMPO_WINDOW ? tp->count : TEMPO_WINDOW;
    uint64_t first = tp->t[(tp->count - n) % TEMPO_WINDOW];
    uint64_t last = tempo_last(tp);
    if (last <= first) return 0;
    return 60e9 * (n - 1) / (double)(last - first);
}
