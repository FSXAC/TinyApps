/*
 * Speak - a tiny front-end for MLX audio text-to-speech, built on SDL3.
 * Type, pick a model, voice and language, and press the big button (⌘↩):
 * the text is generated in the background and played when it's ready; the
 * same button plays the cached result again. Save copies it to ~/Downloads
 * as <noun>_NNN.wav. A .txt or .md file can be opened (⌘O, the File button,
 * drag and drop, or the command line); its text is cleaned up for speech,
 * and saves then go next to it as <file name>_NNN.wav.
 *
 * All the speech work happens in worker/speak_worker.py, run as a child
 * process that this app talks to over a line protocol (see its docstring).
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

#include "speak.h"
#include "tiny.h"

#define FLASH_NS (90 * SDL_NS_PER_MS)
#define CONFIRM_NS (1000 * SDL_NS_PER_MS)
#define MAX_MODELS 32
#define MAX_VOICES 128
#define MAX_LANGS 32
#define MAX_UNDO 64
#define MAX_FILE_BYTES (8 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Child processes: a reader thread turns stdout into queued lines and wakes
   the event loop with a user event. SDL's process pipes are non-blocking,
   so the thread polls them. */

typedef struct Line {
    struct Line *next;
    char text[];
} Line;

typedef struct {
    SDL_Process *proc;
    SDL_Thread *thread;
    SDL_Mutex *lock;
    Line *head, *tail;
    SDL_AtomicInt stop;
    bool eof; /* the reader saw the end of output (under lock) */
    int kind;
} Proc;

enum { PROC_WORKER, PROC_DOWNLOAD };
static Uint32 proc_event, open_event; /* open_event: data1 is a malloc'd path */

static void wake(int kind) {
    SDL_Event e = {0};
    e.type = proc_event;
    e.user.code = kind;
    SDL_PushEvent(&e);
}

static int proc_reader(void *ud) {
    Proc *p = ud;
    SDL_IOStream *out = SDL_GetProcessOutput(p->proc);
    char buf[4096], *acc = NULL;
    size_t len = 0;
    while (!SDL_GetAtomicInt(&p->stop)) {
        size_t n = SDL_ReadIO(out, buf, sizeof buf);
        if (n == 0) {
            if (SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY) {
                SDL_Delay(10);
                continue;
            }
            break;
        }
        acc = realloc(acc, len + n + 1);
        memcpy(acc + len, buf, n);
        len += n;
        char *nl;
        bool any = false;
        while ((nl = memchr(acc, '\n', len))) {
            size_t ll = (size_t)(nl - acc);
            Line *l = malloc(sizeof(Line) + ll + 1);
            memcpy(l->text, acc, ll);
            l->text[ll] = 0;
            l->next = NULL;
            SDL_LockMutex(p->lock);
            if (p->tail) p->tail->next = l;
            else p->head = l;
            p->tail = l;
            SDL_UnlockMutex(p->lock);
            memmove(acc, nl + 1, len - ll - 1);
            len -= ll + 1;
            any = true;
        }
        if (any) wake(p->kind);
    }
    free(acc);
    SDL_LockMutex(p->lock);
    p->eof = true;
    SDL_UnlockMutex(p->lock);
    wake(p->kind);
    return 0;
}

static bool proc_start(Proc *p, int kind, const char *const *args, bool with_stdin) {
    memset(p, 0, sizeof *p);
    p->kind = kind;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          with_stdin ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    p->proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p->proc) {
        fprintf(stderr, "speak: can't start %s: %s\n", args[0], SDL_GetError());
        return false;
    }
    p->lock = SDL_CreateMutex();
    p->thread = SDL_CreateThread(proc_reader, "speak-reader", p);
    return true;
}

static bool proc_running(const Proc *p) { return p->proc != NULL; }

static void proc_stop(Proc *p) {
    if (!p->proc) return;
    SDL_SetAtomicInt(&p->stop, 1);
    SDL_KillProcess(p->proc, true);
    SDL_WaitThread(p->thread, NULL);
    SDL_DestroyProcess(p->proc);
    for (Line *l = p->head, *n; l; l = n) {
        n = l->next;
        free(l);
    }
    SDL_DestroyMutex(p->lock);
    memset(p, 0, sizeof *p);
}

/* Pops the next line (caller frees) or NULL; *eof is set once output has
   ended and every line has been taken. */
static Line *proc_pop(Proc *p, bool *eof) {
    *eof = false;
    if (!p->proc) return NULL;
    SDL_LockMutex(p->lock);
    Line *l = p->head;
    if (l) {
        p->head = l->next;
        if (!p->head) p->tail = NULL;
    } else {
        *eof = p->eof;
    }
    SDL_UnlockMutex(p->lock);
    return l;
}

static bool proc_send(Proc *p, const char *line) {
    if (!p->proc) return false;
    SDL_IOStream *in = SDL_GetProcessInput(p->proc);
    size_t len = strlen(line), off = 0;
    for (int tries = 0; off < len && tries < 1000; tries++) {
        size_t n = SDL_WriteIO(in, line + off, len - off);
        off += n;
        if (!n) {
            if (SDL_GetIOStatus(in) != SDL_IO_STATUS_NOT_READY) return false;
            SDL_Delay(2);
        }
    }
    SDL_FlushIO(in);
    return off == len;
}

/* ------------------------------------------------------------------ */
/* App state. */

typedef struct {
    char id[160];
    bool local;
} Model;

typedef struct {
    char code[32], name[48];
} Lang;

typedef struct {
    char *text;
    size_t cur;
} Undo;

typedef struct {
    int x, y, w, h;
} Rect;

enum { POP_NONE, POP_MODEL, POP_VOICE, POP_LANG };
enum { HIT_NONE, HIT_SEL_MODEL, HIT_SEL_VOICE, HIT_SEL_LANG, HIT_SEL_FILE, HIT_TEXT, HIT_PLAY, HIT_SAVE, HIT_POPUP };

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    const char *python, *worker_path;
    char tmpdir[1024], prefs[1024];

    /* Text box. */
    char *text;
    size_t len, cap, cur, anchor;
    int want_col, scroll;
    size_t *starts;
    int nstarts, starts_cap, cols;
    Undo undo[MAX_UNDO];
    int nundo, last_edit;
    uint64_t last_edit_at;
    bool dragging;
    char file[1024]; /* the opened .txt/.md; saves go next to it */

    /* Selections. */
    Model models[MAX_MODELS];
    int nmodels, model;
    bool models_ready;
    char voices[MAX_VOICES][48];
    int nvoices;
    char voice[48];
    Lang langs[MAX_LANGS];
    int nlangs;
    char lang[32];
    bool info_pending;
    uint64_t model_bytes;

    /* Worker and jobs. */
    Proc worker, dl;
    int job;
    bool generating;
    float gen_frac;
    char gen_stage[32], gen_note[256];
    char *gen_key;
    char *cache_key, *cache_text;
    char cache_path[1100], saved_path[1400];
    double cache_secs;
    char dl_model[160];
    uint64_t dl_done, dl_total;

    /* Playback. */
    SDL_AudioStream *stream;
    int play_total;
    uint64_t play_end_at;

    /* UI. */
    int popup, hover_item, pressed, mx, my;
    uint64_t flash_play_until, confirm_save_until;
    char status[512];
    bool status_err;
    char shown[700];      /* the status row as last drawn */
    const char *shot_to; /* SPEAK_DRIVE: save the next frame here */
} App;

static void set_status(App *a, bool err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_err = err;
}

static const char *model_label(const char *id) {
    const char *p = "mlx-community/";
    return strncmp(id, p, strlen(p)) ? id : id + strlen(p);
}

static Model *cur_model(App *a) { return a->model >= 0 && a->model < a->nmodels ? &a->models[a->model] : NULL; }

static const char *lang_name(App *a, const char *code) {
    for (int i = 0; i < a->nlangs; i++)
        if (!strcmp(a->langs[i].code, code)) return a->langs[i].name;
    return NULL;
}

/* Fingerprint of a request: model, voice, language and text. */
static char *request_key(App *a) {
    Model *m = cur_model(a);
    if (!m) return NULL;
    size_t n = strlen(m->id) + strlen(a->voice) + strlen(a->lang) + a->len + 4;
    char *k = malloc(n);
    snprintf(k, n, "%s\x1f%s\x1f%s\x1f%s", m->id, a->voice, a->lang, a->text);
    return k;
}

static bool text_blank(const App *a) {
    for (size_t i = 0; i < a->len; i++)
        if (a->text[i] != ' ' && a->text[i] != '\n' && a->text[i] != '\t') return false;
    return true;
}

static bool key_is(App *a, const char *key) {
    if (!key) return false;
    char *k = request_key(a);
    bool same = k && !strcmp(k, key);
    free(k);
    return same;
}

static bool cache_valid(App *a) { return key_is(a, a->cache_key); }
static bool playing(const App *a) { return a->stream != NULL; }

/* ------------------------------------------------------------------ */
/* Prefs: the last model, voice and language. */

static void save_prefs(App *a) {
    Model *m = cur_model(a);
    if (!a->prefs[0] || !m) return;
    FILE *f = fopen(a->prefs, "w");
    if (!f) return;
    fprintf(f, "model\t%s\nvoice\t%s\nlang\t%s\n", m->id, a->voice, a->lang);
    fclose(f);
}

