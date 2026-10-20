#include "graphics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[16 + 3 * 16 + 16];
static struct limine_framebuffer framebuffer(void) {
    struct limine_framebuffer fb = {0};
    fb.address = memory + 16; fb.width = 3; fb.height = 3; fb.pitch = 16;
    fb.bpp = 32; fb.memory_model = LIMINE_FRAMEBUFFER_RGB;
    fb.red_mask_size = fb.green_mask_size = fb.blue_mask_size = 8;
    fb.red_mask_shift = 16; fb.green_mask_shift = 8; fb.blue_mask_shift = 0;
    return fb;
}
static void guards(unsigned bytes) {
    for (unsigned i = 0; i < 16; i++) assert(memory[i] == 0xaa);
    for (unsigned i = 64; i < sizeof(memory); i++) assert(memory[i] == 0xaa);
    for (unsigned row = 0; row < 3; row++)
        for (unsigned i = 3 * bytes; i < 16; i++) assert(memory[16 + row * 16 + i] == 0xaa);
}
int main(void) {
    struct gfx_surface s;
    struct limine_framebuffer fb = framebuffer();
    memset(memory, 0xaa, sizeof(memory));
    assert(gfx_surface_init(&s, &fb) == 0);
    gfx_clear(&s, 0x123456);
    assert(memory[16] == 0x56 && memory[17] == 0x34 && memory[18] == 0x12 && memory[19] == 0);
    guards(4);
    gfx_pixel(&s, -1, 0, 0); gfx_pixel(&s, 3, 0, 0); gfx_pixel(&s, 0, 3, 0);
    assert(memory[16] == 0x56);
    gfx_rect(&s, -1, -1, 2, 2, 0xffffff);
    assert(memory[16] == 255 && memory[20] == 0x56 && memory[32] == 0x56);
    gfx_rect(&s, INT64_MAX, INT64_MAX, UINT64_MAX, UINT64_MAX, 0);
    gfx_rect(&s, 0, 0, 0, UINT64_MAX, 0);
    assert(memory[16] == 255);
    gfx_rect(&s, INT64_MIN, INT64_MIN, UINT64_MAX, UINT64_MAX, 0xabcdef);
    assert(memory[16] == 0xef && memory[56] == 0xef);
    guards(4);
    fb.bpp = 24; fb.red_mask_shift = 0; fb.blue_mask_shift = 16;
    memset(memory, 0xaa, sizeof(memory));
    assert(gfx_surface_init(&s, &fb) == 0);
    gfx_clear(&s, 0x123456);
    assert(memory[16] == 0x12 && memory[17] == 0x34 && memory[18] == 0x56);
    guards(3);
    fb.red_mask_size = 5; fb.red_mask_shift = 11;
    fb.green_mask_size = 6; fb.green_mask_shift = 5;
    fb.blue_mask_size = 5; fb.blue_mask_shift = 0;
    assert(gfx_surface_init(&s, &fb) == 0);
    gfx_pixel(&s, 0, 0, 0xffffff);
    assert(memory[16] == 255 && memory[17] == 255 && memory[18] == 0);
    fb = framebuffer(); fb.pitch = 11;
    assert(gfx_surface_init(&s, &fb) != 0 && !s.pixels);
    fb = framebuffer(); fb.red_mask_shift = 8;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.red_mask_size = 0;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.bpp = 16;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.memory_model = 0;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.height = 0;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.width = UINT64_MAX;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.pitch = UINT64_MAX;
    assert(gfx_surface_init(&s, &fb) != 0);
    fb = framebuffer(); fb.address = (void *)(UINTPTR_MAX - 16);
    assert(gfx_surface_init(&s, &fb) != 0);
    assert(gfx_surface_init(&s, NULL) != 0 && gfx_surface_init(NULL, &fb) != 0);
    fb = framebuffer(); memset(memory, 0xaa, sizeof(memory));
    assert(gfx_surface_init(&s, &fb) == 0);
    gfx_glyph(&s, -2, -2, 'A', 0xffffff, 0, 1);
    gfx_glyph(&s, INT64_MAX, INT64_MIN, 'A', 0, 0, 4);
    gfx_glyph(&s, 0, 0, 255, 0xffffff, 0, 2);
    gfx_glyph(&s, 0, 0, 'A', 0, 0, 0);
    guards(4);
    puts("Graphics primitives PASS: pitch, RGB formats, masks, clipping, overflow, glyph guards");
    return 0;
}
