#include "cairo_shape.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

static unsigned char* read_file(const char* path, unsigned int* size)
{
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  rewind(f);
  unsigned char* data = malloc(*size);
  fread(data, 1, *size, f);
  fclose(f);
  return data;
}

static double advance_of(hb_font_t* font, double size, const char* text)
{
  shaped_text_t shaped;
  shape_text(font, size, text, -1, &shaped);
  double advance = shaped.advance;
  shaped_text_free(&shaped);
  return advance;
}

static void test_kerns_pairs(hb_font_t* font)
{
  shaped_text_t to;
  CHECK(shape_text(font, 100, "To", -1, &to) == 0);
  CHECK(to.count == 2);

  double unkerned = advance_of(font, 100, "T") + advance_of(font, 100, "o");
  CHECK(to.advance < unkerned);
  CHECK(to.glyphs[1].x < advance_of(font, 100, "T"));
  shaped_text_free(&to);
}

static void test_scales_with_font_size(hb_font_t* font)
{
  shaped_text_t small, large;
  shape_text(font, 24, "Hello", -1, &small);
  shape_text(font, 48, "Hello", -1, &large);

  CHECK(small.count == 5);
  CHECK(fabs(large.advance - 2 * small.advance) < 0.01);
  CHECK(fabs(large.glyphs[4].x - 2 * small.glyphs[4].x) < 0.01);
  CHECK(small.glyphs[0].x == 0 && small.glyphs[0].y == 0);

  shaped_text_free(&small);
  shaped_text_free(&large);
}

static void test_empty_text(hb_font_t* font)
{
  shaped_text_t empty;
  CHECK(shape_text(font, 24, "", -1, &empty) == 0);
  CHECK(empty.count == 0);
  CHECK(empty.advance == 0);
  shaped_text_free(&empty);
}

int main(int argc, char** argv)
{
  unsigned int size;
  unsigned char* data = read_file(argv[1], &size);
  if (!data) {
    fprintf(stderr, "can't read %s\n", argv[1]);
    return 1;
  }

  hb_font_t* font = shape_font_create(data, size);
  test_kerns_pairs(font);
  test_scales_with_font_size(font);
  test_empty_text(font);
  shape_font_destroy(font);
  free(data);

  printf(failures ? "%d failure(s)\n" : "all shape tests passed\n", failures);
  return failures ? 1 : 0;
}