static void load_prefs(App *a, char *model, size_t n) {
    model[0] = 0;
    FILE *f = a->prefs[0] ? fopen(a->prefs, "r") : NULL;
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *fld[2];
        if (sp_fields(line, fld, 2) != 2) continue;
        if (!strcmp(fld[0], "model")) snprintf(model, n, "%s", fld[1]);
        else if (!strcmp(fld[0], "voice")) snprintf(a->voice, sizeof a->voice, "%s", fld[1]);
        else if (!strcmp(fld[0], "lang")) snprintf(a->lang, sizeof a->lang, "%s", fld[1]);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Worker requests. */

static bool ensure_worker(App *a) {
    if (proc_running(&a->worker)) return true;
    const char *args[] = {a->python, a->worker_path, "serve", NULL};
    if (!proc_start(&a->worker, PROC_WORKER, args, true)) {
        set_status(a, true, "Can't start the worker (%s)", a->python);
        return false;
    }
    return true;
}

static void request_info(App *a) {
    Model *m = cur_model(a);
    if (!m || !ensure_worker(a)) return;
    char line[256];
    snprintf(line, sizeof line, "info\t%s\n", m->id);
    a->info_pending = proc_send(&a->worker, line);
    a->nvoices = a->nlangs = 0;
    a->model_bytes = 0;
}

static void stop_playback(App *a) {
    if (a->stream) SDL_DestroyAudioStream(a->stream);
    a->stream = NULL;
}

static void start_playback(App *a) {
    stop_playback(a);
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(a->cache_path, &spec, &buf, &len)) {
        set_status(a, true, "Can't read audio: %s", SDL_GetError());
        return;
    }
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!a->stream) {
        set_status(a, true, "No audio device: %s", SDL_GetError());
        SDL_free(buf);
        return;
    }
    SDL_PutAudioStreamData(a->stream, buf, (int)len);
    SDL_FlushAudioStream(a->stream);
    SDL_ResumeAudioStreamDevice(a->stream);
    SDL_free(buf);
    a->play_total = (int)len;
    a->play_end_at = 0;
}

static float play_frac(const App *a) {
    if (!a->stream || a->play_total <= 0) return 0;
    int q = SDL_GetAudioStreamQueued(a->stream);
    return 1.0f - (float)q / (float)a->play_total;
}

static void start_generation(App *a) {
    char *key = request_key(a);
    if (!key || !ensure_worker(a)) {
        free(key);
        return;
    }
    if (a->generating && a->gen_key && !strcmp(a->gen_key, key)) {
        free(key); /* already on its way */
        return;
    }
    Model *m = cur_model(a);
    char *esc = sp_escape(a->text);
    char path[1100];
    a->job++;
    snprintf(path, sizeof path, "%s/%016llx-%d.wav", a->tmpdir, (unsigned long long)sp_hash(key, SP_HASH_INIT),
             a->job);
    size_t n = strlen(esc) + strlen(path) + 512;
    char *line = malloc(n);
    snprintf(line, n, "gen\t%d\t%s\t%s\t%s\t%s\t%s\n", a->job, m->id, a->voice, a->lang, path, esc);
    bool ok = proc_send(&a->worker, line);
    free(line);
    free(esc);
    if (!ok) {
        free(key);
        set_status(a, true, "Lost the worker; try again");
        proc_stop(&a->worker);
        return;
    }
    free(a->gen_key);
    a->gen_key = key;
    a->generating = true;
    a->gen_frac = 0;
    a->gen_note[0] = 0;
    snprintf(a->gen_stage, sizeof a->gen_stage, "Starting");
}

static void start_download(App *a) {
    Model *m = cur_model(a);
    if (!m || proc_running(&a->dl)) return;
    snprintf(a->dl_model, sizeof a->dl_model, "%s", m->id);
    const char *args[] = {a->python, a->worker_path, "download", a->dl_model, NULL};
    if (!proc_start(&a->dl, PROC_DOWNLOAD, args, false)) {
        set_status(a, true, "Can't start the download");
        return;
    }
    a->dl_done = 0;
    a->dl_total = a->model_bytes;
}

static bool downloading_current(App *a) {
    Model *m = cur_model(a);
    return m && proc_running(&a->dl) && !strcmp(a->dl_model, m->id);
}

/* The big button: stop, download, play or generate, whichever fits. */
static void main_action(App *a) {
    Model *m = cur_model(a);
    if (!m) return;
    if (playing(a)) {
        stop_playback(a);
        set_status(a, false, "Stopped");
        return;
    }
    if (!m->local) {
        if (downloading_current(a)) {
            proc_stop(&a->dl);
            set_status(a, false, "Download cancelled; it resumes where it left off");
        } else if (proc_running(&a->dl)) {
            set_status(a, false, "Another download is in progress");
        } else {
            start_download(a);
        }
        return;
    }
    if (text_blank(a)) {
        set_status(a, false, "Type something to say first");
        return;
    }
    if (!a->voice[0] || !a->lang[0]) {
        set_status(a, false, "Pick a voice and language");
        return;
    }
    if (cache_valid(a)) start_playback(a);
    else start_generation(a);
}

static bool path_exists(const char *path, void *ud) {
    (void)ud;
    struct stat st;
    return stat(path, &st) == 0;
}

/* `path` with $HOME shortened to "~". */
static const char *tilde(const char *path) {
    static char buf[1400];
    const char *home = getenv("HOME");
    size_t n = home ? strlen(home) : 0;
    if (n && !strncmp(path, home, n) && path[n] == '/') snprintf(buf, sizeof buf, "~%s", path + n);
    else snprintf(buf, sizeof buf, "%s", path);
    return buf;
}

static void save_action(App *a) {
    if (!cache_valid(a)) {
        set_status(a, false, a->generating ? "Still generating" : "Generate first, then save");
        return;
    }
    a->confirm_save_until = SDL_GetTicksNS() + CONFIRM_NS;
    if (a->saved_path[0] && path_exists(a->saved_path, NULL)) {
        set_status(a, false, "Already saved as %s", tilde(a->saved_path));
        return;
    }
    const char *home = getenv("HOME"), *over = getenv("SPEAK_SAVE_DIR");
    char dir[1024], prefix[256], path[1400];
    if (a->file[0]) { /* next to the opened file, named after it */
        if (!sp_split_path(a->file, dir, sizeof dir, prefix, sizeof prefix)) {
            set_status(a, true, "Path too long: %s", a->file);
            return;
        }
    } else {
        if (over && *over) snprintf(dir, sizeof dir, "%s", over);
        else snprintf(dir, sizeof dir, "%s/Downloads", home ? home : ".");
        sp_prefix(a->cache_text, prefix, sizeof prefix);
    }
    if (!sp_save_path(dir, prefix, path_exists, NULL, path, sizeof path)) {
        set_status(a, true, "No free file name in %s", dir);
        return;
    }
    if (!SDL_CopyFile(a->cache_path, path)) {
        set_status(a, true, "Save failed: %s", SDL_GetError());
        a->confirm_save_until = 0;
        return;
    }
    snprintf(a->saved_path, sizeof a->saved_path, "%s", path);
    set_status(a, false, "Saved to %s", tilde(path));
}

/* ------------------------------------------------------------------ */
/* Worker replies. */

static void on_info(App *a, char **f, int n) {
    Model *m = cur_model(a);
    if (n < 6 || !m || strcmp(f[1], m->id)) return; /* a reply for a model no longer selected */
    a->info_pending = false;
    m->local = f[2][0] == '1';
    a->model_bytes = strtoull(f[3], NULL, 10);

    char *items[MAX_VOICES];
    int nv = sp_list(f[4], items, MAX_VOICES);
    bool keep = false;
    for (int i = 0; i < nv; i++) {
        snprintf(a->voices[i], sizeof a->voices[i], "%s", items[i]);
        if (!strcmp(items[i], a->voice)) keep = true;
    }
    a->nvoices = nv;
    if (!keep) {
        a->voice[0] = 0;
        for (int i = 0; i < nv; i++)
            if (!strcmp(a->voices[i], "af_heart")) snprintf(a->voice, sizeof a->voice, "af_heart");
        if (!a->voice[0] && nv) snprintf(a->voice, sizeof a->voice, "%s", a->voices[0]);
    }

    /* Keep the language by code, else by name ("English (US)" ~ "English"). */
    char old_name[48] = "";
    const char *on = lang_name(a, a->lang);
    if (on) snprintf(old_name, sizeof old_name, "%s", on);
    char *langs[MAX_LANGS];
    int nl = sp_list(f[5], langs, MAX_LANGS);
    a->nlangs = 0;
    for (int i = 0; i < nl; i++) {
        char *eq = strchr(langs[i], '=');
        if (!eq) continue;
        *eq = 0;
        Lang *l = &a->langs[a->nlangs++];
        snprintf(l->code, sizeof l->code, "%s", langs[i]);
        snprintf(l->name, sizeof l->name, "%s", eq + 1);
    }
    if (!lang_name(a, a->lang)) {
        const char *pick = NULL;
        size_t wl = strcspn(old_name, " ");
        for (int i = 0; i < a->nlangs && !pick; i++)
            if (wl && !strncmp(a->langs[i].name, old_name, wl)) pick = a->langs[i].code;
        /* Kokoro voices are named for their language (af_ = American female). */
        for (int i = 0; i < a->nlangs && !pick; i++)
            if (strlen(a->langs[i].code) == 1 && a->langs[i].code[0] == a->voice[0]) pick = a->langs[i].code;
        if (!pick && a->nlangs) pick = a->langs[0].code;
        snprintf(a->lang, sizeof a->lang, "%s", pick ? pick : "");
    }
    save_prefs(a);
}

