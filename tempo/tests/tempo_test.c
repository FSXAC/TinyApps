#include "tempo.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                        \
        }                                                                      \
    } while (0)
#define CHECK_NEAR(a, b) CHECK(fabs((a) - (b)) < 0.01)

static void test_steady(void) {
    Tempo tp;
    tempo_reset(&tp);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 1000);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 1500);
    CHECK_NEAR(tempo_bpm(&tp), 120);
    for (int i = 2; i < 40; i++) tempo_tap(&tp, 1000 + 500 * i);
    CHECK_NEAR(tempo_bpm(&tp), 120);
    CHECK(tp.count == 40);
}

static void test_window_tracks_change(void) {
    Tempo tp;
    tempo_reset(&tp);
    uint32_t t = 0;
    for (int i = 0; i < 20; i++) tempo_tap(&tp, t += 500);  /* 120 */
    for (int i = 0; i < TEMPO_WINDOW; i++) tempo_tap(&tp, t += 600); /* 100 */
    CHECK_NEAR(tempo_bpm(&tp), 100);
}

static void test_auto_reset(void) {
    Tempo tp;
    tempo_reset(&tp);
    tempo_tap(&tp, 0);
    tempo_tap(&tp, 500);
    CHECK(!tempo_expired(&tp, 500 + TEMPO_RESET_MS));
    CHECK(tempo_expired(&tp, 501 + TEMPO_RESET_MS));
    tempo_tap(&tp, 5000);
    CHECK(tp.count == 1);
    CHECK(tempo_bpm(&tp) == 0);
    tempo_tap(&tp, 5750);
    CHECK_NEAR(tempo_bpm(&tp), 80);
}

static void test_wraparound(void) {
    Tempo tp;
    tempo_reset(&tp);
    tempo_tap(&tp, 0xFFFFFF00u);
    tempo_tap(&tp, 0xFFFFFF00u + 1000); /* wraps */
    CHECK(tp.count == 2);
    CHECK_NEAR(tempo_bpm(&tp), 60);
}

int main(void) {
    test_steady();
    test_window_tracks_change();
    test_auto_reset();
    test_wraparound();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
