/*
 * UI preview: a small stand-in for vita2d that draws into a PNG with cairo and
 * FreeType, so the menus can be rendered and checked on a PC.
 *
 * Text follows vita2d's own layout rules (glyphs rendered at the pixel size,
 * pen advanced by whole pixels), so widths and alignment match the console
 * closely. Colours, shapes and textures cover what src/gui uses.
 */
#include <vita2d.h>

#include <cairo.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 960
#define H 544

struct vita2d_font {
  FT_Face face;
};

typedef struct {
  vita2d_texture base;   /* must stay first: the app only holds vita2d_texture* */
  int w, h, stride;
  unsigned char *rgba;   /* Vita layout: R, G, B, A bytes */
  cairo_surface_t *png;  /* set for textures loaded from a PNG */
} fake_tex;

static cairo_surface_t *screen;
static cairo_t *cr;
static FT_Library ft;
static unsigned int clear_color = 0xff000000;
static int clip_on, clip_x0, clip_y0, clip_x1, clip_y1;

void preview_frame_done(cairo_surface_t *surface);   /* preview_sys.c */

static void set_color(unsigned int c) {
  cairo_set_source_rgba(cr, (c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0,
                        ((c >> 16) & 0xFF) / 255.0, ((c >> 24) & 0xFF) / 255.0);
}

static void apply_clip(void) {
  cairo_reset_clip(cr);
  if (clip_on) {
    cairo_rectangle(cr, clip_x0, clip_y0, clip_x1 - clip_x0, clip_y1 - clip_y0);
    cairo_clip(cr);
  }
}

int vita2d_init(void) { return vita2d_init_advanced_with_msaa(0, 0); }
int vita2d_init_advanced(unsigned int temp_pool_size) { return vita2d_init_advanced_with_msaa(0, 0); }
int vita2d_init_advanced_with_msaa(unsigned int temp_pool_size, SceGxmMultisampleMode msaa) {
  if (!screen) {
    screen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cr = cairo_create(screen);
    FT_Init_FreeType(&ft);
  }
  return 1;
}
int vita2d_fini(void) { return 1; }
void vita2d_set_clear_color(unsigned int color) { clear_color = color; }
void vita2d_set_vblank_wait(int enable) {}
void vita2d_wait_rendering_done(void) {}
static void pool_reset(void);
void vita2d_start_drawing(void) { pool_reset(); apply_clip(); }
void vita2d_end_drawing(void) {}
void vita2d_clear_screen(void) {
  cairo_save(cr);
  cairo_reset_clip(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  set_color(clear_color);
  cairo_paint(cr);
  cairo_restore(cr);
}
void vita2d_swap_buffers(void) {
  cairo_surface_flush(screen);
  preview_frame_done(screen);
}

void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max) {
  clip_x0 = x_min; clip_y0 = y_min; clip_x1 = x_max; clip_y1 = y_max;
  apply_clip();
}
void vita2d_enable_clipping(void) { clip_on = 1; apply_clip(); }
void vita2d_disable_clipping(void) { clip_on = 0; apply_clip(); }

/* Per-frame vertex memory, released when the frame starts, like vita2d's pool
 * (so that a leak check only reports what the menus themselves keep). */
static void **pool_blocks;
static size_t pool_count, pool_cap;

static void pool_reset(void) {
  for (size_t i = 0; i < pool_count; i++)
    free(pool_blocks[i]);
  pool_count = 0;
}

void *vita2d_pool_memalign(unsigned int size, unsigned int alignment) {
  if (pool_count == pool_cap) {
    pool_cap = pool_cap ? pool_cap * 2 : 256;
    pool_blocks = realloc(pool_blocks, pool_cap * sizeof(void *));
  }
  void *p = malloc(size);
  pool_blocks[pool_count++] = p;
  return p;
}

void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color) {
  set_color(color);
  cairo_rectangle(cr, x, y, w, h);
  cairo_fill(cr);
}

void vita2d_draw_line(float x0, float y0, float x1, float y1, unsigned int color) {
  set_color(color);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x0 + 0.5, y0 + 0.5);
  cairo_line_to(cr, x1 + 0.5, y1 + 0.5);
  cairo_stroke(cr);
}

void vita2d_draw_fill_circle(float x, float y, float radius, unsigned int color) {
  set_color(color);
  cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
  cairo_fill(cr);
}

