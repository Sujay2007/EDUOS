#include "text_buffer.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static struct text_buffer b;
static void write_text(const char *s) { while (*s) text_write(&b, *s++); }
int main(void) {
    text_init(&b, 8, 3);
    write_text("first\nsecond\nthird\nfourth");
    assert(b.last == 3 && b.view == 1 && text_at(&b, 0, 0) == 'f');
    text_scroll(&b, -2);
    assert(b.view == 0 && !b.follow && text_at(&b, b.view, 0) == 'f');
    write_text("\nfifth\nsixth");
    assert(b.view == 0 && !b.follow && text_at(&b, 1, 0) == 's');
    text_scroll(&b, 1); assert(b.view == 1);
    text_scroll(&b, INT_MAX); assert(b.view == text_bottom(&b) && b.follow);
    text_scroll(&b, INT_MIN); assert(b.view == b.first);
    text_follow(&b); assert(b.view == 3 && b.follow);
    text_oldest(&b); assert(b.view == 0);
    for (unsigned i = 0; i < TEXT_HISTORY + 10; i++) write_text("\nline");
    assert(b.last - b.first + 1 == TEXT_HISTORY && b.view == b.first && !b.follow);
    assert(text_at(&b, b.first - 1, 0) == ' ' && text_at(&b, b.last + 1, 0) == ' ');
    text_follow(&b); assert(b.view == text_bottom(&b));
    text_init(&b, 4, 2);
    write_text("abcdE");
    assert(b.line == 1 && b.column == 1 && text_at(&b, 0, 3) == 'd');
    write_text("\b \b");
    assert(b.column == 0 && text_at(&b, 1, 0) == ' ');
    write_text("\b \b");
    assert(b.line == 0 && b.column == 3 && text_at(&b, 0, 3) == ' ');
    write_text("\033[2J\033[H");
    assert(b.first == 0 && b.last == 0 && b.view == 0 && b.column == 0 && b.follow);
    assert(text_at(&b, 0, 0) == ' ');
    text_init(&b, UINT_MAX, UINT_MAX);
    assert(b.cols == TEXT_COLS && b.rows == TEXT_ROWS);
    text_init(&b, 0, 0); assert(b.cols == 1 && b.rows == 1);
    puts("Terminal buffer PASS: history, anchored viewport, navigation bounds, ring rollover, wrap/backspace, clear");
}
