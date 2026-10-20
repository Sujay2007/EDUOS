#include "graphics.h"
#include <stddef.h>

int gfx_surface_init(struct gfx_surface *s, const struct limine_framebuffer *fb) {
    if (!s) return -1;
    s->pixels = NULL;
    if (!fb || !fb->address || !fb->width || !fb->height ||
        fb->width > 16384 || fb->height > 16384 ||
        fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        (fb->bpp != 24 && fb->bpp != 32)) return -1;
    uint64_t bytes = fb->bpp / 8;
    if (fb->pitch < fb->width * bytes || fb->pitch > SIZE_MAX / fb->height ||
        (uintptr_t)fb->address > UINTPTR_MAX - fb->pitch * fb->height) return -1;
    uint8_t sizes[3] = {fb->red_mask_size, fb->green_mask_size, fb->blue_mask_size};
    uint8_t shifts[3] = {fb->red_mask_shift, fb->green_mask_shift, fb->blue_mask_shift};
    uint32_t used = 0;
    for (unsigned i = 0; i < 3; i++) {
        if (!sizes[i] || sizes[i] > 8 || shifts[i] >= fb->bpp ||
            sizes[i] > fb->bpp - shifts[i]) return -1;
        uint32_t mask = ((1U << sizes[i]) - 1) << shifts[i];
        if (used & mask) return -1;
        used |= mask;
    }
    s->width = fb->width; s->height = fb->height; s->pitch = fb->pitch;
    s->bytes_per_pixel = bytes;
    for (unsigned i = 0; i < 3; i++) { s->sizes[i] = sizes[i]; s->shifts[i] = shifts[i]; }
    s->pixels = fb->address;
    return 0;
}
static uint32_t encode(const struct gfx_surface *s, uint32_t rgb) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t channel = (rgb >> (16 - i * 8)) & 255;
        channel = channel * ((1U << s->sizes[i]) - 1) / 255;
        value |= channel << s->shifts[i];
    }
    return value;
}
static void write_pixel(struct gfx_surface *s, uint32_t x, uint32_t y, uint32_t color) {
    volatile uint8_t *p = s->pixels + y * s->pitch + (uint64_t)x * s->bytes_per_pixel;
    for (unsigned i = 0; i < s->bytes_per_pixel; i++) p[i] = color >> (i * 8);
}
void gfx_pixel(struct gfx_surface *s, int64_t x, int64_t y, uint32_t rgb) {
    if (!s || !s->pixels || x < 0 || y < 0 || (uint64_t)x >= s->width ||
        (uint64_t)y >= s->height) return;
    write_pixel(s, x, y, encode(s, rgb));
}
/* Clip without adding coordinates/extents, including INT64_MIN/UINT64_MAX. */
static int clip(int64_t origin, uint64_t extent, uint32_t limit,
                uint32_t *start, uint32_t *length) {
    if (origin < 0) {
        uint64_t skip = (uint64_t)(-(origin + 1)) + 1;
        if (skip >= extent) return 0;
        extent -= skip; *start = 0;
    } else {
        if ((uint64_t)origin >= limit) return 0;
        *start = origin;
    }
    if (extent > limit - *start) extent = limit - *start;
    *length = extent;
    return extent != 0;
}
void gfx_rect(struct gfx_surface *s, int64_t x, int64_t y,
              uint64_t width, uint64_t height, uint32_t rgb) {
    uint32_t left, top, w, h;
    if (!s || !s->pixels || !clip(x, width, s->width, &left, &w) ||
        !clip(y, height, s->height, &top, &h)) return;
    uint32_t color = encode(s, rgb);
    for (uint32_t row = top; row < top + h; row++)
        for (uint32_t col = left; col < left + w; col++) write_pixel(s, col, row, color);
}
void gfx_clear(struct gfx_surface *s, uint32_t rgb) {
    if (s) gfx_rect(s, 0, 0, s->width, s->height, rgb);
}