void vita2d_draw_array(SceGxmPrimitiveType mode, const vita2d_color_vertex *v, size_t count) {
  if (count < 2)
    return;
  if (mode == SCE_GXM_PRIMITIVE_LINES) {
    cairo_set_line_width(cr, 1.0);
    for (size_t i = 0; i + 1 < count; i += 2) {
      set_color(v[i].color);
      cairo_move_to(cr, v[i].x, v[i].y);
      cairo_line_to(cr, v[i + 1].x, v[i + 1].y);
      cairo_stroke(cr);
    }
    return;
  }
  if (mode == SCE_GXM_PRIMITIVE_TRIANGLE_FAN) {
    set_color(v[0].color);
    cairo_move_to(cr, v[1].x, v[1].y);
    for (size_t i = 2; i < count; i++)
      cairo_line_to(cr, v[i].x, v[i].y);
    cairo_close_path(cr);
    cairo_fill(cr);
    return;
  }
  if (mode == SCE_GXM_PRIMITIVE_TRIANGLE_STRIP) {
    /* Outline of the strip: even vertices forwards, odd ones backwards. */
    cairo_move_to(cr, v[0].x, v[0].y);
    for (size_t i = 2; i < count; i += 2)
      cairo_line_to(cr, v[i].x, v[i].y);
    for (size_t i = (count - 1) | 1; ; i -= 2) {
      if (i < count)
        cairo_line_to(cr, v[i].x, v[i].y);
      if (i == 1)
        break;
    }
    cairo_close_path(cr);
    if (count == 4 && v[0].color == v[1].color && v[2].color == v[3].color && v[0].color != v[2].color) {
      /* two-colour quad: vertical gradient */
      cairo_pattern_t *p = cairo_pattern_create_linear(0, v[0].y, 0, v[2].y);
      unsigned int a = v[0].color, b = v[2].color;
      cairo_pattern_add_color_stop_rgba(p, 0, (a & 0xFF) / 255.0, ((a >> 8) & 0xFF) / 255.0,
                                        ((a >> 16) & 0xFF) / 255.0, ((a >> 24) & 0xFF) / 255.0);
      cairo_pattern_add_color_stop_rgba(p, 1, (b & 0xFF) / 255.0, ((b >> 8) & 0xFF) / 255.0,
                                        ((b >> 16) & 0xFF) / 255.0, ((b >> 24) & 0xFF) / 255.0);
      cairo_set_source(cr, p);
      cairo_fill(cr);
      cairo_pattern_destroy(p);
    } else {
      set_color(v[0].color);
      cairo_fill(cr);
    }
  }
}

/* ---------------- textures ---------------- */

vita2d_texture *vita2d_create_empty_texture_format(unsigned int w, unsigned int h, SceGxmTextureFormat format) {
  fake_tex *t = calloc(1, sizeof(*t));
  t->w = w; t->h = h; t->stride = w * 4;
  t->rgba = calloc(1, (size_t)t->stride * h);
  return &t->base;
}
vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h) {
  return vita2d_create_empty_texture_format(w, h, 0);
}
vita2d_texture *vita2d_load_PNG_file(const char *filename) {
  cairo_surface_t *s = cairo_image_surface_create_from_png(filename);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "preview: cannot load %s\n", filename);
    cairo_surface_destroy(s);
    return NULL;
  }
  fake_tex *t = calloc(1, sizeof(*t));
  t->png = s;
  t->w = cairo_image_surface_get_width(s);
  t->h = cairo_image_surface_get_height(s);
  return &t->base;
}
void vita2d_free_texture(vita2d_texture *texture) {
  fake_tex *t = (fake_tex *)texture;
  if (!t) return;
  if (t->png) cairo_surface_destroy(t->png);
  free(t->rgba);
  free(t);
}
void *vita2d_texture_get_datap(const vita2d_texture *texture) { return ((fake_tex *)texture)->rgba; }
unsigned int vita2d_texture_get_stride(const vita2d_texture *texture) { return ((fake_tex *)texture)->stride; }
unsigned int vita2d_texture_get_width(const vita2d_texture *texture) { return ((fake_tex *)texture)->w; }
unsigned int vita2d_texture_get_height(const vita2d_texture *texture) { return ((fake_tex *)texture)->h; }
void vita2d_texture_set_filters(vita2d_texture *texture, SceGxmTextureFilter min_filter, SceGxmTextureFilter mag_filter) {}

static cairo_surface_t *tex_surface(fake_tex *t, int *owned) {
  *owned = 0;
  if (t->png)
    return t->png;
  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, t->w, t->h);
  unsigned char *dst = cairo_image_surface_get_data(s);
  int dstride = cairo_image_surface_get_stride(s);
  for (int y = 0; y < t->h; y++) {
    for (int x = 0; x < t->w; x++) {
      const unsigned char *p = t->rgba + y * t->stride + x * 4;
      unsigned int a = p[3];
      unsigned int px = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
      memcpy(dst + y * dstride + x * 4, &px, 4);
    }
  }
  cairo_surface_mark_dirty(s);
  *owned = 1;
  return s;
}