static void on_worker_line(App *a, char *line) {
    char *f[8];
    int n = sp_fields(line, f, 8);
    if (n < 1) return;
    if (!strcmp(f[0], "ready")) {
        if (!a->models_ready) set_status(a, false, "Loading models…");
    } else if (!strcmp(f[0], "model") && n >= 3 && a->nmodels < MAX_MODELS) {
        Model *m = &a->models[a->nmodels++];
        snprintf(m->id, sizeof m->id, "%s", f[1]);
        m->local = f[2][0] == '1';
    } else if (!strcmp(f[0], "models_end")) {
        a->models_ready = true;
        char want[160];
        load_prefs(a, want, sizeof want);
        a->model = -1;
        for (int i = 0; i < a->nmodels && a->model < 0; i++)
            if (!strcmp(a->models[i].id, want)) a->model = i;
        for (int i = 0; i < a->nmodels && a->model < 0; i++)
            if (a->models[i].local) a->model = i;
        if (a->model < 0 && a->nmodels) a->model = 0;
        set_status(a, false, "Type, then press Generate (⌘↩)");
        request_info(a);
    } else if (!strcmp(f[0], "info")) {
        on_info(a, f, n);
    } else if (!strcmp(f[0], "progress") && n >= 4 && atoi(f[1]) == a->job) {
        a->gen_frac = (float)atof(f[2]);
        snprintf(a->gen_stage, sizeof a->gen_stage, "%s", f[3]);
    } else if (!strcmp(f[0], "note") && n >= 3 && atoi(f[1]) == a->job) {
        snprintf(a->gen_note, sizeof a->gen_note, "%s", f[2]);
    } else if (!strcmp(f[0], "done") && n >= 4 && atoi(f[1]) == a->job) {
        /* One cached file at a time: a new generation replaces the old one. */
        a->generating = false;
        if (a->cache_path[0] && strcmp(a->cache_path, f[2])) remove(a->cache_path);
        snprintf(a->cache_path, sizeof a->cache_path, "%s", f[2]);
        free(a->cache_key);
        a->cache_key = a->gen_key;
        a->gen_key = NULL;
        free(a->cache_text);
        const char *t = strrchr(a->cache_key, '\x1f');
        a->cache_text = SDL_strdup(t ? t + 1 : "");
        a->cache_secs = atof(f[3]);
        a->saved_path[0] = 0;
        a->confirm_save_until = 0; /* that confirmation was for the old file */
        if (a->gen_note[0]) set_status(a, false, "Ready: %.1f s; %s", a->cache_secs, a->gen_note);
        else set_status(a, false, "Ready: %.1f s of audio. Play again (⌘↩) or Save (⌘S)", a->cache_secs);
        if (cache_valid(a)) start_playback(a); /* unless the text changed meanwhile */
    } else if (!strcmp(f[0], "error") && n >= 3) {
        Model *m = cur_model(a);
        if (a->generating && atoi(f[1]) == a->job) {
            a->generating = false;
            set_status(a, true, "%s", f[2]);
        } else if (m && !strcmp(f[1], m->id)) {
            a->info_pending = false;
            set_status(a, true, "Can't read %s: %s", model_label(m->id), f[2]);
        } else if (!strcmp(f[1], "models")) {
            set_status(a, true, "%s", f[2]);
        }
    }
}

static void on_download_line(App *a, char *line) {
    char *f[6];
    int n = sp_fields(line, f, 6);
    if (n >= 4 && !strcmp(f[0], "dl")) {
        a->dl_done = strtoull(f[2], NULL, 10);
        a->dl_total = strtoull(f[3], NULL, 10);
    } else if (n >= 2 && !strcmp(f[0], "dldone")) {
        for (int i = 0; i < a->nmodels; i++)
            if (!strcmp(a->models[i].id, f[1])) a->models[i].local = true;
        set_status(a, false, "Downloaded %s", model_label(f[1]));
        if (cur_model(a) && !strcmp(cur_model(a)->id, f[1])) request_info(a);
    } else if (n >= 3 && !strcmp(f[0], "error")) {
        set_status(a, true, "Download failed: %s", f[2]);
    }
}

static void drain(App *a, int kind) {
    Proc *p = kind == PROC_WORKER ? &a->worker : &a->dl;
    bool eof = false;
    Line *l;
    while ((l = proc_pop(p, &eof))) {
        if (kind == PROC_WORKER) on_worker_line(a, l->text);
        else on_download_line(a, l->text);
        free(l);
    }
    if (!eof) return;
    proc_stop(p);
    if (kind == PROC_WORKER && (a->generating || a->info_pending)) {
        set_status(a, true, "The worker quit (see the terminal)");
        a->generating = a->info_pending = false;
    }
}

/* ------------------------------------------------------------------ */
/* Text editing. */

enum { EDIT_OTHER, EDIT_TYPE, EDIT_DELETE, EDIT_WORD };

static void push_undo(App *a, int kind) {
    uint64_t now = SDL_GetTicksNS();
    bool coalesce = kind != EDIT_OTHER && kind == a->last_edit && now - a->last_edit_at < 1500 * SDL_NS_PER_MS;
    a->last_edit = kind;
    a->last_edit_at = now;
    if (coalesce) return;
    if (a->nundo == MAX_UNDO) {
        free(a->undo[0].text);
        memmove(a->undo, a->undo + 1, sizeof(Undo) * (MAX_UNDO - 1));
        a->nundo--;
    }
    a->undo[a->nundo++] = (Undo){SDL_strdup(a->text), a->cur};
}

static void set_text(App *a, const char *s) {
    size_t n = strlen(s);
    if (n + 1 > a->cap) {
        a->cap = n + 256;
        a->text = realloc(a->text, a->cap);
    }
    memcpy(a->text, s, n + 1);
    a->len = n;
}

static void do_undo(App *a) {
    if (!a->nundo) return;
    Undo u = a->undo[--a->nundo];
    set_text(a, u.text);
    free(u.text);
    a->cur = a->anchor = u.cur > a->len ? a->len : u.cur;
    a->last_edit = EDIT_OTHER;
}

/* Replaces bytes [s, e) with `ins` and puts the cursor after it. */
static void replace(App *a, size_t s, size_t e, const char *ins) {
    size_t il = strlen(ins);
    size_t n = a->len - (e - s) + il;
    if (n + 1 > a->cap) {
        a->cap = n + 256;
        a->text = realloc(a->text, a->cap);
    }
    memmove(a->text + s + il, a->text + e, a->len - e + 1);
    memcpy(a->text + s, ins, il);
    a->len = n;
    a->cur = a->anchor = s + il;
}

static size_t sel_lo(const App *a) { return a->cur < a->anchor ? a->cur : a->anchor; }
static size_t sel_hi(const App *a) { return a->cur < a->anchor ? a->anchor : a->cur; }

static void set_file(App *a, const char *path) {
    snprintf(a->file, sizeof a->file, "%s", path ? path : "");
    char title[1100];
    const char *slash = strrchr(a->file, '/');
    if (a->file[0]) snprintf(title, sizeof title, "Speak — %s", slash ? slash + 1 : a->file);
    else snprintf(title, sizeof title, "Speak");
    SDL_SetWindowTitle(a->win, title);
}

static void insert(App *a, const char *s, int kind) {
    push_undo(a, kind);
    replace(a, sel_lo(a), sel_hi(a), s);
    if (a->file[0] && text_blank(a)) set_file(a, NULL); /* cleared: back to typing */
}

static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\t'; }

static size_t word_left(const App *a, size_t i) {
    while (i > 0 && is_space(a->text[i - 1])) i--;
    while (i > 0 && !is_space(a->text[i - 1])) i--;
    return i;
}

static size_t word_right(const App *a, size_t i) {
    while (i < a->len && is_space(a->text[i])) i++;
    while (i < a->len && !is_space(a->text[i])) i++;
    return i;
}

static void rewrap(App *a, int cols) {
    a->cols = cols;
    int n = sp_wrap(a->text, cols, a->starts, a->starts_cap);
    if (n > a->starts_cap) {
        a->starts_cap = n + 64;
        a->starts = realloc(a->starts, sizeof(size_t) * (size_t)a->starts_cap);
        n = sp_wrap(a->text, cols, a->starts, a->starts_cap);
    }
    a->nstarts = n;
}

static int line_of(const App *a, size_t pos) {
    int k = 0;
    while (k + 1 < a->nstarts && a->starts[k + 1] <= pos) k++;
    return k;
}

static size_t line_end(const App *a, int k) { return sp_line_end(a->text, a->starts, a->nstarts, k); }
static int col_of(const App *a, size_t pos) { return sp_cells(a->text, a->starts[line_of(a, pos)], pos); }

static size_t pos_at(const App *a, int k, int col) {
    if (k < 0) return 0;
    if (k >= a->nstarts) return a->len;
    return sp_advance(a->text, a->starts[k], line_end(a, k), col);
}

