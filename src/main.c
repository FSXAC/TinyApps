/*
 * TinyPainter - a 32x32 black & white pixel painter built on SDL2.
 */
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CANVAS 32
#define MARGIN 4                       /* context cells shown around the canvas */
#define VIEW (CANVAS + 2 * MARGIN)     /* cells visible in the canvas view */
#define UNDO_MAX 128

enum { WHITE = 0, BLACK = 1 };

typedef enum { T_PENCIL, T_LINE, T_ELLIPSE, T_FILL, T_ERASER, TOOL_COUNT } Tool;
typedef enum { A_UNDO, A_CLEAR, A_SAVE, ACTION_COUNT } Action;

typedef struct { uint8_t px[CANVAS][CANVAS]; } Image;

typedef struct { int x, y, w, h; } Rect;

/* ------------------------------------------------------------------ */
/* Tiny 5x7 bitmap font (only the glyphs used by the UI labels).        */

typedef struct { char c; uint8_t rows[7]; } Glyph;
static const Glyph FONT[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
};

static const Glyph *find_glyph(char c) {
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].c == c) return &FONT[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 16x16 toolbar icons.                                                 */

static const char *ICON_PENCIL[16] = {
    "................",
    "...........###..",
    "..........#...#.",
    ".........#.#..#.",
    "........#.#..#..",
    ".......#.#..#...",
    "......#.#..#....",
    ".....#.#..#.....",
    "....#.#..#......",
    "...#.#..#.......",
    "...##..#........",
    "..#.#.#.........",
    "..#..#..........",
    ".####...........",
    ".##.............",
    "................",
};
static const char *ICON_LINE[16] = {
    "................",
    ".............##.",
    ".............##.",
    "............#...",
    "...........#....",
    "..........#.....",
    ".........#......",
    "........#.......",
    ".......#........",
    "......#.........",
    ".....#..........",
    "....#...........",
    "...#............",
    ".##.............",
    ".##.............",
    "................",
};
static const char *ICON_ELLIPSE[16] = {
    "................",
    "................",
    "................",
    ".....######.....",
    "...##......##...",
    "..#..........#..",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    "..#..........#..",
    "...##......##...",
    ".....######.....",
    "................",
    "................",
    "................",
};
static const char *ICON_FILL[16] = {
    "................",
    ".......#........",
    ".......#........",
    "......###.......",
    "......###.......",
    ".....#####......",
    ".....#####......",
    "....#######.....",
    "...#########....",
    "...#.#######....",
    "..##.########...",
    "..##.########...",
    "..###.#######...",
    "...###.#####....",
    "....#######.....",
    "......###.......",
};
static const char *ICON_ERASER[16] = {
    "................",
    "................",
    "..........##....",
    ".........#..#...",
    "........#....#..",
    ".......#......#.",
    "......#......#..",
    ".....##.....#...",
    "....####...#....",
    "...######.#.....",
    "..#######.#.....",
    "...#####.#......",
    "....###.#.......",
    ".....###........",
    "................",
    "................",
};
static const char *ICON_UNDO[16] = {
    "................",
    "................",
    "....#...........",
    "...##...........",
    "..#######.......",
    ".########.#.....",
    "..#######..##...",
    "...##........#..",
    "....#.........#.",
    "..............#.",
    "..............#.",
    ".............#..",
    "............#...",
    "..........##....",
    "................",
    "................",
};
static const char *ICON_CLEAR[16] = {
    "................",
    "......####......",
    ".....#....#.....",
    ".##############.",
    "................",
    "..############..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..#..#..#..#.#..",
    "..############..",
    "................",
    "................",
};
static const char *ICON_SAVE[16] = {
    "................",
    ".#############..",
    ".#..#######.#.#.",
    ".#..#######.#..#",
    ".#..#######....#",
    ".#..#######....#",
    ".#.............#",
    ".#.............#",
    ".#..#########..#",
    ".#..#.......#..#",
    ".#..#.......#..#",
    ".#..#.......#..#",
    ".#..#.......#..#",
    ".###############",
    "................",
    "................",
};

typedef struct {
    const char *label;
    const char **icon;
} ButtonDef;

static const ButtonDef TOOL_BUTTONS[TOOL_COUNT] = {
    {"PENCIL", ICON_PENCIL}, {"LINE", ICON_LINE},     {"ELLIPSE", ICON_ELLIPSE},
    {"FILL", ICON_FILL},     {"ERASER", ICON_ERASER},
};
static const ButtonDef ACTION_BUTTONS[ACTION_COUNT] = {
    {"UNDO", ICON_UNDO}, {"CLEAR", ICON_CLEAR}, {"SAVE", ICON_SAVE},
};
static const char *TOOL_NAMES[TOOL_COUNT] = {"Pencil", "Line", "Ellipse", "Fill", "Eraser"};

/* ------------------------------------------------------------------ */
/* Application state.                                                   */

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;

    Image img;
    Image undo[UNDO_MAX];
    int undo_count;

    Tool tool;
    bool show_grid;

    /* Active drag. */
    bool dragging;
    int drag_button;
    int drag_color;
    int sx, sy;   /* drag start, in canvas coords (may lie in the margin) */
    int lx, ly;   /* last pointer position, canvas coords */
    bool shift;

    /* Shape preview overlay covering the whole view (canvas + margin). */
    uint8_t overlay[VIEW][VIEW];
    int overlay_color;

    /* Hover. */
    bool hover;
    int hx, hy;

    /* Layout (renderer pixels). */
    float dpi;
    int cell;
    Rect view;
    Rect tool_rects[TOOL_COUNT];
    Rect action_rects[ACTION_COUNT];

    char save_path[1024];
} App;

