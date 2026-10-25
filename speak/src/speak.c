#include "speak.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t sp_next(const char *s, size_t i) {
    if (!s[i]) return i;
    i++;
    while (s[i] && is_cont((unsigned char)s[i])) i++;
    return i;
}

size_t sp_prev(const char *s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && is_cont((unsigned char)s[i])) i--;
    return i;
}

int sp_cells(const char *s, size_t a, size_t b) {
    int n = 0;
    for (size_t i = a; i < b && s[i]; i = sp_next(s, i)) n++;
    return n;
}

size_t sp_advance(const char *s, size_t a, size_t b, int cells) {
    size_t i = a;
    while (cells-- > 0 && i < b && s[i] && s[i] != '\n') i = sp_next(s, i);
    return i;
}

int sp_wrap(const char *s, int cols, size_t *starts, int max) {
    if (cols < 1) cols = 1;
    int n = 0;
#define PUSH(v) do { if (n < max) starts[n] = (v); n++; } while (0)
    PUSH(0);
    size_t ls = 0, j = 0, brk = 0;
    int c = 0;
    while (s[j]) {
        if (s[j] == '\n') {
            ls = j + 1;
        } else if (c == cols) {
            /* A space right at the edge hangs off the line instead of
               starting the next one. */
            ls = s[j] == ' ' ? j + 1 : brk > ls ? brk : j;
        } else {
            if (s[j] == ' ') brk = j + 1;
            j = sp_next(s, j);
            c++;
            continue;
        }
        PUSH(ls);
        j = ls;
        c = 0;
        brk = 0;
    }
#undef PUSH
    return n;
}

size_t sp_line_end(const char *s, const size_t *starts, int n, int k) {
    size_t e = k + 1 < n ? starts[k + 1] : strlen(s);
    if (e > starts[k] && s[e - 1] == '\n') e--;
    return e;
}

/* ------------------------------------------------------------------ */
/* Prefix. */

static const char *const STOP[] = {
    "a", "about", "after", "again", "all", "also", "am", "an", "and", "any", "are", "as", "at",
    "be", "because", "been", "before", "but", "by", "can", "could", "dear", "did", "do", "does",
    "dr", "each", "every", "for", "from", "good", "had", "has", "have", "he", "hello", "her",
    "here", "hey", "hi", "his", "how", "i", "if", "in", "into", "is", "it", "its", "just", "let",
    "lets", "like", "may", "me", "might", "mr", "mrs", "ms", "must", "my", "no", "not", "now",
    "of", "oh", "ok", "okay", "on", "once", "one", "or", "our", "please", "she", "should", "so",
    "some", "st", "thank", "thanks", "that", "the", "their", "them", "then", "there", "these",
    "they", "this", "those", "to", "today", "tomorrow", "too", "up", "very", "was", "we", "well",
    "were", "what", "when", "where", "which", "while", "who", "why", "will", "with", "would",
    "yes", "yesterday", "you", "your",
};

static bool is_word_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'' ||
           c >= 0x80;
}

/* The word lower-cased and reduced to [a-z0-9] ("o'brien" -> "obrien"). */
static void clean(const char *w, size_t len, char *out, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < len && k + 1 < n; i++) {
        char c = w[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = c;
    }
    out[k] = 0;
}

static bool is_stop(const char *w) {
    for (size_t i = 0; i < sizeof STOP / sizeof *STOP; i++)
        if (!strcmp(STOP[i], w)) return true;
    return false;
}

typedef struct {
    const char *at;
    size_t len;
    bool cap, initial;
} Word;

