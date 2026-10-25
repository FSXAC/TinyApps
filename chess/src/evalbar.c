#include "evalbar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

double evalbar_white_fraction(const PositionEval *e) {
    if (!e) return 0.5;
    if (e->mate) return e->mate > 0 ? 1.0 : 0.0;
    if (e->score >= EVAL_MATE_SCORE) return 1.0;
    if (e->score <= -EVAL_MATE_SCORE) return 0.0;
    /* The win-probability curve classification uses: +1 is ~59%, +3 ~75%,
       +10 ~97%, so it saturates around ten pawns. */
    return 1.0 / (1.0 + exp(-0.00368208 * e->score));
}

void evalbar_label(const PositionEval *e, char *out, size_t n) {
    int s = abs(e->score);
    if (e->mate) snprintf(out, n, "M%d", abs(e->mate));
    else if (analysis_is_decided(e)) snprintf(out, n, "%s", e->score > 0 ? "1-0" : e->score < 0 ? "0-1" : "1/2");
    else if (s < 995) snprintf(out, n, "%d.%d", (s + 5) / 100, (s + 5) / 10 % 10);
    else snprintf(out, n, "%d", (s + 50) / 100);
}

/* Ink extent of an ASCII glyph: first column, width, first row and height, in
   font pixels. Lets the label be set tight ("1.5" is 12 px, not 17), since the
   bar is narrow and the font's fixed cells waste most of a "1" or ".". */
typedef struct {
    int col, w, row, h;
} Ink;

static Ink glyph_ink(char c) {
    Ink k = {0, 2, 0, 0};
    if (c < TINY_FONT_FIRST || c >= TINY_FONT_FIRST + TINY_FONT_ASCII) return k;
    const uint8_t *rows = tiny_font5x7[c - TINY_FONT_FIRST];
    int lo = 5, hi = -1, top = 9, bot = -1;
    for (int r = 0; r < 9; r++)
        for (int x = 0; x < 5; x++)
            if (rows[r] & (0x10 >> x)) {
                if (x < lo) lo = x;
                if (x > hi) hi = x;
                if (r < top) top = r;
                if (r > bot) bot = r;
            }
    if (hi < 0) return k;
    return (Ink){lo, hi - lo + 1, top, bot - top + 1};
}

static void draw_glyph(const TinyFont *f, char c, Ink k, int x, int y, int u) {
    char s[2] = {c, 0};
    tiny_text(f, s, x - k.col * u, y - k.row * u, u, TINY_PLAIN);
}

/* Draws `s` tightly set, one font pixel between glyphs, horizontally or stacked.
   (x, y) is the top-left of the ink box. */
static void draw_tight(const TinyFont *f, const char *s, int x, int y, int u, bool stacked, int box_w) {
    for (; *s; s++) {
        Ink k = glyph_ink(*s);
        if (stacked) {
            draw_glyph(f, *s, k, x + (box_w - k.w) / 2 * u, y, u);
            y += (k.h + 1) * u;
        } else {
            /* Glyphs sit on a common 7-row cap height. */
            draw_glyph(f, *s, k, x, y + k.row * u, u);
            x += (k.w + 1) * u;
        }
    }
}

/* Ink box of the tight label, in font pixels. */
static void tight_size(const char *s, bool stacked, int *w, int *h) {
    *w = *h = 0;
    for (const char *c = s; *c; c++) {
        Ink k = glyph_ink(*c);
        if (stacked) {
            *w = k.w > *w ? k.w : *w;
            *h += k.h + (c != s);
        } else {
            *w += k.w + (c != s);
            *h = 7;
        }
    }
}

/* Square corners: the bar is a gauge whose fill meets both ends (fully, for a
   mate), and stepped corners would notch the fill and the label at the very
   ends; DESIGN.md squares anything that shows content rather than a control. */
void evalbar_draw(SDL_Renderer *r, const TinyFont *f, int x, int y, int w, int h, int u,
                  const PositionEval *e, bool white_bottom) {
    /* Work in whole units inside the 1u frame. */
    int iw = (w / u - 2), ih = (h / u - 2);
    if (iw < 1 || ih < 2) return;
    int ix = x + u, iy = y + u;

    bool decided = e && (e->mate || abs(e->score) >= EVAL_MATE_SCORE);
    int wh = (int)lround(evalbar_white_fraction(e) * ih);
    if (!decided) wh = wh < 1 ? 1 : wh > ih - 1 ? ih - 1 : wh; /* only a mate fills the bar */
    int top_h = white_bottom ? ih - wh : wh;

    tiny_set_color(r, TINY_INK);
    tiny_fill_rect(r, x, y, iw * u + 2 * u, ih * u + 2 * u); /* frame and Black's share */
    tiny_set_color(r, TINY_PAPER);
    if (white_bottom) tiny_fill_rect(r, ix, iy + top_h * u, iw * u, wh * u);
    else tiny_fill_rect(r, ix, iy, iw * u, wh * u);

    /* Center ticks at 0.0, 2u long on both sides, gray so they read on ink and paper. */
    int mid = iy + ih / 2 * u;
    int tick = iw >= 8 ? 2 : 1;
    tiny_set_color(r, TINY_GRAY);
    tiny_fill_rect(r, ix, mid, tick * u, u);
    tiny_fill_rect(r, ix + (iw - tick) * u, mid, tick * u, u);

    if (!e) return;
    /* Label at the leading side's end (White's for 0.0), in caption size: on the
       row if it fits with a 1u margin each side, otherwise stacked. Signs are
       dropped: which end it sits at says who is ahead. */
    char label[16];
    evalbar_label(e, label, sizeof label);
    int lw, lh;
    bool stacked = false;
    tight_size(label, false, &lw, &lh);
    if (lw + 2 > iw) {
        stacked = true;
        tight_size(label, true, &lw, &lh);
        if (lw + 2 > iw) return; /* too narrow for any label */
    }
    bool white_ahead = e->mate ? e->mate > 0 : e->score >= 0;
    bool at_bottom = white_ahead == white_bottom;
    int pad = 2;
    int lx = ix + (iw - lw) / 2 * u;
    int ly = at_bottom ? iy + (ih - pad - lh) * u : iy + pad * u;
    tiny_set_color(r, white_ahead ? TINY_INK : TINY_PAPER);
    draw_tight(f, label, lx, ly, u, stacked, lw);
}