static void copy_selection(App *a) {
    size_t lo = sel_lo(a), hi = sel_hi(a);
    if (lo == hi) return;
    char *s = malloc(hi - lo + 1);
    memcpy(s, a->text + lo, hi - lo);
    s[hi - lo] = 0;
    SDL_SetClipboardText(s);
    free(s);
}

/* ------------------------------------------------------------------ */
/* Opening files. */

static bool has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), e = strlen(ext);
    return n > e && !SDL_strcasecmp(path + n - e, ext);
}

/* Loads a .txt or .md file into the text box (one undo step back) and
   switches saves to its folder. */
static void open_file(App *a, const char *path) {
    const char *slash = strrchr(path, '/'), *name = slash ? slash + 1 : path;
    size_t n = 0;
    char *data = SDL_LoadFile(path, &n);
    if (!data) {
        set_status(a, true, "Can't open %s: %s", name, SDL_GetError());
        return;
    }
    if (n > MAX_FILE_BYTES || memchr(data, 0, n)) {
        set_status(a, true, n > MAX_FILE_BYTES ? "%s is too big (8 MB at most)" : "%s isn't a text file", name);
        SDL_free(data);
        return;
    }
    bool md = has_ext(path, ".md") || has_ext(path, ".markdown") || has_ext(path, ".mdown");
    char *text = sp_plain(data, n, md);
    SDL_free(data);
    if (!*text) {
        set_status(a, true, "Nothing to read in %s", name);
        free(text);
        return;
    }
    push_undo(a, EDIT_OTHER);
    set_text(a, text);
    free(text);
    a->cur = a->anchor = 0;
    a->scroll = 0;
    a->want_col = 0;
    char *real = SDL_strdup(path);
    char buf[PATH_MAX];
    if (realpath(path, buf)) SDL_free(real), real = SDL_strdup(buf);
    set_file(a, real);
    SDL_free(real);
    char dir[1024], stem[256];
    sp_split_path(a->file, dir, sizeof dir, stem, sizeof stem);
    set_status(a, false, "Opened %s; saves go to %s/%s_NNN.wav", name, tilde(dir), stem);
}

static void SDLCALL dialog_done(void *ud, const char *const *files, int filter) {
    (void)ud, (void)filter;
    if (!files) {
        fprintf(stderr, "speak: open dialog: %s\n", SDL_GetError());
        return;
    }
    if (!files[0]) return; /* cancelled */
    SDL_Event e = {0};
    e.type = open_event;
    e.user.data1 = SDL_strdup(files[0]); /* this may run on another thread */
    SDL_PushEvent(&e);
}

static void open_dialog(App *a) {
    static const SDL_DialogFileFilter filters[] = {{"Text and Markdown", "txt;md;markdown"}};
    SDL_ShowOpenFileDialog(dialog_done, a, a->win, filters, 1, a->file[0] ? a->file : NULL, false);
}

/* ------------------------------------------------------------------ */
/* Layout, in output pixels; `u` is the design unit. */

typedef struct {
    int u, w, h;
    Rect sel[4], text, bar, play, save;
    int label_y, hint_y, status_y;
    int body, caption, title, adv, lh, pad;
} Layout;

static Layout layout(App *a) {
    Layout L;
    SDL_GetRenderOutputSize(a->ren, &L.w, &L.h);
    int u = L.u = tiny_unit(a->win, a->ren);
    int M = TINY_MARGIN * u, P = TINY_PAD * u, G = TINY_GAP * u;
    L.body = TINY_BODY * u;
    L.caption = TINY_CAPTION * u;
    L.title = TINY_TITLE * u;
    L.adv = 6 * L.body;
    L.lh = tiny_line_height(L.body);
    L.pad = P;

    /* Selectors across the top; the model gets the most room. */
    L.label_y = M;
    int sy = M + tiny_text_height(L.caption) + G, sh = tiny_text_height(L.body) + 10 * u;
    int avail = L.w - 2 * M - 3 * P;
    int mw = avail * 32 / 100, vw = avail * 22 / 100, lw = avail * 28 / 100, fw = avail - mw - vw - lw;
    L.sel[0] = (Rect){M, sy, mw, sh};
    L.sel[1] = (Rect){M + mw + P, sy, vw, sh};
    L.sel[2] = (Rect){M + mw + vw + 2 * P, sy, lw, sh};
    L.sel[3] = (Rect){L.w - M - fw, sy, fw, sh};

    /* Bottom up: hint line, buttons, then the progress/status row. */
    L.hint_y = L.h - M - tiny_text_height(L.caption);
    int ph = 44 * u, py = L.hint_y - P - ph;
    int savew = SDL_max(96 * u, tiny_text_width("Save", L.title, TINY_BOLD) + 2 * M);
    L.play = (Rect){M, py, L.w - 2 * M - savew - P, ph};
    L.save = (Rect){L.w - M - savew, py, savew, ph};
    int bh = 10 * u;
    L.status_y = py - P - G - tiny_text_height(L.body);
    L.bar = (Rect){M, L.status_y + (tiny_text_height(L.body) - bh) / 2, (L.w - 2 * M) / 2, bh};

    /* The text box takes everything in between. */
    int ty = sy + sh + P + G;
    L.text = (Rect){M, ty, L.w - 2 * M, L.status_y - P - G - ty};
    return L;
}

static int text_cols(const Layout *L) { return SDL_max(1, (L->text.w - 2 * L->pad) / L->adv); }
static int text_rows(const Layout *L) {
    return SDL_max(1, (L->text.h - 2 * L->pad + L->lh - tiny_text_height(L->body)) / L->lh);
}

static void keep_cursor_visible(App *a, const Layout *L) {
    rewrap(a, text_cols(L));
    int k = line_of(a, a->cur), rows = text_rows(L);
    if (k < a->scroll) a->scroll = k;
    if (k >= a->scroll + rows) a->scroll = k - rows + 1;
    a->scroll = SDL_clamp(a->scroll, 0, SDL_max(0, a->nstarts - rows));
}

