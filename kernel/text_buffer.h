#ifndef TEXT_BUFFER_H
#define TEXT_BUFFER_H
#include <stdint.h>
#define TEXT_COLS 200U
#define TEXT_ROWS 100U
#define TEXT_HISTORY 512U
enum { TEXT_CELL = 1, TEXT_CLEAR = 2 };
struct text_buffer {
    char lines[TEXT_HISTORY][TEXT_COLS];
    uint64_t first, last, line, view;
    unsigned cols, rows, column, escape, csi_length;
    char csi_first;
    int follow;
};
void text_init(struct text_buffer *b, unsigned cols, unsigned rows);
unsigned text_write(struct text_buffer *b, unsigned char ch);
char text_at(const struct text_buffer *b, uint64_t line, unsigned column);
uint64_t text_bottom(const struct text_buffer *b);
void text_scroll(struct text_buffer *b, int lines);
void text_follow(struct text_buffer *b);
void text_oldest(struct text_buffer *b);
#endif