void vita2d_draw_texture_part_scale(const vita2d_texture *texture, float x, float y, float tex_x, float tex_y,
                                    float tex_w, float tex_h, float x_scale, float y_scale) {
  fake_tex *t = (fake_tex *)texture;
  if (!t) return;
  int owned;
  cairo_surface_t *s = tex_surface(t, &owned);
  cairo_save(cr);
  cairo_rectangle(cr, x, y, tex_w * x_scale, tex_h * y_scale);
  cairo_clip(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, x_scale, y_scale);
  cairo_set_source_surface(cr, s, -tex_x, -tex_y);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  if (owned) cairo_surface_destroy(s);
}
void vita2d_draw_texture(const vita2d_texture *texture, float x, float y) {
  fake_tex *t = (fake_tex *)texture;
  if (t) vita2d_draw_texture_part_scale(texture, x, y, 0, 0, t->w, t->h, 1, 1);
}
void vita2d_draw_texture_scale(const vita2d_texture *texture, float x, float y, float x_scale, float y_scale) {
  fake_tex *t = (fake_tex *)texture;
  if (t) vita2d_draw_texture_part_scale(texture, x, y, 0, 0, t->w, t->h, x_scale, y_scale);
}
void vita2d_draw_texture_part(const vita2d_texture *texture, float x, float y, float tex_x, float tex_y, float tex_w, float tex_h) {
  vita2d_draw_texture_part_scale(texture, x, y, tex_x, tex_y, tex_w, tex_h, 1, 1);
}

/* ---------------- fonts ---------------- */

vita2d_font *vita2d_load_font_file(const char *filename) {
  vita2d_font *f = calloc(1, sizeof(*f));
  if (FT_New_Face(ft, filename, 0, &f->face)) {
    fprintf(stderr, "preview: cannot load font %s\n", filename);
    free(f);
    return NULL;
  }
  return f;
}
vita2d_font *vita2d_load_font_mem(const void *buffer, unsigned int size) {
  vita2d_font *f = calloc(1, sizeof(*f));
  if (FT_New_Memory_Face(ft, buffer, size, 0, &f->face)) {
    free(f);
    return NULL;
  }
  return f;
}
void vita2d_free_font(vita2d_font *font) {
  if (font) { FT_Done_Face(font->face); free(font); }
}

static int utf8_next(const char *s, unsigned int *cp) {
  const unsigned char *u = (const unsigned char *)s;
  if (u[0] < 0x80) { *cp = u[0]; return 1; }
  if ((u[0] & 0xE0) == 0xC0 && u[1]) { *cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
  if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { *cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); return 3; }
  if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
    *cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    return 4;
  }
  *cp = '?';
  return 1;
}

/* Same rules as vita2d's generic_font_draw_text. */
static int font_text(vita2d_font *font, int draw, int x, int y, unsigned int color, unsigned int size, const char *text) {
  if (!font || !text)
    return 0;
  FT_Set_Pixel_Sizes(font->face, size, size);
  int pen_x = x, max_x = x, pen_y = y;
  for (int i = 0; text[i];) {
    unsigned int cp;
    i += utf8_next(&text[i], &cp);
    if (cp == '\n') {
      if (pen_x > max_x) max_x = pen_x;
      pen_x = x;
      pen_y += size;
      continue;
    }
    FT_UInt gi = FT_Get_Char_Index(font->face, cp);
    if (FT_Load_Glyph(font->face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
      continue;
    FT_GlyphSlot g = font->face->glyph;
    if (draw && g->bitmap.width && g->bitmap.rows) {
      int bw = g->bitmap.width, bh = g->bitmap.rows;
      cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, bw, bh);
      unsigned char *dst = cairo_image_surface_get_data(mask);
      int stride = cairo_image_surface_get_stride(mask);
      for (int r = 0; r < bh; r++)
        memcpy(dst + r * stride, g->bitmap.buffer + r * g->bitmap.pitch, bw);
      cairo_surface_mark_dirty(mask);
      set_color(color);
      cairo_mask_surface(cr, mask, pen_x + g->bitmap_left, pen_y - g->bitmap_top);
      cairo_surface_destroy(mask);
    }
    pen_x += g->advance.x >> 6;
  }
  if (pen_x > max_x) max_x = pen_x;
  return max_x - x;
}

int vita2d_font_draw_text(vita2d_font *font, int x, int y, unsigned int color, unsigned int size, const char *text) {
  return font_text(font, 1, x, y, color, size, text);
}
int vita2d_font_text_width(vita2d_font *font, unsigned int size, const char *text) {
  return font_text(font, 0, 0, 0, 0, size, text);
}
int vita2d_common_dialog_update(void) { return 0; }
