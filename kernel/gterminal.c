#include "graphics.h"
#include "serial.h"
#include "idt.h"

extern volatile struct limine_framebuffer_request framebuffer_request;
static struct gfx_surface display;
#define MAX_COLS 200
#define MAX_ROWS 100
#define CW 12U
#define CH 18U
#define BG 0x101b2bU
#define FG 0xdce8f5U
static char cells[MAX_ROWS][MAX_COLS];
static unsigned cols, rows, column, row, origin_x, origin_y;
static unsigned escape, csi_length;
static char csi_first;
static int active;

static void label(unsigned x, unsigned y, const char *text, uint32_t color) {
    while (*text) {
        gfx_glyph(&display, x, y, *text++, color, 0x1e3047, 2);
        x += CW;
    }
}
static void render(unsigned x, unsigned y) {
    gfx_glyph(&display, origin_x + x * CW, origin_y + y * CH,
              cells[y][x], FG, BG, 2);
}
static void cursor(void) {
    gfx_rect(&display, origin_x + column * CW, origin_y + row * CH + CH - 2, CW - 2, 2, 0x67d8b4);
}
static void clear_text(void) {
    for (unsigned y = 0; y < rows; y++)
        for (unsigned x = 0; x < cols; x++) cells[y][x] = ' ';
    gfx_rect(&display, origin_x, origin_y, cols * CW, rows * CH, BG);
    column = row = 0;
}
static void newline(void) {
    column = 0;
    if (++row < rows) return;
    row = rows - 1;
    for (unsigned y = 0; y + 1 < rows; y++)
        for (unsigned x = 0; x < cols; x++) cells[y][x] = cells[y + 1][x];
    for (unsigned x = 0; x < cols; x++) cells[rows - 1][x] = ' ';
    for (unsigned y = 0; y < rows; y++)
        for (unsigned x = 0; x < cols; x++) render(x, y);
}
void gterminal_init(void) {
    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (!response || !response->framebuffer_count || !response->framebuffers ||
        gfx_surface_init(&display, response->framebuffers[0]) ||
        display.width < 640 || display.height < 400) {
        serial_write("[GFX] Framebuffer unavailable/unsupported (requires packed RGB 24/32-bit, >=640x400).\r\n");
        return;
    }
    serial_write("[GFX] Limine framebuffer validated: ");
    serial_write_dec(display.width); serial_write("x"); serial_write_dec(display.height);
    serial_write(" pitch="); serial_write_dec(display.pitch);
    serial_write(" bytes-per-pixel="); serial_write_dec(display.bytes_per_pixel); serial_write("\r\n");
    cols = (display.width - 64) / CW;
    rows = (display.height - 144) / CH;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    origin_x = (display.width - cols * CW) / 2;
    origin_y = 100;
    gfx_clear(&display, 0x0a1220);
    gfx_rect(&display, 0, 0, display.width, 40, 0x1e3047);
    label(24, 12, "EDUCATIONAL OS", 0x67d8b4);
    gfx_rect(&display, origin_x - 10, 58, cols * CW + 20, 32, 0x1e3047);
    label(origin_x, 65, "Terminal / shell", FG);
    gfx_rect(&display, origin_x - 10, 90, cols * CW + 20, rows * CH + 20, BG);
    gfx_rect(&display, 0, display.height - 30, display.width, 30, 0x1e3047);
    label(24, display.height - 23, "Keyboard + serial | help | clear", 0xa7bbcf);
    clear_text(); cursor();
    active = 1;
    serial_write("[GFX] Graphical terminal ready; keyboard focus: shell.\r\n");
}
void gterminal_putchar(char c) {
    if (!active) return;
    uint64_t flags = interrupts_save_and_disable();
    render(column, row); /* Remove old cursor without destroying its character. */
    if (escape == 1) {
        escape = c == '[' ? 2 : 0;
        csi_length = 0; csi_first = 0;
    } else if (escape == 2) {
        if (!csi_length) csi_first = c;
        csi_length++;
        if (c >= 0x40 && c <= 0x7e) {
            if (c == 'J' && csi_first == '2' && csi_length == 2) clear_text();
            else if (c == 'H' && csi_length == 1) column = row = 0;
            escape = 0;
        } else if (csi_length > 16) escape = 0;
    } else if (c == 27) escape = 1;
    else if (c == '\r') column = 0;
    else if (c == '\n') newline();
    else if (c == '\b') {
        if (column) column--;
        else if (row) { row--; column = cols - 1; }
    } else if (c == '\t') {
        unsigned spaces = 4 - column % 4;
        while (spaces--) {
            cells[row][column] = ' '; render(column, row);
            if (++column == cols) newline();
        }
    } else if ((unsigned char)c >= 32) {
        cells[row][column] = c; render(column, row);
        if (++column == cols) newline();
    }
    cursor();
    interrupts_restore(flags);
}