static bool in_rect(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* Popup menu: items in as many columns as the window needs. */
typedef struct {
    Rect panel;
    int n, rows, cols, item_h, col_w;
} Popup;

static int popup_count(App *a) {
    return a->popup == POP_MODEL ? a->nmodels : a->popup == POP_VOICE ? a->nvoices : a->popup == POP_LANG ? a->nlangs : 0;
}

static const char *popup_item(App *a, int i, char *buf, size_t n) {
    if (a->popup == POP_MODEL) {
        const Model *m = &a->models[i];
        snprintf(buf, n, "%s%s", model_label(m->id), m->local ? "" : "  ↓");
        return buf;
    }
    if (a->popup == POP_VOICE) return a->voices[i];
    return a->langs[i].name;
}

static bool popup_current(App *a, int i) {
    if (a->popup == POP_MODEL) return i == a->model;
    if (a->popup == POP_VOICE) return !strcmp(a->voices[i], a->voice);
    return !strcmp(a->langs[i].code, a->lang);
}

static Popup popup_layout(App *a, const Layout *L) {
    Popup p = {0};
    p.n = popup_count(a);
    if (!p.n) return p;
    int u = L->u;
    Rect anchor = L->sel[a->popup - POP_MODEL];
    p.item_h = tiny_text_height(L->body) + 6 * u;
    int top = anchor.y + anchor.h + 2 * u;
    int fit = SDL_max(1, (L->h - top - TINY_MARGIN * u - 2 * u) / p.item_h);
    p.cols = (p.n + fit - 1) / fit;
    p.rows = (p.n + p.cols - 1) / p.cols;
    char buf[200];
    int tw = 0;
    for (int i = 0; i < p.n; i++)
        tw = SDL_max(tw, tiny_text_width(popup_item(a, i, buf, sizeof buf), L->body, TINY_PLAIN));
    p.col_w = tw + 2 * TINY_PAD * u + 8 * u; /* room for the current-item bullet */
    int w = SDL_max(anchor.w, p.cols * p.col_w);
    if (p.cols == 1) p.col_w = w;
    int x = anchor.x;
    if (x + w > L->w - TINY_MARGIN * u) x = SDL_max(TINY_MARGIN * u, L->w - TINY_MARGIN * u - w);
    p.panel = (Rect){x, top, w, p.rows * p.item_h + 2 * u};
    return p;
}

static int popup_hit(App *a, const Layout *L, int x, int y) {
    Popup p = popup_layout(a, L);
    if (!p.n || !in_rect(p.panel, x, y)) return -1;
    int c = (x - p.panel.x) / p.col_w, r = (y - p.panel.y - L->u) / p.item_h;
    if (c < 0 || c >= p.cols || r < 0 || r >= p.rows) return -1;
    int i = c * p.rows + r;
    return i < p.n ? i : -1;
}

static int hit(App *a, const Layout *L, int x, int y) {
    if (a->popup != POP_NONE) {
        Popup p = popup_layout(a, L);
        if (p.n && in_rect(p.panel, x, y)) return HIT_POPUP;
    }
    for (int i = 0; i < 4; i++)
        if (in_rect(L->sel[i], x, y)) return HIT_SEL_MODEL + i;
    if (in_rect(L->text, x, y)) return HIT_TEXT;
    if (in_rect(L->play, x, y)) return HIT_PLAY;
    if (in_rect(L->save, x, y)) return HIT_SAVE;
    return HIT_NONE;
}

static size_t text_pos_at(App *a, const Layout *L, int x, int y) {
    rewrap(a, text_cols(L));
    int dy = y - L->text.y - L->pad;
    int k = a->scroll + (dy < 0 ? -1 : dy / L->lh);
    if (k < 0) return 0;
    if (k >= a->nstarts) return a->len;
    int col = (x - L->text.x - L->pad + L->adv / 2) / L->adv;
    return pos_at(a, k, SDL_max(0, col));
}

/* ------------------------------------------------------------------ */
/* Drawing. */

/* Draws `s` within `maxw` pixels, ending in "…" if it doesn't fit. */
static void text_fit(App *a, const char *s, int x, int y, int scale, TinyStyle st, int maxw) {
    if (tiny_text_width(s, scale, st) <= maxw) {
        tiny_text(&a->font, s, x, y, scale, st);
        return;
    }
    char buf[640];
    size_t keep = 0;
    for (size_t i = 0; s[i];) {
        size_t nx = sp_next(s, i);
        if (nx + 4 >= sizeof buf) break;
        memcpy(buf, s, nx);
        memcpy(buf + nx, "…", 4);
        if (tiny_text_width(buf, scale, st) > maxw) break;
        keep = i = nx;
    }
    memcpy(buf, s, keep);
    memcpy(buf + keep, "…", 4);
    tiny_text(&a->font, buf, x, y, scale, st);
}

static TinyState btn_state(App *a, int id, Rect r, bool forced) {
    if (forced || (a->pressed == id && in_rect(r, a->mx, a->my))) return TINY_PRESSED;
    if (a->pressed == HIT_NONE && a->popup == POP_NONE && in_rect(r, a->mx, a->my)) return TINY_HOVER;
    return TINY_NORMAL;
}

/* Icons sized `s` pixels square, stepped in whole units. */
static void icon_play(SDL_Renderer *r, int x, int y, int s, int u) {
    int rows = s / u;
    for (int j = 0; j < rows; j++) {
        int d = j < rows / 2 ? j : rows - 1 - j;
        tiny_fill_rect(r, x, y + j * u, (d + 1) * u, u);
    }
}

static void icon_stop(SDL_Renderer *r, int x, int y, int s, int u) { tiny_fill_rect(r, x, y + u, s - 2 * u, s - 2 * u); }

static void icon_download(SDL_Renderer *r, int x, int y, int s, int u) {
    int rows = s / u, half = rows / 2, mid = x + (rows / 2) * u;
    tiny_fill_rect(r, mid - u, y, 3 * u, half * u);
    for (int j = 0; j < rows - half; j++) {
        int hw = SDL_max(0, rows - half - 1 - j);
        tiny_fill_rect(r, mid - hw * u, y + (half + j) * u, (2 * hw + 1) * u, u);
    }
}

static void disabled_button(SDL_Renderer *r, Rect b, int u) {
    tiny_frame_round(r, b.x, b.y, b.w, b.h, TINY_RADIUS, u, TINY_CORNERS, TINY_PAPER, TINY_GRAY);
}

/* A labelled button; the first three open menus (▾), the last a file dialog. */
static void draw_selector(App *a, const Layout *L, int i, const char *label, const char *value, bool enabled) {
    Rect r = L->sel[i];
    int u = L->u;
    tiny_set_color(a->ren, TINY_DIM);
    tiny_text(&a->font, label, r.x, L->label_y, L->caption, TINY_PLAIN);
    TinyColor fg = TINY_GRAY;
    if (enabled) fg = tiny_button(a->ren, r.x, r.y, r.w, r.h, u, btn_state(a, HIT_SEL_MODEL + i, r, a->popup == POP_MODEL + i));
    else disabled_button(a->ren, r, u);
    tiny_set_color(a->ren, fg);
    bool menu = i < 3;
    int arrow = menu ? tiny_text_width("▾", L->body, TINY_PLAIN) + TINY_PAD * u : 0;
    int ty = r.y + (r.h - tiny_text_height(L->body)) / 2;
    text_fit(a, value, r.x + TINY_PAD * u, ty, L->body, TINY_PLAIN, r.w - 2 * TINY_PAD * u - arrow);
    if (menu) tiny_text(&a->font, "▾", r.x + r.w - arrow, ty, L->body, TINY_PLAIN);
}

static void draw_text_box(App *a, const Layout *L) {
    SDL_Renderer *r = a->ren;
    Rect b = L->text;
    int u = L->u;
    tiny_frame_round(r, b.x, b.y, b.w, b.h, 0, u, 0, TINY_PAPER, TINY_INK);
    keep_cursor_visible(a, L);
    int rows = text_rows(L), tx = b.x + L->pad, ty0 = b.y + L->pad, th = tiny_text_height(L->body);
    if (a->len == 0) {
        tiny_set_color(r, TINY_GRAY);
        tiny_text(&a->font, "Type something to say…", tx, ty0, L->body, TINY_PLAIN);
    }
    size_t lo = sel_lo(a), hi = sel_hi(a);
    char *buf = malloc((size_t)a->cols * 4 + 16);
    for (int k = a->scroll; k < a->nstarts && k < a->scroll + rows; k++) {
        size_t s = a->starts[k], e = line_end(a, k);
        int y = ty0 + (k - a->scroll) * L->lh;
        memcpy(buf, a->text + s, e - s);
        buf[e - s] = 0;
        tiny_set_color(r, TINY_INK);
        tiny_text(&a->font, buf, tx, y, L->body, TINY_PLAIN);
        /* Selection: an inverted chip over the selected cells, plus one cell
           for a selected line break. */
        size_t ss = SDL_max(lo, s), se = SDL_min(hi, e);
        bool nl = hi > e && lo <= e && e < a->len && a->text[e] == '\n';
        if (ss < se || nl) {
            int c0 = sp_cells(a->text, s, ss), c1 = sp_cells(a->text, s, SDL_max(ss, se)) + (nl ? 1 : 0);
            tiny_set_color(r, TINY_INK);
            tiny_fill_round(r, tx + c0 * L->adv - u, y - 2 * u, (c1 - c0) * L->adv + u, th + 4 * u,
                            TINY_RADIUS_SMALL, u, TINY_CORNERS);
            if (ss < se) {
                memcpy(buf, a->text + ss, se - ss);
                buf[se - ss] = 0;
                tiny_set_color(r, TINY_PAPER);
                tiny_text(&a->font, buf, tx + c0 * L->adv, y, L->body, TINY_PLAIN);
            }
        }
    }
    free(buf);
    if (lo == hi && a->popup == POP_NONE) {
        int k = line_of(a, a->cur);
        if (k >= a->scroll && k < a->scroll + rows) {
            int x = tx + col_of(a, a->cur) * L->adv - u, y = ty0 + (k - a->scroll) * L->lh - 2 * u;
            tiny_set_color(r, TINY_INK);
            tiny_fill_rect(r, x, y, u, th + 4 * u);
            float d = tiny_pixel_density(a->win, r);
            SDL_Rect area = {(int)(x / d), (int)(y / d), 1, (int)(L->lh / d)};
            SDL_SetTextInputArea(a->win, &area, 0);
        }
    }
    /* Scroll thumb, when there's more text than fits. */
    if (a->nstarts > rows) {
        int track = b.h - 2 * L->pad, tth = SDL_max(8 * u, track * rows / a->nstarts);
        int y = b.y + L->pad + (track - tth) * a->scroll / SDL_max(1, a->nstarts - rows);
        tiny_set_color(r, TINY_GRAY);
        tiny_fill_rect(r, b.x + b.w - 4 * u, y, 2 * u, tth);
    }
}

static void draw_bar(App *a, const Layout *L, float frac) {
    Rect b = L->bar;
    int u = L->u;
    tiny_frame_round(a->ren, b.x, b.y, b.w, b.h, TINY_RADIUS_SMALL, u, TINY_CORNERS, TINY_PAPER, TINY_INK);
    int iw = b.w - 4 * u, fw = (int)(SDL_clamp(frac, 0.0f, 1.0f) * (float)iw) / u * u;
    tiny_set_color(a->ren, TINY_INK);
    tiny_fill_rect(a->ren, b.x + 2 * u, b.y + 2 * u, fw, b.h - 4 * u);
}

static void draw_popup(App *a, const Layout *L) {
    Popup p = popup_layout(a, L);
    if (!p.n) return;
    SDL_Renderer *r = a->ren;
    int u = L->u;
    tiny_panel(r, p.panel.x, p.panel.y, p.panel.w, p.panel.h, u);
    char buf[200];
    for (int i = 0; i < p.n; i++) {
        int x = p.panel.x + (i / p.rows) * p.col_w, y = p.panel.y + u + (i % p.rows) * p.item_h;
        bool hov = i == a->hover_item;
        if (hov) {
            tiny_set_color(r, TINY_INK);
            tiny_fill_round(r, x + 2 * u, y, p.col_w - 4 * u, p.item_h, TINY_RADIUS_SMALL, u, TINY_CORNERS);
        }
        bool dim = a->popup == POP_MODEL && !a->models[i].local;
        tiny_set_color(r, hov ? TINY_PAPER : dim ? TINY_DIM : TINY_INK);
        int ty = y + (p.item_h - tiny_text_height(L->body)) / 2;
        if (popup_current(a, i)) tiny_text(&a->font, "•", x + 4 * u, ty, L->body, TINY_PLAIN);
        tiny_text(&a->font, popup_item(a, i, buf, sizeof buf), x + TINY_PAD * u + 8 * u, ty, L->body, TINY_PLAIN);
    }
}

static void fmt_size(char *buf, size_t n, uint64_t bytes) {
    if (bytes >= 1000000000ULL) snprintf(buf, n, "%.1f GB", (double)bytes / 1e9);
    else snprintf(buf, n, "%d MB", (int)((bytes + 500000) / 1000000));
}

static void render(App *a) {
    SDL_Renderer *r = a->ren;
    Layout L = layout(a);
    int u = L.u;
    uint64_t now = SDL_GetTicksNS();
    tiny_set_color(r, TINY_PAPER);
    SDL_RenderClear(r);

    Model *m = cur_model(a);
    const char *ln = lang_name(a, a->lang);
    draw_selector(a, &L, 0, "Model", m ? model_label(m->id) : "Loading…", a->models_ready);
    draw_selector(a, &L, 1, "Voice", a->nvoices ? a->voice : a->info_pending ? "Loading…" : "–", a->nvoices > 0);
    draw_selector(a, &L, 2, "Language", ln ? ln : a->info_pending ? "Loading…" : "–", a->nlangs > 0);
    const char *slash = strrchr(a->file, '/');
    draw_selector(a, &L, 3, "File (.txt, .md)", a->file[0] ? (slash ? slash + 1 : a->file) : "Open…", true);

    draw_text_box(a, &L);

    /* Progress row: the bar appears while generating, downloading or playing;
       otherwise the latest status message. */
    bool dl = downloading_current(a);
    bool busy = a->generating || dl || playing(a);
    int sx = TINY_MARGIN * u, sw = L.w - 2 * TINY_MARGIN * u;
    char line[700];
    if (busy) {
        float frac;
        if (dl) {
            char d[32], t[32];
            fmt_size(d, sizeof d, a->dl_done);
            fmt_size(t, sizeof t, a->dl_total);
            snprintf(line, sizeof line, "Downloading %s / %s", d, t);
            frac = a->dl_total ? (float)a->dl_done / (float)a->dl_total : 0;
        } else if (playing(a)) {
            frac = play_frac(a);
            snprintf(line, sizeof line, "Playing %.1f / %.1f s", frac * a->cache_secs, a->cache_secs);
        } else {
            frac = a->gen_frac;
            snprintf(line, sizeof line, "%s… %d%%", a->gen_stage, (int)(a->gen_frac * 100 + 0.5f));
        }
        draw_bar(a, &L, frac);
        sx = L.bar.x + L.bar.w + TINY_PAD * u;
        sw = L.w - TINY_MARGIN * u - sx;
        tiny_set_color(r, TINY_INK);
    } else {
        snprintf(line, sizeof line, "%s", a->status);
        tiny_set_color(r, a->status_err ? TINY_INK : TINY_DIM);
    }
    text_fit(a, line, sx, L.status_y, L.body, !busy && a->status_err ? TINY_BOLD : TINY_PLAIN, sw);
    snprintf(a->shown, sizeof a->shown, "%s", line);

    /* The big button. */
    const char *label = "Generate";
    int icon = 0; /* 0 play, 1 stop, 2 download */
    bool enabled = m != NULL;
    char dlbuf[64];
    if (playing(a)) {
        label = "Stop", icon = 1;
    } else if (m && !m->local) {
        if (dl) {
            label = "Cancel", icon = 1;
        } else {
            char sz[32];
            fmt_size(sz, sizeof sz, a->model_bytes);
            snprintf(dlbuf, sizeof dlbuf, a->model_bytes ? "Download %s" : "Download", sz);
            label = dlbuf, icon = 2;
            enabled = !proc_running(&a->dl) && !a->info_pending;
        }
    } else if (cache_valid(a)) {
        label = "Play";
    } else if (a->generating && key_is(a, a->gen_key)) {
        label = "Generating";
    } else {
        enabled = enabled && !text_blank(a) && a->nvoices > 0;
    }
    Rect pb = L.play;
    TinyColor fg = TINY_GRAY;
    if (!enabled) disabled_button(r, pb, u);
    else if (now < a->flash_play_until) {
        tiny_frame_round(r, pb.x, pb.y, pb.w, pb.h, TINY_RADIUS, u, TINY_CORNERS, TINY_SHADE, TINY_INK);
        fg = TINY_INK;
    } else {
        fg = tiny_button(r, pb.x, pb.y, pb.w, pb.h, u, btn_state(a, HIT_PLAY, pb, false));
    }
    int is = tiny_text_height(L.title);
    int lw = tiny_text_width(label, L.title, TINY_BOLD), gap = TINY_PAD * u;
    int ix = pb.x + (pb.w - is - gap - lw) / 2, iy = pb.y + (pb.h - is) / 2;
    tiny_set_color(r, fg);
    if (icon == 0) icon_play(r, ix, iy, is, u);
    else if (icon == 1) icon_stop(r, ix, iy, is, u);
    else icon_download(r, ix, iy, is, u);
    tiny_text(&a->font, label, ix + is + gap, iy, L.title, TINY_BOLD);

    Rect sb = L.save;
    fg = TINY_GRAY;
    if (cache_valid(a)) fg = tiny_button(r, sb.x, sb.y, sb.w, sb.h, u, btn_state(a, HIT_SAVE, sb, now < a->confirm_save_until));
    else disabled_button(r, sb, u);
    tiny_set_color(r, fg);
    tiny_text_centered(&a->font, "Save", sb.x + sb.w / 2, sb.y + (sb.h - tiny_text_height(L.title)) / 2, L.title, TINY_BOLD);

    /* Hints: shortcuts on the left; a voice/language mismatch (allowed, just
       noted) on the right. Kokoro voice names start with their language code. */
    tiny_set_color(r, TINY_DIM);
    int hw = tiny_text(&a->font, "⌘↩ generate/play · ⌘S save · ⌘O open · Esc stop", TINY_MARGIN * u, L.hint_y, L.caption, TINY_PLAIN);
    const char *vl = NULL;
    if (a->voice[0] && strlen(a->lang) == 1 && a->voice[1] && a->voice[0] != a->lang[0]) {
        char code[2] = {a->voice[0], 0};
        vl = lang_name(a, code);
    }
    if (vl && ln) {
        snprintf(line, sizeof line, "%s voice reading %s", vl, ln);
        int w = tiny_text_width(line, L.caption, TINY_PLAIN);
        int x = SDL_max(TINY_MARGIN * u + hw + 2 * TINY_PAD * u, L.w - TINY_MARGIN * u - w);
        text_fit(a, line, x, L.hint_y, L.caption, TINY_PLAIN, L.w - TINY_MARGIN * u - x);
    }

    draw_popup(a, &L);
    if (a->shot_to) {
        SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
        if (!s || !SDL_SaveBMP(s, a->shot_to)) fprintf(stderr, "shot: %s\n", SDL_GetError());
        SDL_DestroySurface(s);
        a->shot_to = NULL;
    }
    tiny_present(r);
}

/* ------------------------------------------------------------------ */
/* Events. */

static int next_timeout(App *a) {
    uint64_t now = SDL_GetTicksNS(), due = 0;
    if (playing(a)) return 33; /* the bar follows playback */
    if (now < a->flash_play_until) due = a->flash_play_until;
    if (now < a->confirm_save_until && (!due || a->confirm_save_until < due)) due = a->confirm_save_until;
    if (!due) return -1;
    return (int)((due - now) / SDL_NS_PER_MS) + 1;
}

static void tick_playback(App *a) {
    if (!a->stream || SDL_GetAudioStreamQueued(a->stream) > 0) return;
    /* Let the device play its last buffer before closing it. */
    uint64_t now = SDL_GetTicksNS();
    if (!a->play_end_at) a->play_end_at = now + 150 * SDL_NS_PER_MS;
    else if (now >= a->play_end_at) stop_playback(a);
}

static void toggle_popup(App *a, int which, bool enabled) {
    a->popup = a->popup == which || !enabled ? POP_NONE : which;
    a->hover_item = -1;
}

static void choose(App *a, int i) {
    if (a->popup == POP_MODEL) {
        if (i != a->model) {
            a->model = i;
            request_info(a);
        }
    } else if (a->popup == POP_VOICE) {
        snprintf(a->voice, sizeof a->voice, "%s", a->voices[i]);
    } else if (a->popup == POP_LANG) {
        snprintf(a->lang, sizeof a->lang, "%s", a->langs[i].code);
    }
    a->popup = POP_NONE;
    save_prefs(a);
}

static void move_cursor(App *a, size_t pos, bool extend, bool keep_col) {
    a->cur = pos;
    if (!extend) a->anchor = pos;
    if (!keep_col) a->want_col = col_of(a, pos);
    a->last_edit = EDIT_OTHER;
}

/* Returns false to quit. */
static bool handle_key(App *a, const SDL_KeyboardEvent *k) {
    bool cmd = k->mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL), shift = k->mod & SDL_KMOD_SHIFT, alt = k->mod & SDL_KMOD_ALT;
    Layout L = layout(a);
    rewrap(a, text_cols(&L));
    if (cmd) {
        switch (k->key) {
        case SDLK_Q: case SDLK_W: return false;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (!k->repeat) {
                a->popup = POP_NONE;
                a->flash_play_until = SDL_GetTicksNS() + FLASH_NS;
                main_action(a);
            }
            break;
        case SDLK_S: if (!k->repeat) save_action(a); break;
        case SDLK_O: if (!k->repeat) open_dialog(a); break;
        case SDLK_A: a->anchor = 0, a->cur = a->len; break;
        case SDLK_C: copy_selection(a); break;
        case SDLK_X:
            copy_selection(a);
            if (sel_lo(a) != sel_hi(a)) insert(a, "", EDIT_OTHER);
            break;
        case SDLK_V: {
            char *clip = SDL_GetClipboardText();
            if (clip && *clip) {
                for (char *p = clip; *p; p++)
                    if (*p == '\r') *p = '\n';
                insert(a, clip, EDIT_OTHER);
            }
            SDL_free(clip);
            break;
        }
        case SDLK_Z: do_undo(a); break;
        case SDLK_BACKSPACE:
            if (sel_lo(a) == sel_hi(a)) a->anchor = a->starts[line_of(a, a->cur)];
            insert(a, "", EDIT_OTHER);
            break;
        case SDLK_LEFT: move_cursor(a, a->starts[line_of(a, a->cur)], shift, false); break;
        case SDLK_RIGHT: move_cursor(a, line_end(a, line_of(a, a->cur)), shift, false); break;
        case SDLK_UP: move_cursor(a, 0, shift, false); break;
        case SDLK_DOWN: move_cursor(a, a->len, shift, false); break;
        default: break;
        }
        return true;
    }
    switch (k->key) {
    case SDLK_ESCAPE:
        if (a->popup != POP_NONE) a->popup = POP_NONE;
        else if (playing(a)) stop_playback(a), set_status(a, false, "Stopped");
        else a->anchor = a->cur;
        break;
    case SDLK_RETURN: case SDLK_KP_ENTER:
        if (a->popup == POP_NONE) insert(a, "\n", EDIT_TYPE);
        break;
    case SDLK_BACKSPACE:
        if (sel_lo(a) == sel_hi(a)) {
            if (!a->cur) break;
            a->anchor = alt ? word_left(a, a->cur) : sp_prev(a->text, a->cur);
        }
        insert(a, "", EDIT_DELETE);
        break;
    case SDLK_DELETE:
        if (sel_lo(a) == sel_hi(a)) {
            if (a->cur >= a->len) break;
            a->anchor = alt ? word_right(a, a->cur) : sp_next(a->text, a->cur);
        }
        insert(a, "", EDIT_DELETE);
        break;
    case SDLK_LEFT:
        if (!shift && sel_lo(a) != sel_hi(a)) move_cursor(a, sel_lo(a), false, false);
        else move_cursor(a, alt ? word_left(a, a->cur) : sp_prev(a->text, a->cur), shift, false);
        break;
    case SDLK_RIGHT:
        if (!shift && sel_lo(a) != sel_hi(a)) move_cursor(a, sel_hi(a), false, false);
        else move_cursor(a, alt ? word_right(a, a->cur) : sp_next(a->text, a->cur), shift, false);
        break;
    case SDLK_UP: {
        int ln = line_of(a, a->cur);
        move_cursor(a, ln ? pos_at(a, ln - 1, a->want_col) : 0, shift, true);
        break;
    }
    case SDLK_DOWN: {
        int ln = line_of(a, a->cur);
        move_cursor(a, ln + 1 < a->nstarts ? pos_at(a, ln + 1, a->want_col) : a->len, shift, true);
        break;
    }
    case SDLK_HOME: move_cursor(a, a->starts[line_of(a, a->cur)], shift, false); break;
    case SDLK_END: move_cursor(a, line_end(a, line_of(a, a->cur)), shift, false); break;
    default: break;
    }
    return true;
}

