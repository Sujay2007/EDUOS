#include "text_buffer.h"

static void blank(struct text_buffer *b, uint64_t line) {
    for (unsigned i = 0; i < b->cols; i++) b->lines[line % TEXT_HISTORY][i] = ' ';
}
uint64_t text_bottom(const struct text_buffer *b) {
    uint64_t top = b->last + 1 > b->rows ? b->last + 1 - b->rows : 0;
    return top < b->first ? b->first : top;
}
void text_init(struct text_buffer *b, unsigned cols, unsigned rows) {
    b->cols = !cols ? 1 : cols > TEXT_COLS ? TEXT_COLS : cols;
    b->rows = !rows ? 1 : rows > TEXT_ROWS ? TEXT_ROWS : rows;
    b->first = b->last = b->line = b->view = 0;
    b->column = b->escape = b->csi_length = 0;
    b->csi_first = 0; b->follow = 1;
    blank(b, 0);
}
char text_at(const struct text_buffer *b, uint64_t line, unsigned col) {
    if (line < b->first || line > b->last || col >= b->cols) return ' ';
    return b->lines[line % TEXT_HISTORY][col];
}
void text_follow(struct text_buffer *b) { b->follow = 1; b->view = text_bottom(b); }
void text_oldest(struct text_buffer *b) { b->view = b->first; b->follow = b->view == text_bottom(b); }
void text_scroll(struct text_buffer *b, int lines) {
    uint64_t bottom = text_bottom(b);
    if (lines < 0) {
        uint64_t n = (uint64_t)(-(int64_t)lines);
        b->view = n > b->view - b->first ? b->first : b->view - n;
    } else {
        uint64_t n = (unsigned)lines;
        b->view = n > bottom - b->view ? bottom : b->view + n;
    }
    b->follow = b->view == bottom;
}
static void next_line(struct text_buffer *b) {
    b->column = 0;
    if (++b->line > b->last) {
        b->last = b->line;
        if (b->last - b->first >= TEXT_HISTORY) b->first = b->last - TEXT_HISTORY + 1;
        blank(b, b->line);
    }
    if (b->follow) b->view = text_bottom(b);
    else if (b->view < b->first) b->view = b->first;
}
unsigned text_write(struct text_buffer *b, unsigned char c) {
    if (b->escape == 1) {
        b->escape = c == '[' ? 2 : 0;
        b->csi_length = 0; b->csi_first = 0;
        return 0;
    }
    if (b->escape == 2) {
        if (!b->csi_length) b->csi_first = c;
        b->csi_length++;
        if (c >= 0x40 && c <= 0x7e) {
            int clear = c == 'J' && b->csi_first == '2' && b->csi_length == 2;
            if (clear) text_init(b, b->cols, b->rows);
            else if (c == 'H' && b->csi_length == 1) { b->line = b->view; b->column = 0; }
            b->escape = 0;
            return clear ? TEXT_CLEAR : 0;
        }
        if (b->csi_length > 16) b->escape = 0;
        return 0;
    }
    if (c == 27) { b->escape = 1; return 0; }
    if (c == '\r') { b->column = 0; return 0; }
    if (c == '\n') { next_line(b); return 0; }
    if (c == '\b') {
        if (b->column) b->column--;
        else if (b->line > b->first) { b->line--; b->column = b->cols - 1; }
        return 0;
    }
    if (c < 32) return 0;
    b->lines[b->line % TEXT_HISTORY][b->column] = c;
    if (++b->column == b->cols) next_line(b);
    return TEXT_CELL;
}