/* ------------------------------------------------------------------ */
/* Persistence.                                                         */

static void autosave_path(char *out, size_t n) {
    char *base = SDL_GetPrefPath("TinyPainter", "TinyPainter");
    snprintf(out, n, "%scanvas.bin", base ? base : "./");
    SDL_free(base);
}

static void autosave(App *a) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", a->save_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    bool ok = fwrite(&a->img, sizeof a->img, 1, f) == 1;
    ok = (fclose(f) == 0) && ok;
    if (ok) rename(tmp, a->save_path);
    else remove(tmp);
}

static void autoload(App *a) {
    FILE *f = fopen(a->save_path, "rb");
    if (!f) return;
    Image tmp;
    if (fread(&tmp, sizeof tmp, 1, f) == 1) {
        for (int y = 0; y < CANVAS; y++)
            for (int x = 0; x < CANVAS; x++) tmp.px[y][x] = tmp.px[y][x] ? BLACK : WHITE;
        a->img = tmp;
    }
    fclose(f);
}

static void put_le16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put_le32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF;
}

/* Writes a true 1-bit monochrome BMP. */
static bool write_bmp(const Image *img, const char *path) {
    enum { HDR = 14 + 40 + 8, ROW = 4, DATA = ROW * CANVAS };
    uint8_t buf[HDR + DATA];
    memset(buf, 0, sizeof buf);
    buf[0] = 'B'; buf[1] = 'M';
    put_le32(buf + 2, sizeof buf);
    put_le32(buf + 10, HDR);
    put_le32(buf + 14, 40);
    put_le32(buf + 18, CANVAS);
    put_le32(buf + 22, CANVAS);      /* positive height: bottom-up rows */
    put_le16(buf + 26, 1);
    put_le16(buf + 28, 1);           /* 1 bit per pixel */
    put_le32(buf + 34, DATA);
    put_le32(buf + 38, 2835);        /* 72 DPI */
    put_le32(buf + 42, 2835);
    put_le32(buf + 46, 2);
    put_le32(buf + 50, 2);
    /* Palette: index 0 = white, index 1 = black (BGRA). */
    buf[54] = buf[55] = buf[56] = 0xFF;
    uint8_t *data = buf + HDR;
    for (int y = 0; y < CANVAS; y++) {
        uint8_t *row = data + (CANVAS - 1 - y) * ROW;
        for (int x = 0; x < CANVAS; x++)
            if (img->px[y][x] == BLACK) row[x / 8] |= 0x80 >> (x % 8);
    }
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, sizeof buf, 1, f) == 1;
    return (fclose(f) == 0) && ok;
}

static bool dir_writable(const char *dir) {
    char test[1200];
    snprintf(test, sizeof test, "%s/.tinypainter_probe", dir);
    FILE *f = fopen(test, "wb");
    if (!f) return false;
    fclose(f);
    remove(test);
    return true;
}