static void track_mouse(App *a, float x, float y) {
    float d = tiny_pixel_density(a->win, a->ren);
    a->mx = (int)(x * d);
    a->my = (int)(y * d);
}

/* Returns false to quit. */
static bool handle_event(App *a, const SDL_Event *e) {
    if (e->type == proc_event) {
        drain(a, e->user.code);
        return true;
    }
    if (e->type == open_event) {
        open_file(a, e->user.data1);
        SDL_free(e->user.data1);
        return true;
    }
    switch (e->type) {
    case SDL_EVENT_QUIT:
        return false;
    case SDL_EVENT_KEY_DOWN:
        return handle_key(a, &e->key);
    case SDL_EVENT_DROP_FILE:
        a->popup = POP_NONE;
        open_file(a, e->drop.data);
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (a->popup == POP_NONE && !(SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
            const char *t = e->text.text;
            bool printable = *t != 0;
            for (const char *p = t; *p; p++)
                if ((unsigned char)*p < 0x20) printable = false;
            /* Words coalesce into one undo step; a space starts the next. */
            if (printable) insert(a, t, strchr(t, ' ') ? EDIT_WORD : EDIT_TYPE);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        track_mouse(a, e->motion.x, e->motion.y);
        Layout L = layout(a);
        if (a->popup != POP_NONE) a->hover_item = popup_hit(a, &L, a->mx, a->my);
        if (a->dragging) a->cur = text_pos_at(a, &L, a->mx, a->my);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        track_mouse(a, e->button.x, e->button.y);
        Layout L = layout(a);
        int h = hit(a, &L, a->mx, a->my);
        if (a->popup != POP_NONE && h != HIT_POPUP) {
            /* Any click outside closes the menu; clicking its own selector
               shouldn't immediately reopen it. */
            int was = a->popup;
            a->popup = POP_NONE;
            if (h == HIT_SEL_MODEL + was - POP_MODEL) break;
        }
        if (h == HIT_POPUP) {
            int i = popup_hit(a, &L, a->mx, a->my);
            if (i >= 0) choose(a, i);
        } else if (h == HIT_SEL_MODEL) {
            toggle_popup(a, POP_MODEL, a->models_ready);
        } else if (h == HIT_SEL_VOICE) {
            toggle_popup(a, POP_VOICE, a->nvoices > 0);
        } else if (h == HIT_SEL_LANG) {
            toggle_popup(a, POP_LANG, a->nlangs > 0);
        } else if (h == HIT_SEL_FILE) {
            a->pressed = h;
        } else if (h == HIT_TEXT) {
            size_t pos = text_pos_at(a, &L, a->mx, a->my);
            if (e->button.clicks == 2) {
                a->anchor = word_left(a, pos < a->len && !is_space(a->text[pos]) ? sp_next(a->text, pos) : pos);
                a->cur = word_right(a, a->anchor);
            } else if (e->button.clicks >= 3) {
                a->anchor = 0, a->cur = a->len;
            } else {
                move_cursor(a, pos, SDL_GetModState() & SDL_KMOD_SHIFT, false);
                a->dragging = true;
            }
        } else if (h == HIT_PLAY || h == HIT_SAVE) {
            a->pressed = h;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        track_mouse(a, e->button.x, e->button.y);
        Layout L = layout(a);
        if (a->dragging) {
            a->dragging = false;
            rewrap(a, text_cols(&L));
            a->want_col = col_of(a, a->cur);
        }
        int pressed = a->pressed;
        a->pressed = HIT_NONE;
        if (pressed && hit(a, &L, a->mx, a->my) == pressed) {
            if (pressed == HIT_PLAY) main_action(a);
            else if (pressed == HIT_SAVE) save_action(a);
            else open_dialog(a);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        Layout L = layout(a);
        if (!in_rect(L.text, a->mx, a->my)) break;
        rewrap(a, text_cols(&L));
        int rows = text_rows(&L), step = e->wheel.integer_y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) step = -step;
        a->scroll = SDL_clamp(a->scroll - step, 0, SDL_max(0, a->nstarts - rows));
        /* Keep the cursor in view so the redraw doesn't scroll back to it. */
        int k = line_of(a, a->cur);
        if (k < a->scroll) a->cur = a->anchor = a->starts[a->scroll];
        else if (k >= a->scroll + rows) a->cur = a->anchor = a->starts[a->scroll + rows - 1];
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        a->popup = POP_NONE;
        a->pressed = HIT_NONE;
        a->dragging = false;
        break;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* SPEAK_DRIVE=<script>: drives the UI for automated checks by pushing mouse
   and keyboard events at the widgets' real positions, through the normal
   event path. One command per line:
     click model|voice|lang|text|play|save    pick <popup item label>
     type <text>    key <enter|backspace|escape|a|s|z|left|right> [cmd] [shift]
     drop <file>    (as if dropped on the window)
     sleep <ms>     until <status text>       idle      shot <file.bmp>
     expect <status text>                     quit
   `until`/`idle` time out after 3 minutes. Failures print FAIL and exit 2. */

typedef struct {
    FILE *f;
    uint64_t wake_at, deadline;
    char until[256];
    bool idle, done;
    int failures;
} Drive;

static void push_mouse(App *a, Rect r) {
    float d = tiny_pixel_density(a->win, a->ren);
    float x = (r.x + r.w / 2) / d, y = (r.y + r.h / 2) / d;
    SDL_WindowID id = SDL_GetWindowID(a->win);
    SDL_Event e = {0};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = id, e.motion.x = x, e.motion.y = y;
    SDL_PushEvent(&e);
    for (int down = 1; down >= 0; down--) {
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = id, e.button.button = SDL_BUTTON_LEFT, e.button.down = down;
        e.button.clicks = 1, e.button.x = x, e.button.y = y;
        SDL_PushEvent(&e);
    }
}

static SDL_Keycode key_named(const char *s) {
    static const struct { const char *name; SDL_Keycode key; } keys[] = {
        {"enter", SDLK_RETURN}, {"backspace", SDLK_BACKSPACE}, {"escape", SDLK_ESCAPE}, {"left", SDLK_LEFT},
        {"right", SDLK_RIGHT}, {"a", SDLK_A}, {"s", SDLK_S}, {"z", SDLK_Z},
    };
    for (size_t i = 0; i < SDL_arraysize(keys); i++)
        if (!strcmp(keys[i].name, s)) return keys[i].key;
    return SDLK_UNKNOWN;
}

static bool drive_busy(App *a) {
    return a->generating || playing(a) || a->info_pending || proc_running(&a->dl) || !a->models_ready;
}

/* Runs script commands until one has to wait. Returns false at the end. */
static bool drive_step(App *a, Drive *d) {
    uint64_t now = SDL_GetTicksNS();
    if (now < d->wake_at) return true;
    if (d->until[0] || d->idle) {
        bool met = d->idle ? !drive_busy(a) : strstr(a->shown, d->until) != NULL;
        bool failed = !d->idle && a->status_err && !drive_busy(a); /* an error ends the wait */
        if (!met && !failed && now < d->deadline) return true;
        if (!met) {
            printf("FAIL %s waiting for %s (status: %s)\n", failed ? "error" : "timed out", d->idle ? "idle" : d->until,
                   a->shown);
            d->failures++;
        }
        d->until[0] = 0;
        d->idle = false;
    }
    char line[1024];
    for (;;) {
        if (!fgets(line, sizeof line, d->f)) {
            d->done = true;
            return false;
        }
        line[strcspn(line, "\r\n")] = 0;
        char *arg = strchr(line, ' ');
        if (arg) *arg++ = 0;
        else arg = line + strlen(line);
        Layout L = layout(a);
        if (!line[0] || line[0] == '#') continue;
        printf("> %s %s\n", line, arg);
        fflush(stdout);
        if (!strcmp(line, "click")) {
            static const char *names[] = {"model", "voice", "lang", "text", "play", "save"};
            Rect rects[] = {L.sel[0], L.sel[1], L.sel[2], L.text, L.play, L.save};
            for (int i = 0; i < 6; i++)
                if (!strcmp(arg, names[i])) push_mouse(a, rects[i]);
            d->wake_at = now + 100 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "pick")) {
            Popup p = popup_layout(a, &L);
            char buf[200];
            int found = -1;
            for (int i = 0; i < p.n && found < 0; i++)
                if (!strcmp(popup_item(a, i, buf, sizeof buf), arg)) found = i;
            if (found < 0) {
                printf("FAIL no popup item \"%s\"\n", arg);
                d->failures++;
                continue;
            }
            Rect r = {p.panel.x + (found / p.rows) * p.col_w, p.panel.y + L.u + (found % p.rows) * p.item_h, p.col_w, p.item_h};
            push_mouse(a, r);
            d->wake_at = now + 100 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "type")) {
            for (const char *s = arg; *s;) {
                size_t n = sp_next(s, 0);
                char *cp = SDL_strndup(s, n); /* events keep the pointer; leaked on purpose */
                SDL_Event e = {0};
                e.type = SDL_EVENT_TEXT_INPUT;
                e.text.windowID = SDL_GetWindowID(a->win);
                e.text.text = cp;
                SDL_PushEvent(&e);
                s += n;
            }
            d->wake_at = now + 50 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "key")) {
            char name[32] = "";
            sscanf(arg, "%31s", name);
            SDL_Event e = {0};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.windowID = SDL_GetWindowID(a->win);
            e.key.key = key_named(name);
            e.key.down = true;
            if (strstr(arg, " cmd")) e.key.mod |= SDL_KMOD_LGUI;
            if (strstr(arg, " shift")) e.key.mod |= SDL_KMOD_LSHIFT;
            SDL_PushEvent(&e);
            d->wake_at = now + 50 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "drop")) {
            static char path[2048];
            snprintf(path, sizeof path, "%s", arg);
            SDL_Event e = {0};
            e.type = SDL_EVENT_DROP_FILE;
            e.drop.windowID = SDL_GetWindowID(a->win);
            e.drop.data = path;
            SDL_PushEvent(&e);
            d->wake_at = now + 100 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "sleep")) {
            d->wake_at = now + (uint64_t)atoi(arg) * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "until") || !strcmp(line, "idle")) {
            snprintf(d->until, sizeof d->until, "%s", arg);
            d->idle = !strcmp(line, "idle");
            d->deadline = now + 180 * SDL_NS_PER_SECOND;
            return true;
        } else if (!strcmp(line, "shot")) {
            static char path[1024];
            snprintf(path, sizeof path, "%s", arg);
            a->shot_to = path;
            d->wake_at = now + 50 * SDL_NS_PER_MS;
            return true;
        } else if (!strcmp(line, "expect")) {
            bool ok = strstr(a->shown, arg) != NULL;
            printf("%s expect \"%s\" (status: %s)\n", ok ? "PASS" : "FAIL", arg, a->shown);
            if (!ok) d->failures++;
        } else if (!strcmp(line, "quit")) {
            d->done = true;
            return false;
        } else {
            printf("FAIL unknown command %s\n", line);
            d->failures++;
        }
    }
}

