#include "check.h"
#include "tempo.h"

#define MS 1000000ull /* nanoseconds */

static void test_steady(void) {
    Tempo tp;
    tempo_reset(&tp);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 1000 * MS);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 1500 * MS);
    CHECK_NEAR(tempo_bpm(&tp), 120, 0.01);
    for (int i = 2; i < 40; i++) tempo_tap(&tp, (1000 + 500 * (uint64_t)i) * MS);
    CHECK_NEAR(tempo_bpm(&tp), 120, 0.01);
    CHECK(tp.count == 40);
}

static void test_window_tracks_change(void) {
    Tempo tp;
    tempo_reset(&tp);
    uint64_t t = 0;
    for (int i = 0; i < 20; i++) tempo_tap(&tp, t += 500 * MS);           /* 120 */
    for (int i = 0; i < TEMPO_WINDOW; i++) tempo_tap(&tp, t += 600 * MS); /* 100 */
    CHECK_NEAR(tempo_bpm(&tp), 100, 0.01);
}

static void test_auto_reset(void) {
    Tempo tp;
    tempo_reset(&tp);
    tempo_tap(&tp, 0);
    tempo_tap(&tp, 500 * MS);
    CHECK(!tempo_expired(&tp, 500 * MS + TEMPO_RESET_NS));
    CHECK(tempo_expired(&tp, 500 * MS + TEMPO_RESET_NS + 1));
    tempo_tap(&tp, 5000 * MS);
    CHECK(tp.count == 1);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 5750 * MS);
    CHECK_NEAR(tempo_bpm(&tp), 80, 0.01);
}

static void test_sub_ms_precision(void) {
    Tempo tp;
    tempo_reset(&tp);
    tempo_tap(&tp, 0);
    tempo_tap(&tp, 499500000ull); /* 499.5 ms */
    CHECK_NEAR(tempo_bpm(&tp), 60000.0 / 499.5, 0.001);
}

int main(void) {
    test_steady();
    test_window_tracks_change();
    test_auto_reset();
    test_sub_ms_precision();
    return check_report();
}