static void save_bitmap(App *a) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/Pictures", home ? home : ".");
    if (!dir_writable(dir)) snprintf(dir, sizeof dir, "%s", home ? home : ".");

    time_t now = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    char path[1200];
    snprintf(path, sizeof path, "%s/TinyPainter-%s.bmp", dir, stamp);
    for (int i = 2; i < 100; i++) {
        FILE *e = fopen(path, "rb");
        if (!e) break;
        fclose(e);
        snprintf(path, sizeof path, "%s/TinyPainter-%s-%d.bmp", dir, stamp, i);
    }

    char msg[1400];
    if (write_bmp(&a->img, path)) {
        snprintf(msg, sizeof msg, "Saved 32x32 bitmap to:\n%s", path);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "TinyPainter", msg, a->win);
    } else {
        snprintf(msg, sizeof msg, "Could not save bitmap to:\n%s", path);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TinyPainter", msg, a->win);
    }
}

/* ------------------------------------------------------------------ */
/* Undo.                                                                */

static void push_undo(App *a) {
    if (a->undo_count == UNDO_MAX) {
        memmove(&a->undo[0], &a->undo[1], sizeof(Image) * (UNDO_MAX - 1));
        a->undo_count--;
    }
    a->undo[a->undo_count++] = a->img;
}

/* Drops the most recent undo snapshot if nothing changed since. */
static void settle_undo(App *a) {
    if (a->undo_count && memcmp(&a->undo[a->undo_count - 1], &a->img, sizeof(Image)) == 0)
        a->undo_count--;
}

static void do_undo(App *a) {
    if (!a->undo_count) return;
    a->img = a->undo[--a->undo_count];
    autosave(a);
}

static void do_clear(App *a) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Clear"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_WARNING, a->win, "TinyPainter",
        "Clear the canvas? (You can still undo.)", 2, buttons, NULL,
    };
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) != 0 || choice != 1) return;
    push_undo(a);
    memset(&a->img, WHITE, sizeof a->img);
    settle_undo(a);
    autosave(a);
}

/* ------------------------------------------------------------------ */
/* Raster algorithms.                                                   */

typedef void (*PlotFn)(App *a, int x, int y, int color);

static void plot_canvas(App *a, int x, int y, int color) {
    if (x >= 0 && y >= 0 && x < CANVAS && y < CANVAS) a->img.px[y][x] = (uint8_t)color;
}

static void plot_overlay(App *a, int x, int y, int color) {
    (void)color;
    x += MARGIN;
    y += MARGIN;
    if (x >= 0 && y >= 0 && x < VIEW && y < VIEW) a->overlay[y][x] = 1;
}

static void raster_line(App *a, int x0, int y0, int x1, int y1, int color, PlotFn plot) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(a, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Ellipse inscribed in the rectangle (x0,y0)-(x1,y1); Zingl's algorithm. */
static void raster_ellipse(App *a, int x0, int y0, int x1, int y1, int color, PlotFn plot) {
    long long ea = llabs((long long)x1 - x0), eb = llabs((long long)y1 - y0), b1 = eb & 1;
    long long dx = 4 * (1 - ea) * eb * eb, dy = 4 * (b1 + 1) * ea * ea;
    long long err = dx + dy + b1 * ea * ea, e2;
    if (x0 > x1) { x0 = x1; x1 += (int)ea; }
    if (y0 > y1) y0 = y1;
    y0 += (int)((eb + 1) / 2);
    y1 = y0 - (int)b1;
    ea = 8 * ea * ea;
    b1 = 8 * eb * eb;
    do {
        plot(a, x1, y0, color);
        plot(a, x0, y0, color);
        plot(a, x0, y1, color);
        plot(a, x1, y1, color);
        e2 = 2 * err;
        if (e2 <= dy) { y0++; y1--; err += dy += ea; }
        if (e2 >= dx || 2 * err > dy) { x0++; x1--; err += dx += b1; }
    } while (x0 <= x1);
    while (y0 - y1 <= eb) {
        plot(a, x0 - 1, y0, color);
        plot(a, x1 + 1, y0++, color);
        plot(a, x0 - 1, y1, color);
        plot(a, x1 + 1, y1--, color);
    }
}

static void flood_fill(App *a, int x, int y, int color) {
    if (x < 0 || y < 0 || x >= CANVAS || y >= CANVAS) return;
    int target = a->img.px[y][x];
    if (target == color) return;
    static int stack[CANVAS * CANVAS * 4 + 1][2];
    int sp = 0;
    stack[sp][0] = x; stack[sp][1] = y; sp++;
    while (sp) {
        sp--;
        int cx = stack[sp][0], cy = stack[sp][1];
        if (cx < 0 || cy < 0 || cx >= CANVAS || cy >= CANVAS) continue;
        if (a->img.px[cy][cx] != target) continue;
        a->img.px[cy][cx] = (uint8_t)color;
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++) {
            stack[sp][0] = cx + d[i][0];
            stack[sp][1] = cy + d[i][1];
            sp++;
        }
    }
}

