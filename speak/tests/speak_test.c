#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "speak.h"

static void check_prefix(const char *text, const char *want) {
    char got[64];
    sp_prefix(text, got, sizeof got);
    if (strcmp(got, want)) fprintf(stderr, "prefix(\"%s\") = \"%s\", want \"%s\"\n", text, got, want);
    CHECK(!strcmp(got, want));
}

static void test_prefix(void) {
    check_prefix("Hello there. This is Paris calling.", "paris");
    check_prefix("Paris is lovely in spring.", "paris");
    check_prefix("I met Alice at the station.", "alice");
    check_prefix("The weather is nice.", "weather");
    check_prefix("Dr. Smith will see you now.", "smith");
    check_prefix("we flew to NASA's base", "nasas");
    check_prefix("O'Brien said hi", "obrien");
    check_prefix("", "speech");
    check_prefix("I am. You are.", "speech");
    check_prefix("ok, on y va", "va");
    check_prefix("\xe4\xbd\xa0\xe5\xa5\xbd", "speech"); /* non-Latin: no usable letters */
}

static bool taken(const char *path, void *ud) {
    const char *const *list = ud;
    for (; *list; list++)
        if (!strcmp(*list, path)) return true;
    return false;
}

static void test_save_path(void) {
    const char *none[] = {NULL};
    const char *some[] = {"/d/paris_001.wav", "/d/paris_002.wav", "/d/paris_004.wav", NULL};
    char out[64];
    CHECK(sp_save_path("/d", "paris", taken, (void *)none, out, sizeof out));
    CHECK(!strcmp(out, "/d/paris_001.wav"));
    CHECK(sp_save_path("/d", "paris", taken, (void *)some, out, sizeof out));
    CHECK(!strcmp(out, "/d/paris_003.wav"));
    CHECK(!sp_save_path("/a/very/long/dir", "paris", taken, (void *)none, out, 12));
}

static void test_utf8(void) {
    const char *s = "a\xc3\xa9z"; /* aéz */
    CHECK(sp_next(s, 0) == 1);
    CHECK(sp_next(s, 1) == 3);
    CHECK(sp_prev(s, 3) == 1);
    CHECK(sp_prev(s, 1) == 0);
    CHECK(sp_prev(s, 0) == 0);
    CHECK(sp_next(s, 4) == 4);
    CHECK(sp_cells(s, 0, 4) == 3);
    CHECK(sp_advance(s, 0, 4, 2) == 3);
    CHECK(sp_advance("ab\ncd", 0, 5, 9) == 2);
}

static void test_wrap(void) {
    size_t st[16];
    CHECK(sp_wrap("", 10, st, 16) == 1 && st[0] == 0);
    CHECK(sp_wrap("hello world", 20, st, 16) == 1);
    /* "hello " fits in 6; "world" goes to the next line. */
    CHECK(sp_wrap("hello world", 8, st, 16) == 2 && st[1] == 6);
    CHECK(sp_line_end("hello world", st, 2, 0) == 6);
    /* A space exactly at the edge hangs off the line. */
    CHECK(sp_wrap("abcde fgh", 5, st, 16) == 2 && st[1] == 6);
    /* Long words break. */
    CHECK(sp_wrap("abcdefghij", 4, st, 16) == 3 && st[1] == 4 && st[2] == 8);
    /* Newlines, including a trailing one, start lines. */
    const char *nl = "ab\n\ncd\n";
    CHECK(sp_wrap(nl, 10, st, 16) == 4 && st[1] == 3 && st[2] == 4 && st[3] == 7);
    CHECK(sp_line_end(nl, st, 4, 0) == 2);
    CHECK(sp_line_end(nl, st, 4, 1) == 3);
    CHECK(sp_line_end(nl, st, 4, 3) == 7);
    /* Multi-byte code points count as one cell. */
    CHECK(sp_wrap("\xc3\xa9\xc3\xa9\xc3\xa9 ab", 4, st, 16) == 2 && st[1] == 7);
    /* More lines than room: the count is still right. */
    CHECK(sp_wrap("a\nb\nc\nd", 4, st, 2) == 4);
}

