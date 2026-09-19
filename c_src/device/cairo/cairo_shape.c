#include "cairo_shape.h"

#include <stdlib.h>

hb_font_t* shape_font_create(const unsigned char* data, unsigned int size)
{
  // The caller keeps data alive for the font's lifetime, as FreeType also requires.
  hb_blob_t* blob = hb_blob_create((const char*)data, size, HB_MEMORY_MODE_READONLY, NULL, NULL);
  hb_face_t* face = hb_face_create(blob, 0);
  hb_font_t* font = hb_font_create(face);
  hb_face_destroy(face);
  hb_blob_destroy(blob);
  return font;
}

void shape_font_destroy(hb_font_t* font)
{
  hb_font_destroy(font);
}

int shape_text(hb_font_t* font, double font_size, const char* text, int length,
               shaped_text_t* out)
{
  out->glyphs = NULL;
  out->count = 0;
  out->advance = 0;

  hb_buffer_t* buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, text, length, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font, buffer, NULL, 0);

  unsigned int count;
  hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
  hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);

  // Positions come back in font units; the default font scale is units per em.
  double scale = font_size / hb_face_get_upem(hb_font_get_face(font));

  if (count > 0) {
    out->glyphs = malloc(sizeof(cairo_glyph_t) * count);
    if (!out->glyphs) {
      hb_buffer_destroy(buffer);
      return -1;
    }
  }

  double x = 0;
  double y = 0;
  for (unsigned int i = 0; i < count; i++) {
    out->glyphs[i].index = infos[i].codepoint;
    out->glyphs[i].x = x + positions[i].x_offset * scale;
    out->glyphs[i].y = y - positions[i].y_offset * scale;
    x += positions[i].x_advance * scale;
    y -= positions[i].y_advance * scale;
  }

  out->count = count;
  out->advance = x;
  hb_buffer_destroy(buffer);
  return 0;
}

void shaped_text_free(shaped_text_t* shaped)
{
  free(shaped->glyphs);
  shaped->glyphs = NULL;
  shaped->count = 0;
}