/* Applies shift constraints: 0/45/90-degree lines and perfect circles. */
static void constrained_end(App *a, int *ex, int *ey) {
    if (!a->shift) return;
    int dx = *ex - a->sx, dy = *ey - a->sy;
    int adx = abs(dx), ady = abs(dy);
    if (a->tool == T_LINE) {
        if (adx > 2 * ady) *ey = a->sy;
        else if (ady > 2 * adx) *ex = a->sx;
        else {
            int m = adx > ady ? adx : ady;
            *ex = a->sx + (dx < 0 ? -m : m);
            *ey = a->sy + (dy < 0 ? -m : m);
        }
    } else if (a->tool == T_ELLIPSE) {
        int m = adx > ady ? adx : ady;
        *ex = a->sx + (dx < 0 ? -m : m);
        *ey = a->sy + (dy < 0 ? -m : m);
    }
}

static void build_shape(App *a, PlotFn plot) {
    int ex = a->lx, ey = a->ly;
    constrained_end(a, &ex, &ey);
    if (a->tool == T_LINE) raster_line(a, a->sx, a->sy, ex, ey, a->drag_color, plot);
    else raster_ellipse(a, a->sx, a->sy, ex, ey, a->drag_color, plot);
}

static bool is_shape_tool(Tool t) { return t == T_LINE || t == T_ELLIPSE; }

static void update_overlay(App *a) {
    memset(a->overlay, 0, sizeof a->overlay);
    if (a->dragging && is_shape_tool(a->tool)) {
        a->overlay_color = a->drag_color;
        build_shape(a, plot_overlay);
    }
}

/* ------------------------------------------------------------------ */
/* Layout & hit testing.                                                */

static bool in_rect(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void layout(App *a) {
    int ww, wh, rw, rh;
    SDL_GetWindowSize(a->win, &ww, &wh);
    SDL_GetRendererOutputSize(a->ren, &rw, &rh);
    a->dpi = ww > 0 ? (float)rw / (float)ww : 1.0f;

    const int nbtn = TOOL_COUNT + ACTION_COUNT;
    int pad = (int)(10 * a->dpi);
    int gap = (int)(14 * a->dpi); /* separation between tool and action groups */
    int base_btn = (int)(64 * a->dpi);

    /* Evaluate a left (vertical) and a top (horizontal) toolbar and pick the
       one giving the larger canvas. Buttons shrink if the window is small. */
    int best_cell = -1;
    for (int vertical = 1; vertical >= 0; vertical--) {
        int along = vertical ? rh : rw;
        int b = (along - 2 * pad - gap - (nbtn - 1) * (pad / 2)) / nbtn;
        if (b > base_btn) b = base_btn;
        if (b < 20) b = 20;
        int bar = b + 2 * pad;
        int aw = vertical ? rw - bar : rw;
        int ah = vertical ? rh : rh - bar;
        int avail = (aw < ah ? aw : ah) - 2 * pad;
        int cell = avail / VIEW;
        if (cell < 1) cell = 1;
        if (cell <= best_cell) continue;
        best_cell = cell;
        a->cell = cell;
        int size = cell * VIEW;
        a->view.w = a->view.h = size;
        a->view.x = (vertical ? bar : 0) + (aw - size) / 2;
        a->view.y = (vertical ? 0 : bar) + (ah - size) / 2;
        int total = nbtn * b + (nbtn - 1) * (pad / 2) + gap;
        int pos = (along - total) / 2;
        if (pos < pad) pos = pad;
        for (int i = 0; i < nbtn; i++) {
            if (i == TOOL_COUNT) pos += gap;
            Rect r = vertical ? (Rect){pad, pos, b, b} : (Rect){pos, pad, b, b};
            if (i < TOOL_COUNT) a->tool_rects[i] = r;
            else a->action_rects[i - TOOL_COUNT] = r;
            pos += b + pad / 2;
        }
    }
}

static void to_renderer(App *a, int wx, int wy, int *rx, int *ry) {
    *rx = (int)(wx * a->dpi);
    *ry = (int)(wy * a->dpi);
}

static int floor_div(int n, int d) { return n >= 0 ? n / d : -1 - (-n - 1) / d; }

/* Converts renderer coordinates to canvas cell coordinates. Returns whether
   the point lies inside the view (canvas + margin). */
static bool to_cell(App *a, int rx, int ry, int *cx, int *cy) {
    *cx = floor_div(rx - a->view.x, a->cell) - MARGIN;
    *cy = floor_div(ry - a->view.y, a->cell) - MARGIN;
    return in_rect(a->view, rx, ry);
}

/* ------------------------------------------------------------------ */
/* Drawing.                                                             */

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static void draw_icon(SDL_Renderer *r, const char **icon, int x, int y, int scale) {
    for (int iy = 0; iy < 16; iy++)
        for (int ix = 0; ix < 16; ix++)
            if (icon[iy][ix] == '#') fill_rect(r, x + ix * scale, y + iy * scale, scale, scale);
}

static void draw_text(SDL_Renderer *r, const char *s, int x, int y, int scale) {
    for (; *s; s++, x += 6 * scale) {
        const Glyph *g = find_glyph(*s);
        if (!g) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g->rows[row] & (0x10 >> col))
                    fill_rect(r, x + col * scale, y + row * scale, scale, scale);
    }
}