static void test_protocol(void) {
    char *e = sp_escape("a\tb\nc\\d\r");
    CHECK(!strcmp(e, "a\\tb\\nc\\\\d"));
    free(e);
    char line[] = "progress\t7\t0.5\tSpeaking\n";
    char *f[8];
    CHECK(sp_fields(line, f, 8) == 4);
    CHECK(!strcmp(f[0], "progress") && !strcmp(f[3], "Speaking"));
    char empty[] = "info\tm\t1\t0\t\t";
    CHECK(sp_fields(empty, f, 8) == 6 && !*f[4] && !*f[5]);
    char list[] = "af_heart,am_adam";
    char *it[4];
    CHECK(sp_list(list, it, 4) == 2 && !strcmp(it[1], "am_adam"));
    char none[] = "";
    CHECK(sp_list(none, it, 4) == 0);
    CHECK(sp_hash("a", SP_HASH_INIT) != sp_hash("b", SP_HASH_INIT));
}

static void check_plain(const char *src, bool md, const char *want) {
    char *got = sp_plain(src, strlen(src), md);
    if (strcmp(got, want)) fprintf(stderr, "plain(\"%s\") =\n\"%s\"\nwant\n\"%s\"\n", src, got, want);
    CHECK(!strcmp(got, want));
    free(got);
}

static void test_plain(void) {
    /* Text: CRLF, tabs, typography, hard wraps rejoined, paragraphs kept. */
    check_plain("\xef\xbb\xbfOne line\r\nwrapped here.\r\n\r\nNew\tpara\xe2\x80\x94with \xe2\x80\x9cquotes\xe2\x80\x9d\xe2\x80\xa6\n",
                false, "One line wrapped here.\n\nNew para - with \"quotes\"...");
    /* Poems: capitalized lines stay lines. */
    check_plain("Roses are red\nViolets are blue\n\n\n\nEnd.", false, "Roses are red\nViolets are blue\n\nEnd.");
    /* Markdown. */
    check_plain("---\ntitle: x\n---\n# Title #\nSome *bold* and _it_ text\nwrapped `code` line.\n\n"
                "```c\nint x;\n```\n- [x] item [link](http://a.b) one\n2. two ![pic](p.png)\n\n"
                "> quoted\n\n| a | b |\n|---|:-:|\n| 1 | 2 |\n\n***\n[ref]: http://x\n"
                "snake_case <b>tag</b> <!-- hidden --> 2 * 3 \\*lit\\*\n",
                true,
                "Title\n\nSome bold and it text wrapped code line.\n\nitem link one\ntwo pic\n\nquoted\n\na, b\n1, 2\n\n"
                "snake_case tag 2 * 3 *lit*");
    check_plain("Setext\n======\nBody", true, "Setext\n\nBody");
    check_plain("<!--\nmulti\n-->\nafter", true, "after");
    check_plain("", true, "");
}

static void test_split_path(void) {
    char d[64], s[32];
    CHECK(sp_split_path("/a/b/notes.md", d, sizeof d, s, sizeof s) && !strcmp(d, "/a/b") && !strcmp(s, "notes"));
    CHECK(sp_split_path("my.notes.txt", d, sizeof d, s, sizeof s) && !strcmp(d, ".") && !strcmp(s, "my.notes"));
    CHECK(sp_split_path("/x/.hidden", d, sizeof d, s, sizeof s) && !strcmp(s, ".hidden"));
    CHECK(sp_split_path("/README", d, sizeof d, s, sizeof s) && !strcmp(d, "/") && !strcmp(s, "README"));
    CHECK(!sp_split_path("/a/b/notes.md", d, 3, s, sizeof s));
}

int main(void) {
    test_plain();
    test_split_path();
    test_prefix();
    test_save_path();
    test_utf8();
    test_wrap();
    test_protocol();
    return check_report();
}
