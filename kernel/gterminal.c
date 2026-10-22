#include "graphics.h"
#include "serial.h"
#include "idt.h"
#include "live.h"
#include "text_buffer.h"
#include "education.h"

extern volatile struct limine_framebuffer_request framebuffer_request;
static struct gfx_surface display;
#define MAX_COLS 200
#define MAX_ROWS 100
#define CW 12U
#define CH 18U
#define BG 0x101b2bU
#define FG 0xdce8f5U
static struct text_buffer history;
static unsigned cols, rows, origin_x, origin_y;
static int active;
static int tools_visible = 1;
static uint64_t previous_behind = UINT64_MAX;
static int previous_follow = -1;
#define OUTPUT_CAPACITY 32768U
static uint16_t output[OUTPUT_CAPACITY];
static unsigned output_head, output_tail;
static uint64_t output_drops;
#define CANVAS_QUEUE_SIZE 16U
static struct app_canvas_rect canvas_queue[CANVAS_QUEUE_SIZE];
static unsigned canvas_head, canvas_tail;
static uint32_t canvas_pixels[APP_CANVAS_HEIGHT][APP_CANVAS_WIDTH];

static void label(unsigned x, unsigned y, const char *text, uint32_t color) {
    while (*text) {
        gfx_glyph(&display, x, y, *text++, color, 0x1e3047, 2);
        x += CW;
    }
}
static int is_prompt(uint64_t line) {
    const char *prompt = "edus> ";
    for (unsigned i = 0; i < 6; i++) if (text_at(&history, line, i) != prompt[i]) return 0;
    return 1;
}
static void render_at(uint64_t line, unsigned x, uint64_t view) {
    if (line < view || line - view >= rows) return;
    gfx_glyph(&display, origin_x + x * CW, origin_y + (line - view) * CH,
              text_at(&history, line, x), x < 5 && is_prompt(line) ? 0x67d8b4 : FG, BG, 2);
}
static void cursor(void) {
    if (!history.follow || history.line < history.view || history.line - history.view >= rows) return;
    gfx_rect(&display, origin_x + history.column * CW,
              origin_y + (history.line - history.view) * CH + CH - 2, CW - 2, 2, 0x67d8b4);
}
static void clear_text(void) {
    gfx_rect(&display, origin_x, origin_y, cols * CW, rows * CH, BG);
}
static void redraw(void) {
    clear_text();
    for (unsigned y = 0; y < rows; y++)
        for (unsigned x = 0; x < cols; x++)
            if (text_at(&history, history.view + y, x) != ' ') render_at(history.view + y, x, history.view);
    cursor();
}
static void scroll_pixels(void) {
    if (!display.pixels) return;
    /* Copy only visible terminal pixels instead of re-rendering every glyph.
     * Top-to-bottom is safe for this upward overlapping move. Pitch/padding and
     * desktop/panel pixels stay untouched. This keeps live refresh responsive. */
    unsigned bytes = cols * CW * display.bytes_per_pixel;
    for (unsigned y = 0; y < (rows - 1) * CH; y++) {
        volatile uint8_t *dst = display.pixels + (origin_y + y) * display.pitch +
                                origin_x * display.bytes_per_pixel;
        volatile uint8_t *src = dst + CH * display.pitch;
        if (!((uintptr_t)dst & 3) && !((uintptr_t)src & 3) && !(bytes & 3)) {
            volatile uint32_t *d = (volatile uint32_t *)dst;
            const volatile uint32_t *s = (const volatile uint32_t *)src;
            for (unsigned i = 0; i < bytes / 4; i++) d[i] = s[i];
        } else {
            for (unsigned i = 0; i < bytes; i++) dst[i] = src[i];
        }
    }
    gfx_rect(&display, origin_x, origin_y + (rows - 1) * CH, cols * CW, CH, BG);
}
static void status(void) {
    uint64_t behind = text_bottom(&history) - history.view;
    if (previous_behind == behind && previous_follow == history.follow) return;
    previous_behind = behind; previous_follow = history.follow;
    char message[64]; unsigned n = 0;
    const char *prefix = history.follow ? "LIVE | PgUp/PgDn: history" : "HISTORY -";
    while (*prefix) message[n++] = *prefix++;
    if (!history.follow) {
        char digits[21]; unsigned count = 0;
        do { digits[count++] = '0' + behind % 10; behind /= 10; } while (behind);
        while (count) message[n++] = digits[--count];
        const char *suffix = " lines | End: live";
        while (*suffix) message[n++] = *suffix++;
    }
    unsigned area = cols * CW > 132 ? cols * CW - 132 : 0;
    if (area > 246) area = 246;
    unsigned x = origin_x + cols * CW - area;
    gfx_rect(&display, x, 65, area, 18, 0x1e3047);
    for (unsigned i = 0; i < n && (i + 1) * 6 <= area; i++)
        gfx_glyph(&display, x + i * 6, 69, message[i], history.follow ? 0xa7bbcf : 0x67d8b4, 0x1e3047, 1);
}
static void paint_tools(void) {
    gfx_clear(&display, 0x0a1220);
    gfx_rect(&display, 0, 0, display.width, 40, 0x1e3047);
    label(24, 12, "EDUCATIONAL OS", 0x67d8b4);
    gfx_rect(&display, origin_x - 10, 58, cols * CW + 20, 32, 0x1e3047);
    label(origin_x, 65, "Terminal", FG);
    gfx_rect(&display, origin_x - 10, 90, cols * CW + 20, rows * CH + 20, BG);
    gfx_rect(&display, 0, display.height - 30, display.width, 30, 0x1e3047);
    const char *hint = "Tab: Explorer | PgUp/PgDn: scroll | Up/Down: line | Home: oldest | End: live | help: pages";
    for (unsigned i = 0; hint[i] && 24 + (i + 1) * 6 < display.width; i++)
        gfx_glyph(&display, 24 + i * 6, display.height - 20, hint[i], 0xa7bbcf, 0x1e3047, 1);
    for (unsigned y=0;y<APP_CANVAS_HEIGHT;y++)
        for (unsigned x=0;x<APP_CANVAS_WIDTH;x++)
            if(canvas_pixels[y][x]) gfx_pixel(&display,display.width-APP_CANVAS_WIDTH-24+x,8+y,canvas_pixels[y][x]&0xffffff);
    unsigned panel_x = origin_x + cols * CW + 24;
    live_panel_attach(&display, panel_x, 58, display.width - panel_x - 24, display.height - 100);
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
    unsigned full_cols = (display.width - 64) / CW;
    unsigned panel_width = display.width >= 1200 ? 320 : display.width >= 1000 ? 288 : display.width >= 800 ? 256 : 208;
    cols = (display.width - 64 - panel_width) / CW;
    rows = (display.height - 144) / CH;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    origin_x = (display.width - full_cols * CW) / 2;
    origin_y = 100;
    paint_tools();
    text_init(&history, cols, rows);
    clear_text(); cursor(); status();
    active = 1;
    education_attach(&display);
    serial_write("[GFX] Graphical terminal ready; keyboard focus: shell.\r\n");
}
static void consume(char c) {
    if (c == '\t') {
        unsigned spaces = 4 - history.column % 4;
        while (spaces--) consume(' ');
        return;
    }
    uint64_t old_view = history.view, line = history.line;
    unsigned column = history.column;
    if (history.follow) render_at(line, column, old_view);
    unsigned changed = text_write(&history, (unsigned char)c);
    if (changed & TEXT_CLEAR) clear_text();
    else {
        if (changed & TEXT_CELL) {
            render_at(line, column, old_view);
            if (column < 6)
                for (unsigned x = 0; x < 6 && x < cols; x++) render_at(line, x, old_view);
        }
        if (old_view != history.view) {
            if (history.follow && history.view == old_view + 1) scroll_pixels();
            else redraw();
        }
    }
    cursor();
}
static void enqueue(unsigned code) {
    if (!active) return;
    /* Safe in IRQ/syscall context: no framebuffer access or formatting. */
    uint64_t flags = interrupts_save_and_disable();
    unsigned next = (output_head + 1) % OUTPUT_CAPACITY;
    if (next == output_tail) output_drops++;
    else { output[output_head] = code; output_head = next; }
    interrupts_restore(flags);
}
void gterminal_putchar(char c) { enqueue((unsigned char)c); }
void gterminal_navigate(enum gterminal_navigation action) { enqueue(action); }
unsigned gterminal_columns(void) { return active ? cols : 80; }
unsigned gterminal_rows(void) { return active ? rows : 24; }
void gterminal_flush(void) {
    if (!active) return;
    int visible = !education_active();
    if (visible && !tools_visible) {
        paint_tools(); redraw(); previous_behind=UINT64_MAX; previous_follow=-1;
    }
    tools_visible=visible;
    volatile uint8_t *pixels=display.pixels;
    if (!visible) display.pixels=0;
    for (unsigned n = 0; n < 128; n++) {
        uint64_t flags = interrupts_save_and_disable();
        if (output_tail == output_head) { interrupts_restore(flags); break; }
        unsigned code = output[output_tail];
        output_tail = (output_tail + 1) % OUTPUT_CAPACITY;
        interrupts_restore(flags);
        if (code < 256) consume(code);
        else {
            uint64_t old_view = history.view;
            int old_follow = history.follow;
            if (code == GT_FOLLOW) text_follow(&history);
            else if (code == GT_HOME) text_oldest(&history);
            else text_scroll(&history, code == GT_UP ? -1 : code == GT_DOWN ? 1 :
                             code == GT_PAGE_UP ? -(int)(rows - 1) : (int)(rows - 1));
            if (old_view != history.view || old_follow != history.follow) redraw();
        }
    }
    status();
    for (unsigned n = 0; n < CANVAS_QUEUE_SIZE; n++) {
        uint64_t flags = interrupts_save_and_disable();
        if (canvas_tail == canvas_head) { interrupts_restore(flags); break; }
        struct app_canvas_rect r = canvas_queue[canvas_tail];
        canvas_tail = (canvas_tail + 1) % CANVAS_QUEUE_SIZE;
        interrupts_restore(flags);
        for(unsigned y=r.y;y<r.y+r.height;y++)
            for(unsigned x=r.x;x<r.x+r.width;x++) canvas_pixels[y][x]=r.rgb|0x1000000;
        gfx_rect(&display, display.width - APP_CANVAS_WIDTH - 24 + r.x,
                  8 + r.y, r.width, r.height, r.rgb);
    }
    display.pixels=pixels;
}
int gterminal_canvas_submit(const struct app_canvas_rect *r) {
    if (!r || !r->width || !r->height || r->x >= APP_CANVAS_WIDTH ||
        r->y >= APP_CANVAS_HEIGHT || r->width > APP_CANVAS_WIDTH - r->x ||
        r->height > APP_CANVAS_HEIGHT - r->y || r->rgb > 0xffffff) return -2;
    if (!active) return -1;
    uint64_t flags = interrupts_save_and_disable();
    unsigned next = (canvas_head + 1) % CANVAS_QUEUE_SIZE;
    if (next == canvas_tail) { interrupts_restore(flags); return -3; }
    canvas_queue[canvas_head] = *r;
    canvas_head = next;
    interrupts_restore(flags);
    return 0;
}
uint64_t gterminal_output_drops(void) {
    uint64_t flags = interrupts_save_and_disable();
    uint64_t count = output_drops;
    interrupts_restore(flags);
    return count;
}
