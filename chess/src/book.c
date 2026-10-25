#include "book.h"

#include <string.h>

/* Each name applies to the position at the end of its line; positions in
   between are still book, and the review carries the last name forward.
   Add lines freely: tests check that every one is legal. ASCII only (the
   bitmap font has no accents). */
const BookLine book_lines[] = {
    /* 1.e4 e5 */
    {"King's Pawn Opening", "e2e4"},
    {"King's Pawn Game", "e2e4 e7e5"},
    {"King's Knight Opening", "e2e4 e7e5 g1f3"},
    {"King's Knight Opening: Normal", "e2e4 e7e5 g1f3 b8c6"},
    {"Ruy Lopez", "e2e4 e7e5 g1f3 b8c6 f1b5"},
    {"Ruy Lopez: Morphy Defense", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6"},
    {"Ruy Lopez: Closed", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3"},
    {"Ruy Lopez: Exchange Variation", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6"},
    {"Ruy Lopez: Berlin Defense", "e2e4 e7e5 g1f3 b8c6 f1b5 g8f6"},
    {"Ruy Lopez: Berlin Endgame", "e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 e4d6 b5c6 d7c6 d4e5 d6f5 d1d8 e8d8"},
    {"Italian Game", "e2e4 e7e5 g1f3 b8c6 f1c4"},
    {"Italian Game: Giuoco Piano", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5"},
    {"Italian Game: Giuoco Pianissimo", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6 e1g1 e8g8"},
    {"Italian Game: Classical Variation", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3"},
    {"Italian Game: Evans Gambit", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4"},
    {"Italian Game: Two Knights Defense", "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6"},
    {"Two Knights Defense: Knight Attack",
     "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 c6a5 c4b5 c7c6 d5c6 b7c6 b5e2 h7h6 g5f3 e5e4 f3e5"},
    {"Scotch Game", "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4"},
    {"Four Knights Game", "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6"},
    {"Ponziani Opening", "e2e4 e7e5 g1f3 b8c6 c2c3"},
    {"Petrov's Defense", "e2e4 e7e5 g1f3 g8f6"},
    {"Petrov's Defense: Classical", "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3"},
    {"Philidor Defense", "e2e4 e7e5 g1f3 d7d6"},
    {"Vienna Game", "e2e4 e7e5 b1c3"},
    {"King's Gambit", "e2e4 e7e5 f2f4"},
    {"King's Gambit Accepted", "e2e4 e7e5 f2f4 e5f4"},
    {"Bishop's Opening", "e2e4 e7e5 f1c4"},
    {"Center Game", "e2e4 e7e5 d2d4 e5d4 d1d4"},

    /* 1.e4, other replies */
    {"Sicilian Defense", "e2e4 c7c5"},
    {"Sicilian Defense: Open", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4"},
    {"Sicilian Defense: Najdorf Variation", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6"},
    {"Sicilian Defense: Najdorf, English Attack", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5 d4b3 c8e6 f2f3"},
    {"Sicilian Defense: Dragon Variation", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6"},
    {"Sicilian Defense: Classical Variation", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 b8c6"},
    {"Sicilian Defense: Scheveningen", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e6"},
    {"Sicilian Defense: Old Sicilian", "e2e4 c7c5 g1f3 b8c6"},
    {"Sicilian Defense: Sveshnikov Variation", "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6"},
    {"Sicilian Defense: Accelerated Dragon", "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6"},
    {"Sicilian Defense: Rossolimo Variation", "e2e4 c7c5 g1f3 b8c6 f1b5"},
    {"Sicilian Defense: Moscow Variation", "e2e4 c7c5 g1f3 d7d6 f1b5"},
    {"Sicilian Defense: Kan Variation", "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6"},
    {"Sicilian Defense: Taimanov Variation", "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6"},
    {"Sicilian Defense: Alapin Variation", "e2e4 c7c5 c2c3"},
    {"Sicilian Defense: Closed", "e2e4 c7c5 b1c3"},
    {"Sicilian Defense: Smith-Morra Gambit", "e2e4 c7c5 d2d4 c5d4 c2c3"},
    {"French Defense", "e2e4 e7e6"},
    {"French Defense: Normal", "e2e4 e7e6 d2d4 d7d5"},
    {"French Defense: Advance Variation", "e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6"},
    {"French Defense: Exchange Variation", "e2e4 e7e6 d2d4 d7d5 e4d5 e6d5"},
    {"French Defense: Winawer Variation", "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4"},
    {"French Defense: Classical Variation", "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6"},
    {"French Defense: Rubinstein Variation", "e2e4 e7e6 d2d4 d7d5 b1c3 d5e4 c3e4"},
    {"French Defense: Tarrasch Variation", "e2e4 e7e6 d2d4 d7d5 b1d2"},
    {"Caro-Kann Defense", "e2e4 c7c6"},
    {"Caro-Kann Defense: Classical", "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6"},
    {"Caro-Kann Defense: Advance Variation", "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5"},
    {"Caro-Kann Defense: Exchange Variation", "e2e4 c7c6 d2d4 d7d5 e4d5 c6d5"},
    {"Scandinavian Defense", "e2e4 d7d5"},
    {"Scandinavian Defense: Main Line", "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5"},
    {"Scandinavian Defense: Modern Variation", "e2e4 d7d5 e4d5 g8f6"},
    {"Pirc Defense", "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6"},
    {"Modern Defense", "e2e4 g7g6"},
    {"Alekhine's Defense", "e2e4 g8f6"},
    {"Alekhine's Defense: Modern Variation", "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3"},

    /* 1.d4 d5 */
    {"Queen's Pawn Opening", "d2d4"},
    {"Queen's Pawn Game", "d2d4 d7d5"},
    {"Queen's Pawn Game: Zukertort", "d2d4 d7d5 g1f3"},
    {"Queen's Gambit", "d2d4 d7d5 c2c4"},
    {"Queen's Gambit Declined", "d2d4 d7d5 c2c4 e7e6"},
    {"Queen's Gambit Declined: Orthodox Defense", "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3"},
    {"Queen's Gambit Declined: Exchange Variation", "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c4d5 e6d5 c1g5"},
    {"Queen's Gambit Accepted", "d2d4 d7d5 c2c4 d5c4"},
    {"Queen's Gambit Accepted: Classical", "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1 a7a6"},
    {"Slav Defense", "d2d4 d7d5 c2c4 c7c6"},
    {"Slav Defense: Main Line", "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5"},
    {"Semi-Slav Defense", "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 e7e6"},
    {"London System", "d2d4 d7d5 c1f4"},
    {"London System", "d2d4 d7d5 g1f3 g8f6 c1f4"},
    {"London System: Main Line", "d2d4 d7d5 c1f4 g8f6 e2e3 e7e6 g1f3 c7c5 c2c3 b8c6 b1d2 f8d6 f4g3 e8g8 f1d3"},

    /* 1.d4 Nf6 and others */
    {"Indian Defense", "d2d4 g8f6"},
    {"London System", "d2d4 g8f6 c1f4"},
    {"Trompowsky Attack", "d2d4 g8f6 c1g5"},
    {"King's Indian Defense", "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6"},
    {"King's Indian Defense: Classical", "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6 d4d5 c6e7"},
    {"Grunfeld Defense", "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5"},
    {"Grunfeld Defense: Exchange Variation", "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7"},
    {"Nimzo-Indian Defense", "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4"},
    {"Queen's Indian Defense", "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6"},
    {"Bogo-Indian Defense", "d2d4 g8f6 c2c4 e7e6 g1f3 f8b4"},
    {"Catalan Opening", "d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2"},
    {"Benoni Defense: Modern Variation", "d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6"},
    {"Benko Gambit", "d2d4 g8f6 c2c4 c7c5 d4d5 b7b5"},
    {"Dutch Defense", "d2d4 f7f5"},
    {"Dutch Defense: Leningrad Variation", "d2d4 f7f5 g2g3 g8f6 f1g2 g7g6"},

    /* Flank openings */
    {"English Opening", "c2c4"},
    {"English Opening: Reversed Sicilian", "c2c4 e7e5"},
    {"English Opening: Symmetrical Variation", "c2c4 c7c5"},
    {"English Opening: Anglo-Indian Defense", "c2c4 g8f6"},
    {"Zukertort Opening", "g1f3"},
    {"Reti Opening", "g1f3 d7d5 c2c4"},
    {"King's Indian Attack", "g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8 d2d3"},
    {"Bird's Opening", "f2f4"},
    {"Nimzo-Larsen Attack", "b2b3"},
    {"Hungarian Opening", "g2g3"},
};
const int book_line_count = (int)(sizeof book_lines / sizeof book_lines[0]);

static int sq_at(const char *s) { return (s[0] - 'a') + (s[1] - '1') * 8; }

/* Plays one UCI move (no legality check: the lines are trusted, and tested). */
static bool play_uci(Position *p, const char *s) {
    int from = sq_at(s), to = sq_at(s + 2), promo = 0;
    if (s[4] && s[4] != ' ') promo = (int)(strchr("..nbrq", s[4]) - "..nbrq");
    Move moves[MAX_MOVES];
    int n = pos_pseudo_moves(p, moves, false);
    for (int i = 0; i < n; i++)
        if (moves[i].from == from && moves[i].to == to && moves[i].promo == promo) {
            pos_make(p, moves[i]);
            return true;
        }
    return false;
}

bool book_lookup(const Position *p, const char **name) {
    if (name) *name = NULL;
    /* Every line starts from the initial position, so only its move at this ply can match. */
    int ply = (p->fullmove - 1) * 2 + p->side;
    Position q;
    pos_start(&q);
    if (ply == 0) return q.hash == p->hash;
    bool found = false;
    for (int i = 0; i < book_line_count; i++) {
        pos_start(&q);
        const char *s = book_lines[i].moves;
        int k = 0;
        while (k < ply && *s && play_uci(&q, s)) {
            k++;
            s += 4;
            while (*s && *s != ' ') s++;
            while (*s == ' ') s++;
        }
        if (k != ply || q.hash != p->hash) continue;
        found = true;
        if (!*s) {
            if (name) *name = book_lines[i].name;
            return true;
        }
    }
    return found;
}

bool book_move(const Position *before, Move m) {
    if (!book_lookup(before, NULL)) return false;
    Position after = *before;
    pos_make(&after, m);
    return book_lookup(&after, NULL);
}