void sp_prefix(const char *s, char *out, size_t n) {
    enum { MAXW = 512 };
    Word words[MAXW];
    int nw = 0;
    bool initial = true;
    for (size_t i = 0; s[i] && nw < MAXW;) {
        unsigned char c = (unsigned char)s[i];
        if (!is_word_byte(c) || c == '\'') {
            if (c == '.' || c == '!' || c == '?' || c == '\n' || c == ':' || c == ';') initial = true;
            i++;
            continue;
        }
        size_t a = i;
        while (s[i] && is_word_byte((unsigned char)s[i])) i++;
        words[nw++] = (Word){s + a, i - a, c >= 'A' && c <= 'Z', initial};
        initial = false;
    }
    char w[64];
    for (int pass = 0; pass < 3; pass++) {
        for (int k = 0; k < nw; k++) {
            const Word *x = &words[k];
            if (pass == 0 && !(x->cap && !x->initial)) continue;
            if (pass == 1 && !(x->cap && x->initial)) continue;
            clean(x->at, x->len, w, sizeof w);
            if (!w[0] || is_stop(w)) continue;
            if (pass == 2 && strlen(w) < 2) continue;
            snprintf(out, n, "%s", w);
            return;
        }
    }
    snprintf(out, n, "speech");
}

bool sp_save_path(const char *dir, const char *prefix, bool (*exists)(const char *path, void *ud),
                  void *ud, char *out, size_t n) {
    for (int i = 1; i <= 999; i++) {
        int len = snprintf(out, n, "%s/%s_%03d.wav", dir, prefix, i);
        if (len < 0 || (size_t)len >= n) return false;
        if (!exists(out, ud)) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Plain text from files. */

typedef struct {
    char *p;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

/* Line breaks to '\n', tabs to spaces, typography to ASCII (the font is
   ASCII), control characters dropped. */
static char *normalize(const char *s, size_t n) {
    static const struct { const char *from, *to; } MAP[] = {
        {"\xef\xbb\xbf", ""},  {"\xc2\xa0", " "},     {"\xe2\x80\x98", "'"},   {"\xe2\x80\x99", "'"},
        {"\xe2\x80\x9c", "\""}, {"\xe2\x80\x9d", "\""}, {"\xe2\x80\x93", "-"},   {"\xe2\x80\x94", " - "},
        {"\xe2\x80\xa6", "..."},
    };
    Buf b = {0};
    put(&b, "", 0);
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') {
            put(&b, "\n", 1);
            i += i + 1 < n && s[i + 1] == '\n' ? 2 : 1;
            continue;
        }
        if (c == '\t') c = ' ';
        if (c < 0x20 && c != '\n') {
            i++;
            continue;
        }
        bool mapped = false;
        for (size_t k = 0; c >= 0x80 && k < sizeof MAP / sizeof *MAP && !mapped; k++) {
            size_t fl = strlen(MAP[k].from);
            if (i + fl <= n && !memcmp(s + i, MAP[k].from, fl)) {
                put(&b, MAP[k].to, strlen(MAP[k].to));
                i += fl;
                mapped = true;
            }
        }
        if (mapped) continue;
        char ch = (char)c;
        put(&b, &ch, 1);
        i++;
    }
    return b.p;
}

enum { L_TEXT, L_ITEM, L_ROW, L_HEAD };

typedef struct {
    Buf out;
    int brk;   /* before the next line: 1 a line break, 2 a blank line */
    bool open; /* the last line is text the next one may continue */
} Para;

static void blank(Para *p) {
    p->open = false;
    if (p->out.len) p->brk = 2;
}

/* Appends one line of content, joined to the paragraph or on its own. */
static void emit_line(Para *p, char *c, int kind, bool md) {
    size_t k = 0;
    for (size_t i = 0; c[i]; i++)
        if (c[i] != ' ' || (k && c[k - 1] != ' ')) c[k++] = c[i];
    while (k && c[k - 1] == ' ') k--;
    c[k] = 0;
    if (!k) return;
    if (kind == L_HEAD && p->out.len) p->brk = 2;
    char last = p->out.len ? p->out.p[p->out.len - 1] : 0;
    bool join = p->open && kind == L_TEXT && p->brk < 2 &&
                (md || (!strchr(".!?:;", last) && c[0] >= 'a' && c[0] <= 'z'));
    if (join) put(&p->out, " ", 1);
    else if (p->out.len) put(&p->out, "\n\n", p->brk >= 2 ? 2 : 1);
    put(&p->out, c, k);
    p->open = kind == L_TEXT || kind == L_ITEM;
    p->brk = kind == L_HEAD ? 2 : 1;
}

/* True if `s` is only characters from `set` (and spaces), at least `min` of them. */
static bool only_chars(const char *s, const char *set, int min) {
    int k = 0;
    for (; *s; s++) {
        if (*s == ' ') continue;
        if (!strchr(set, *s)) return false;
        k++;
    }
    return k >= min;
}

static char *list_item(char *s) {
    if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') return s + 2;
    int d = 0;
    while (s[d] >= '0' && s[d] <= '9') d++;
    if (d && d < 10 && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') return s + d + 2;
    return NULL;
}

static bool is_alnum(char c) { return isalnum((unsigned char)c) || (unsigned char)c >= 0x80; }

/* Markdown inline syntax off one line; `comment` carries an open <!-- -->. */
static void inline_md(const char *s, Buf *o, bool *comment) {
    for (size_t i = 0; s[i];) {
        char c = s[i];
        if (*comment) {
            const char *e = strstr(s + i, "-->");
            if (!e) return;
            i = (size_t)(e - s) + 3;
            *comment = false;
            continue;
        }
        if (c == '\\' && s[i + 1] && strchr("\\`*_{}[]()#+-.!|<>~", s[i + 1])) {
            put(o, s + i + 1, 1);
            i += 2;
            continue;
        }
        if (!strncmp(s + i, "<!--", 4)) {
            *comment = true;
            i += 4;
            continue;
        }
        if (c == '<' && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/')) {
            const char *e = strchr(s + i, '>');
            if (e) {
                /* A tag goes; an autolink keeps its address. */
                size_t a = i + 1, len = (size_t)(e - s) - a;
                char *in = strndup(s + a, len);
                if (!strchr(in, ' ') && (strstr(in, "://") || strchr(in, '@'))) put(o, in, len);
                free(in);
                i = (size_t)(e - s) + 1;
                continue;
            }
        }
        if (c == '!' && s[i + 1] == '[') { /* an image reads as its alt text */
            i++;
            continue;
        }
        if (c == '[') {
            const char *e = strchr(s + i, ']');
            if (e && (e[1] == '(' || e[1] == '[')) {
                const char *close = strchr(e + 2, e[1] == '(' ? ')' : ']');
                if (close) {
                    char *in = strndup(s + i + 1, (size_t)(e - s) - i - 1);
                    bool no = false;
                    inline_md(in, o, &no);
                    free(in);
                    i = (size_t)(close - s) + 1;
                    continue;
                }
            }
        }
        if (c == '*' && !(i && s[i - 1] == ' ' && s[i + 1] == ' ')) { /* "2 * 3" stays */
            i++;
            continue;
        }
        if (c == '`' || (c == '_' && !(i && is_alnum(s[i - 1]) && is_alnum(s[i + 1])))) {
            i++;
            continue;
        }
        if (c == '~' && s[i + 1] == '~') {
            i += 2;
            continue;
        }
        put(o, s + i, 1);
        i++;
    }
}

/* "| a | b |" -> "a, b". */
static char *table_row(const char *s) {
    Buf b = {0};
    put(&b, "", 0);
    while (*s) {
        while (*s == ' ' || *s == '|') s++;
        size_t n = strcspn(s, "|");
        while (n && s[n - 1] == ' ') n--;
        if (!n) break;
        if (b.len) put(&b, ", ", 2);
        put(&b, s, n);
        s += n;
    }
    return b.p;
}

char *sp_plain(const char *src, size_t n, bool md) {
    char *norm = normalize(src, n);
    Para P = {0};
    put(&P.out, "", 0);
    bool front = false, fence = false, comment = false;
    char fch = 0;
    int ln = 0;
    for (char *line = norm, *next; line; line = next, ln++) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = line;
        while (*s == ' ') s++;
        size_t e = strlen(s);
        while (e && s[e - 1] == ' ') s[--e] = 0;
        if (!*s) {
            if (!fence && !front) blank(&P);
            continue;
        }
        if (!md) {
            emit_line(&P, s, L_TEXT, false);
            continue;
        }
        if (ln == 0 && !strcmp(s, "---")) { /* YAML front matter */
            front = true;
            continue;
        }
        if (front) {
            if (!strcmp(s, "---") || !strcmp(s, "...")) front = false;
            continue;
        }
        if (fence) {
            if (s[0] == fch && s[1] == fch && s[2] == fch) fence = false;
            continue;
        }
        if (!strncmp(s, "```", 3) || !strncmp(s, "~~~", 3)) {
            fence = true;
            fch = s[0];
            blank(&P);
            continue;
        }
        while (*s == '>') { /* quotes read as plain text */
            s++;
            while (*s == ' ') s++;
        }
        if (!*s) {
            blank(&P);
            continue;
        }
        /* Setext heading underlines and horizontal rules. */
        if ((P.open && (only_chars(s, "=", 2) || only_chars(s, "-", 2))) || only_chars(s, "-", 3) ||
            only_chars(s, "*", 3) || only_chars(s, "_", 3)) {
            P.open = false;
            if (P.out.len) P.brk = 2;
            continue;
        }
        if (s[0] == '[') { /* link reference definitions */
            const char *b = strchr(s, ']');
            if (b && b[1] == ':') continue;
        }
        int kind = L_TEXT, h = 0;
        char *row = NULL, *item;
        while (s[h] == '#') h++;
        if (h >= 1 && h <= 6 && (s[h] == ' ' || !s[h])) {
            s += h;
            e = strlen(s);
            while (e && (s[e - 1] == '#' || s[e - 1] == ' ')) s[--e] = 0;
            kind = L_HEAD;
        } else if ((item = list_item(s))) {
            s = item;
            if (s[0] == '[' && strchr(" xX", s[1]) && s[2] == ']' && s[3] == ' ') s += 4; /* task boxes */
            kind = L_ITEM;
        } else if (strchr(s, '|')) {
            if (only_chars(s, "|:-", 3) && strchr(s, '-')) continue; /* the header rule */
            s = row = table_row(s);
            kind = L_ROW;
        }
        Buf o = {0};
        put(&o, "", 0);
        inline_md(s, &o, &comment);
        emit_line(&P, o.p, kind, true);
        free(o.p);
        free(row);
    }
    free(norm);
    while (P.out.len && (P.out.p[P.out.len - 1] == '\n' || P.out.p[P.out.len - 1] == ' '))
        P.out.p[--P.out.len] = 0;
    return P.out.p;
}

bool sp_split_path(const char *path, char *dir, size_t dn, char *stem, size_t sn) {
    const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
    const char *dsrc = slash ? path : ".";
    size_t dl = !slash ? 1 : slash == path ? 1 : (size_t)(slash - path);
    const char *dot = strrchr(base, '.');
    size_t bl = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (dl >= dn || bl >= sn || !bl) return false;
    memcpy(dir, dsrc, dl);
    dir[dl] = 0;
    memcpy(stem, base, bl);
    stem[bl] = 0;
    return true;
}

/* ------------------------------------------------------------------ */
/* Protocol. */

char *sp_escape(const char *s) {
    size_t len = strlen(s);
    char *o = malloc(2 * len + 1), *p = o;
    if (!o) return NULL;
    for (; *s; s++) {
        if (*s == '\\') *p++ = '\\', *p++ = '\\';
        else if (*s == '\n') *p++ = '\\', *p++ = 'n';
        else if (*s == '\t') *p++ = '\\', *p++ = 't';
        else if (*s != '\r') *p++ = *s;
    }
    *p = 0;
    return o;
}

static int split(char *s, char sep, char **items, int max) {
    int n = 0;
    if (!*s) return 0;
    while (n < max) {
        items[n++] = s;
        char *e = strchr(s, sep);
        if (!e) break;
        *e = 0;
        s = e + 1;
    }
    return n;
}

int sp_fields(char *line, char **fields, int max) {
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
    return split(line, '\t', fields, max);
}

int sp_list(char *s, char **items, int max) { return split(s, ',', items, max); }

uint64_t sp_hash(const char *s, uint64_t h) {
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    return h;
}