static void draw_button(App *a, Rect rc, const ButtonDef *def, bool active, bool hot) {
    SDL_Renderer *r = a->ren;
    if (active) SDL_SetRenderDrawColor(r, 235, 235, 235, 255);
    else if (hot) SDL_SetRenderDrawColor(r, 88, 88, 96, 255);
    else SDL_SetRenderDrawColor(r, 62, 62, 68, 255);
    fill_rect(r, rc.x, rc.y, rc.w, rc.h);

    int ls = (int)(a->dpi + 0.5f);
    if (ls < 1) ls = 1;
    int label_h = 7 * ls;
    int label_w = (int)strlen(def->label) * 6 * ls - ls;
    bool show_label = rc.h >= (int)(52 * a->dpi) && label_w <= rc.w - 2 * ls;

    int icon_space = rc.h - (show_label ? label_h + 6 * ls : 0);
    int scale = (icon_space * 3 / 4) / 16;
    if (scale < 1) scale = 1;
    int isz = 16 * scale;

    if (active) SDL_SetRenderDrawColor(r, 20, 20, 20, 255);
    else SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
    int top = rc.y + (icon_space - isz) / 2 + (show_label ? 2 * ls : 0);
    draw_icon(r, def->icon, rc.x + (rc.w - isz) / 2, top, scale);
    if (show_label)
        draw_text(r, def->label, rc.x + (rc.w - label_w) / 2, rc.y + rc.h - label_h - 4 * ls, ls);
}

