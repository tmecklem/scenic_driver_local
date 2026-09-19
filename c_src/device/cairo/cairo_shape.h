#pragma once

#include <cairo.h>
#include <hb.h>

typedef struct {
  cairo_glyph_t* glyphs;
  int count;
  double advance;
} shaped_text_t;

hb_font_t* shape_font_create(const unsigned char* data, unsigned int size);
void shape_font_destroy(hb_font_t* font);

// Glyph positions are in pixels, starting at x = 0 on the baseline.
int shape_text(hb_font_t* font, double font_size, const char* text, int length,
               shaped_text_t* out);
void shaped_text_free(shaped_text_t* shaped);
