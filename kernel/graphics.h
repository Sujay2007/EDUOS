#ifndef GRAPHICS_H
#define GRAPHICS_H
#include <stdint.h>
#include <limine.h>

/* RGB colors are 0xRRGGBB, independent of framebuffer byte/channel order. */
struct gfx_surface {
    volatile uint8_t *pixels;
    uint32_t width, height;
    uint64_t pitch;
    uint8_t bytes_per_pixel, sizes[3], shifts[3];
};
/* Supports packed little-endian 24/32-bit RGB, 1..8 bits per channel. */
int gfx_surface_init(struct gfx_surface *s, const struct limine_framebuffer *fb);
void gfx_pixel(struct gfx_surface *s, int64_t x, int64_t y, uint32_t rgb);
void gfx_rect(struct gfx_surface *s, int64_t x, int64_t y,
              uint64_t width, uint64_t height, uint32_t rgb);
void gfx_clear(struct gfx_surface *s, uint32_t rgb);
/* Embedded 5x7 bitmap glyphs in a 6x9 cell, scale 1..4, ASCII with '?' fallback. */
void gfx_glyph(struct gfx_surface *s, int64_t x, int64_t y, unsigned char ch,
               uint32_t fg, uint32_t bg, unsigned scale);
void gterminal_init(void);
void gterminal_putchar(char c);
#endif