static bool make_tmpdir(App *a) {
    const char *t = getenv("TMPDIR");
    snprintf(a->tmpdir, sizeof a->tmpdir, "%s/speak-%d", t && *t ? t : "/tmp", (int)getpid());
    char *dbl; /* $TMPDIR usually ends in '/' */
    while ((dbl = strstr(a->tmpdir, "//"))) memmove(dbl, dbl + 1, strlen(dbl));
    return SDL_CreateDirectory(a->tmpdir);
}

int main(int argc, char **argv) {
    App app = {0};
    App *a = &app;
    a->model = -1;
    a->hover_item = -1;
    a->python = getenv("SPEAK_PYTHON");
    if (!a->python || !*a->python) a->python = SPEAK_PYTHON_DEFAULT;
    a->worker_path = getenv("SPEAK_WORKER");
    if (!a->worker_path || !*a->worker_path) a->worker_path = SPEAK_WORKER;
    const char *initial = getenv("SPEAK_TEXT"); /* handy for screenshots */
    set_text(a, initial ? initial : "");
    a->cur = a->anchor = a->len;
    set_status(a, false, "Starting the MLX audio worker…");

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    proc_event = SDL_RegisterEvents(2);
    open_event = proc_event + 1;
    if (!make_tmpdir(a)) fprintf(stderr, "speak: can't create %s: %s\n", a->tmpdir, SDL_GetError());
    char *pref = SDL_GetPrefPath("tinyapps", "speak");
    if (getenv("SPEAK_DRIVE")) { /* scripted runs leave the real prefs alone */
        SDL_free(pref);
        pref = NULL;
        snprintf(a->prefs, sizeof a->prefs, "%s/prefs.txt", a->tmpdir);
    }
    if (pref) {
        snprintf(a->prefs, sizeof a->prefs, "%sprefs.txt", pref);
        SDL_free(pref);
    }
    if (!tiny_create_window("Speak", 720, 520, 520, 400, &a->win, &a->ren) || !tiny_font_init(&a->font, a->ren)) {
        SDL_Quit();
        return 1;
    }
    SDL_StartTextInput(a->win);
    if (argc > 1) open_file(a, argv[1]); /* speak notes.md */

    /* Screenshot runs draw one frame and quit, so they skip the worker. */
    const char *shot = getenv("TINY_SCREENSHOT");
    if (!(shot && *shot) && ensure_worker(a)) proc_send(&a->worker, "models\n");

    /* Event-driven: worker output arrives as events; playback and button
       flashes set wake-up deadlines. */
    Drive drive = {0};
    const char *script = getenv("SPEAK_DRIVE");
    if (script && *script && !(drive.f = fopen(script, "r"))) fprintf(stderr, "speak: can't open %s\n", script);

    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        if (drive.f && !drive_step(a, &drive)) break;
        int timeout = drive.f ? 30 : next_timeout(a);
        bool got = timeout < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, timeout);
        if (got) {
            running = handle_event(a, &e);
            while (running && SDL_PollEvent(&e)) running = handle_event(a, &e);
        }
        tick_playback(a);
        if (running) render(a);
    }

    stop_playback(a);
    proc_stop(&a->worker);
    proc_stop(&a->dl);
    if (a->cache_path[0]) remove(a->cache_path);
    SDL_RemovePath(a->tmpdir);
    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    if (drive.f) {
        fclose(drive.f);
        if (!drive.done) {
            printf("FAIL the app quit before the script finished\n");
            drive.failures++;
        }
        printf("%s (%d failure%s)\n", drive.failures ? "FAIL" : "PASS", drive.failures, drive.failures == 1 ? "" : "s");
        return drive.failures ? 2 : 0;
    }
    return 0;
}
