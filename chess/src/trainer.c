#include "trainer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Where a complete beginner starts: about one move in three goes wrong. */
static const double BEGINNER[3] = {0.10, 0.10, 0.15};
#define STRETCH 0.85       /* the Student slips this fraction as often as you, so it stays a step ahead */
#define MIN_WEIGHT 0.15    /* a new game's weight in the average, after the first few (about 10 games' memory) */
#define MIN_MOVES 6        /* shorter games say too little about your rates */
#define EDGE_STEP 0.25     /* per game, times how far the result was from a draw */
#define EDGE_MAX 1.5       /* the result correction scales slips by at most e^1.5 either way */
#define MAX_SLIPS 0.9      /* it always plays well sometimes */
#define LOST ANALYSIS_LOSS_MISTAKE /* expected points too low to blunder from */

void trainer_init(Trainer *t) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < 3; i++) t->rate[i] = BEGINNER[i];
}

void trainer_tally(TrainerGame *g, const MoveClass *classes, const PositionEval *evals, int ply, int color) {
    g->moves = 0;
    g->slips[0] = g->slips[1] = g->slips[2] = 0;
    for (int i = color; i < ply; i += 2) {
        MoveClass c = classes[i];
        if (c == CLASS_NONE || c == CLASS_BOOK || c == CLASS_FORCED) continue;
        if (analysis_points(&evals[i], color) < LOST) continue;
        g->moves++;
        if (c == CLASS_INACCURACY) g->slips[0]++;
        else if (c == CLASS_MISTAKE || c == CLASS_MISS) g->slips[1]++;
        else if (c == CLASS_BLUNDER) g->slips[2]++;
    }
}

bool trainer_record(Trainer *t, const TrainerGame *g) {
    if (t->games && g->seed == t->last_seed) return false;
    t->games++;
    t->last_seed = g->seed;
    if (g->score >= 1) t->wins++;
    else if (g->score <= 0) t->losses++;
    else t->draws++;
    t->edge += EDGE_STEP * 2 * (g->score - 0.5);
    t->edge = fmax(-EDGE_MAX, fmin(EDGE_MAX, t->edge));
    if (g->moves >= MIN_MOVES) {
        /* Early games move it quickly away from the beginner guess. */
        double w = fmax(1.0 / (t->graded + 2), MIN_WEIGHT);
        for (int i = 0; i < 3; i++) t->rate[i] += w * ((double)g->slips[i] / g->moves - t->rate[i]);
        t->graded++;
    }
    return true;
}

BotSlips trainer_slips(const Trainer *t) {
    double k = STRETCH * exp(-t->edge), r[3], sum = 0;
    for (int i = 0; i < 3; i++) sum += r[i] = k * t->rate[i];
    if (sum > MAX_SLIPS)
        for (int i = 0; i < 3; i++) r[i] *= MAX_SLIPS / sum;
    return (BotSlips){r[0], r[1], r[2]};
}

void trainer_format(const Trainer *t, char *out, size_t n) {
    snprintf(out, n, "trainer 1\ngames %d\ngraded %d\nrates %.5f %.5f %.5f\nedge %.5f\nrecord %d %d %d\nlast %llu\n",
             t->games, t->graded, t->rate[0], t->rate[1], t->rate[2], t->edge, t->wins, t->draws, t->losses,
             t->last_seed);
}

bool trainer_parse(Trainer *t, const char *text) {
    Trainer m;
    trainer_init(&m);
    int version = 0;
    double r[3];
    for (const char *line = text; line && *line;) {
        if (sscanf(line, "rates %lf %lf %lf", &r[0], &r[1], &r[2]) == 3)
            for (int i = 0; i < 3; i++) m.rate[i] = fmax(0, fmin(1, r[i]));
        else if (sscanf(line, "trainer %d", &version) != 1 && sscanf(line, "games %d", &m.games) != 1 &&
                 sscanf(line, "graded %d", &m.graded) != 1 && sscanf(line, "edge %lf", &m.edge) != 1 &&
                 sscanf(line, "record %d %d %d", &m.wins, &m.draws, &m.losses) != 3)
            sscanf(line, "last %llu", &m.last_seed);
        line = strchr(line, '\n');
        if (line) line++;
    }
    trainer_init(t);
    if (version != 1) return false;
    m.edge = fmax(-EDGE_MAX, fmin(EDGE_MAX, m.edge));
    *t = m;
    return true;
}
