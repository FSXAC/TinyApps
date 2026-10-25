/*
 * speak core: the SDL-free logic behind the speak app (text layout, the
 * save-file prefix, save-path indexing and the worker line protocol).
 * Text is UTF-8; positions are byte offsets that always sit on a code point
 * boundary, and every code point takes one cell of the monospace font.
 */
#ifndef SPEAK_H
#define SPEAK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Byte offset of the code point after / before the one at `i`. */
size_t sp_next(const char *s, size_t i);
size_t sp_prev(const char *s, size_t i);
/* Number of code points in s[a, b). */
int sp_cells(const char *s, size_t a, size_t b);
/* Byte offset `cells` code points after `a`, stopping at `b` or a newline. */
size_t sp_advance(const char *s, size_t a, size_t b, int cells);

/* Word wraps `s` into lines of at most `cols` cells. Writes up to `max` line
   start offsets to `starts` and returns the line count (which may exceed max).
   A line ends at a '\n' (not included) or after the last space that fits;
   words longer than a line are broken. There is always at least one line. */
int sp_wrap(const char *s, int cols, size_t *starts, int max);
/* End of line `k` (exclusive, before any '\n' or the wrap point's trailing
   text), given the starts from sp_wrap and the line count. */
size_t sp_line_end(const char *s, const size_t *starts, int n, int k);

/* Save-file prefix: the first proper noun of `s`, lower-cased and reduced to
   [a-z0-9]. Prefers a capitalized word that doesn't start a sentence, then a
   sentence-initial capitalized word that isn't a common function word, then
   the first word that isn't one; "speech" when nothing qualifies. */
void sp_prefix(const char *s, char *out, size_t n);

/* Writes "<dir>/<prefix>_NNN.wav" for the lowest NNN >= 1 that `exists`
   reports free. Returns false if out is too small or 999 are taken. */
bool sp_save_path(const char *dir, const char *prefix, bool (*exists)(const char *path, void *ud),
                  void *ud, char *out, size_t n);

/* Readable text from a .txt or .md file's bytes, as it should be spoken and
   shown. Both: drops a BOM and control characters, turns CRLF, tabs and
   typographic punctuation (curly quotes, dashes, ellipsis, nbsp) into ASCII,
   and rejoins hard-wrapped lines (a line that doesn't end a sentence followed
   by one starting in lower case). Markdown also loses front matter, code
   blocks, rules, link definitions, HTML, heading/list/quote markers and
   emphasis; links and images keep their text; table rows become "a, b";
   lines within a paragraph are joined. Paragraphs stay separated by a blank
   line. Returns a malloc'd string. */
char *sp_plain(const char *src, size_t n, bool markdown);

/* Splits `path` into its directory ("." if none) and its file name without
   the last extension ("notes.md" -> "notes"). Returns false if either
   doesn't fit. */
bool sp_split_path(const char *path, char *dir, size_t dn, char *stem, size_t sn);

/* Escapes '\\', '\n' and '\t' for the worker protocol. Returns a malloc'd string. */
char *sp_escape(const char *s);
/* Splits a protocol line on tabs in place. Returns the field count (<= max). */
int sp_fields(char *line, char **fields, int max);
/* Splits a comma separated list in place. Returns the count (<= max). */
int sp_list(char *s, char **items, int max);

/* FNV-1a, used to name temp files after the request they hold. */
uint64_t sp_hash(const char *s, uint64_t h);
#define SP_HASH_INIT 1469598103934665603ULL

#endif