static void render(App *a) {
    SDL_Renderer *r = a->ren;
    layout(a);
    SDL_SetRenderDrawColor(r, 40, 40, 44, 255);
    SDL_RenderClear(r);

    int mx, my, rx, ry;
    SDL_GetMouseState(&mx, &my);
    to_renderer(a, mx, my, &rx, &ry);

    for (int i = 0; i < TOOL_COUNT; i++)
        draw_button(a, a->tool_rects[i], &TOOL_BUTTONS[i], a->tool == (Tool)i,
                    in_rect(a->tool_rects[i], rx, ry));
    for (int i = 0; i < ACTION_COUNT; i++)
        draw_button(a, a->action_rects[i], &ACTION_BUTTONS[i], false,
                    in_rect(a->action_rects[i], rx, ry));

    int c = a->cell;
    int ox = a->view.x, oy = a->view.y;

    /* Margin (context area around the paintable canvas). */
    SDL_SetRenderDrawColor(r, 120, 120, 126, 255);
    fill_rect(r, ox, oy, a->view.w, a->view.h);

    /* Canvas pixels + shape preview overlay. */
    for (int vy = 0; vy < VIEW; vy++) {
        for (int vx = 0; vx < VIEW; vx++) {
            int x = vx - MARGIN, y = vy - MARGIN;
            bool inside = x >= 0 && y >= 0 && x < CANVAS && y < CANVAS;
            bool ov = a->overlay[vy][vx];
            if (!inside && !ov) continue;
            if (ov && !inside) {
                /* Parts of a shape falling off the canvas: tinted hint only. */
                if (a->overlay_color == BLACK) SDL_SetRenderDrawColor(r, 70, 70, 76, 255);
                else SDL_SetRenderDrawColor(r, 185, 185, 190, 255);
            } else {
                int color = ov ? a->overlay_color : a->img.px[y][x];
                if (color == BLACK) SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                else SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            }
            fill_rect(r, ox + vx * c, oy + vy * c, c, c);
        }
    }

    int cx0 = ox + MARGIN * c, cy0 = oy + MARGIN * c, csz = CANVAS * c;

    /* Grid, with slightly stronger lines every 8 pixels. */
    if (a->show_grid && c >= 6) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (int i = 1; i < CANVAS; i++) {
            bool major = i % 8 == 0;
            int lw = major && c >= 12 ? 2 : 1;
            SDL_SetRenderDrawColor(r, 128, 128, 128, major ? 140 : 70);
            fill_rect(r, cx0 + i * c - lw / 2, cy0, lw, csz);
            fill_rect(r, cx0, cy0 + i * c - lw / 2, csz, lw);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    /* Canvas border. */
    int bw = c >= 8 ? 2 : 1;
    SDL_SetRenderDrawColor(r, 30, 30, 34, 255);
    fill_rect(r, cx0 - bw, cy0 - bw, csz + 2 * bw, bw);
    fill_rect(r, cx0 - bw, cy0 + csz, csz + 2 * bw, bw);
    fill_rect(r, cx0 - bw, cy0, bw, csz);
    fill_rect(r, cx0 + csz, cy0, bw, csz);

    /* Hovered-pixel outline. */
    if (a->hover && !(a->dragging && is_shape_tool(a->tool))) {
        int hx = cx0 + a->hx * c, hy = cy0 + a->hy * c;
        int t = c >= 10 ? 2 : 1;
        SDL_SetRenderDrawColor(r, 255, 80, 60, 255);
        fill_rect(r, hx, hy, c, t);
        fill_rect(r, hx, hy + c - t, c, t);
        fill_rect(r, hx, hy, t, c);
        fill_rect(r, hx + c - t, hy, t, c);
    }

    SDL_RenderPresent(r);
}

static void update_title(App *a) {
    char title[128];
    snprintf(title, sizeof title, "TinyPainter - %s", TOOL_NAMES[a->tool]);
    SDL_SetWindowTitle(a->win, title);
}

/* ------------------------------------------------------------------ */
/* Input.                                                               */

static void begin_drag(App *a, int button, int cx, int cy, int color) {
    push_undo(a);
    a->dragging = true;
    a->drag_button = button;
    a->drag_color = color;
    a->sx = a->lx = cx;
    a->sy = a->ly = cy;
    if (a->tool == T_PENCIL || a->tool == T_ERASER) {
        plot_canvas(a, cx, cy, color);
    } else if (a->tool == T_FILL) {
        flood_fill(a, cx, cy, color);
        a->dragging = false;
        settle_undo(a);
        autosave(a);
    } else {
        update_overlay(a);
    }
    if (a->dragging) SDL_CaptureMouse(SDL_TRUE);
}

static void drag_to(App *a, int cx, int cy) {
    if (!a->dragging) return;
    if (a->tool == T_PENCIL || a->tool == T_ERASER)
        raster_line(a, a->lx, a->ly, cx, cy, a->drag_color, plot_canvas);
    a->lx = cx;
    a->ly = cy;
    update_overlay(a);
}

static void end_drag(App *a) {
    if (!a->dragging) return;
    SDL_CaptureMouse(SDL_FALSE);
    if (is_shape_tool(a->tool)) build_shape(a, plot_canvas);
    a->dragging = false;
    memset(a->overlay, 0, sizeof a->overlay);
    settle_undo(a);
    autosave(a);
}

static void cancel_drag(App *a) {
    if (!a->dragging) return;
    SDL_CaptureMouse(SDL_FALSE);
    a->dragging = false;
    memset(a->overlay, 0, sizeof a->overlay);
    do_undo(a);
}

static void set_tool(App *a, Tool t) {
    end_drag(a);
    a->tool = t;
    update_title(a);
}

static void run_action(App *a, Action act) {
    end_drag(a);
    switch (act) {
    case A_UNDO: do_undo(a); break;
    case A_CLEAR: do_clear(a); break;
    case A_SAVE: save_bitmap(a); break;
    default: break;
    }
}

static void handle_mouse_down(App *a, const SDL_MouseButtonEvent *e) {
    int rx, ry, cx, cy;
    layout(a);
    to_renderer(a, e->x, e->y, &rx, &ry);
    if (a->dragging) return;
    if (e->button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < TOOL_COUNT; i++)
            if (in_rect(a->tool_rects[i], rx, ry)) { set_tool(a, (Tool)i); return; }
        for (int i = 0; i < ACTION_COUNT; i++)
            if (in_rect(a->action_rects[i], rx, ry)) { run_action(a, (Action)i); return; }
    }
    if (e->button != SDL_BUTTON_LEFT && e->button != SDL_BUTTON_RIGHT) return;
    if (!to_cell(a, rx, ry, &cx, &cy)) return;
    /* Left paints black, right paints white; the eraser swaps them. */
    bool primary = e->button == SDL_BUTTON_LEFT;
    if (a->tool == T_ERASER) primary = !primary;
    begin_drag(a, e->button, cx, cy, primary ? BLACK : WHITE);
}

