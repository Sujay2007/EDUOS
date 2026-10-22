#ifndef EDUCATION_H
#define EDUCATION_H
#include "graphics.h"
void education_attach(struct gfx_surface *s);
int education_active(void);
/* Main shell input consumer only; serial remains an independent operational shell. */
int education_input(unsigned char key, int serial);
/* Sole framebuffer worker calls this with interrupts enabled. */
void education_render(void);
#endif