static void handle_motion(App *a, int wx, int wy) {
    int rx, ry, cx, cy;
    layout(a);
    to_renderer(a, wx, wy, &rx, &ry);
    bool inview = to_cell(a, rx, ry, &cx, &cy);
    a->hover = inview && cx >= 0 && cy >= 0 && cx < CANVAS && cy < CANVAS;
    a->hx = cx;
    a->hy = cy;
    if (a->dragging) {
        /* Keep coordinates bounded when the pointer leaves the window. */
        int lim = VIEW * 4;
        cx = cx < -lim ? -lim : cx > lim ? lim : cx;
        cy = cy < -lim ? -lim : cy > lim ? lim : cy;
        drag_to(a, cx, cy);
    }
}

static void handle_key(App *a, const SDL_KeyboardEvent *e) {
    SDL_Keymod mod = SDL_GetModState();
    bool cmd = (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
    switch (e->keysym.sym) {
    case SDLK_p: case SDLK_b: if (!cmd) set_tool(a, T_PENCIL); break;
    case SDLK_l: if (!cmd) set_tool(a, T_LINE); break;
    case SDLK_o: if (!cmd) set_tool(a, T_ELLIPSE); break;
    case SDLK_f: if (!cmd) set_tool(a, T_FILL); break;
    case SDLK_e: case SDLK_x: if (!cmd) set_tool(a, T_ERASER); break;
    case SDLK_g: if (!cmd) a->show_grid = !a->show_grid; break;
    case SDLK_z: if (cmd) run_action(a, A_UNDO); break;
    case SDLK_s: if (cmd) run_action(a, A_SAVE); break;
    case SDLK_n: if (cmd) run_action(a, A_CLEAR); break;
    case SDLK_DELETE: case SDLK_BACKSPACE: run_action(a, A_CLEAR); break;
    case SDLK_ESCAPE: cancel_drag(a); break;
    default: break;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    static App app;
    App *a = &app;
    a->tool = T_PENCIL;
    a->show_grid = true;
    autosave_path(a->save_path, sizeof a->save_path);
    autoload(a);

    a->win = SDL_CreateWindow("TinyPainter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 820,
                              720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a->win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(a->win, 360, 300);
    a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->ren) a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_SOFTWARE);
    if (!a->ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    update_title(a);

    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 250)) continue;
        do {
            switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                handle_mouse_down(a, &e.button);
                break;
            case SDL_MOUSEBUTTONUP:
                if (a->dragging && e.button.button == a->drag_button) end_drag(a);
                break;
            case SDL_MOUSEMOTION:
                a->shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                handle_motion(a, e.motion.x, e.motion.y);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_LEAVE) a->hover = false;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) end_drag(a);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (shift != a->shift) {
                    a->shift = shift;
                    update_overlay(a);
                }
                if (e.type == SDL_KEYDOWN) handle_key(a, &e.key);
                break;
            }
            default:
                break;
            }
        } while (SDL_PollEvent(&e));
        render(a);
    }

    end_drag(a);
    autosave(a);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
