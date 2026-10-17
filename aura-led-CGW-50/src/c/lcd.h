// Slanted 7-segment digits, rasterised straight into the framebuffer with exact
// coverage anti-aliasing. Ported from PT2-LCD221 (github.com/ChocolatineDigitale/PT2-LCD221,
// MIT, Copyright (c) 2026 Kicou); the segment outlines are from the 7-Segment font by
// Jan Bobrowski (SIL OFL 1.1), see segments.h and LICENSES/.
#pragma once
#include <pebble.h>
#include "segments.h"
#include "seg14.h"

#define LCD_SLANT 75      // lean, in thousandths of the digit height
#define LCD_AA_SUB 4
#define LCD_AA_MAX_W 96
#define LCD_DIGIT_MINUS 10

enum { SEG_A = 1, SEG_B = 2, SEG_C = 4, SEG_D = 8, SEG_E = 16, SEG_F = 32, SEG_G = 64 };

static const uint8_t DIGIT_SEGS[] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, SEG_B | SEG_C, SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,
  SEG_A | SEG_B | SEG_G | SEG_C | SEG_D, SEG_F | SEG_G | SEG_B | SEG_C, SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D, SEG_A | SEG_B | SEG_C, 0x7F,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G, SEG_G,
};

static GBitmap *s_fb;
static bool s_ghosts_on = true;   // draw the dotted unlit segments
static GColor s_ink, s_ghost;
static uint8_t s_aa[3];            // faint, medium, full ink
static int s_ghost_density = 3;    // out of 16
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void lcd_span(int y, int x0, int x1, GColor c, bool dither) {
  if (y < 0 || y >= PBL_DISPLAY_HEIGHT) return;
  GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, y);
  if (x0 < row.min_x) x0 = row.min_x;
  if (x1 > row.max_x) x1 = row.max_x;
  for (int x = x0; x <= x1; x++) {
    if (!dither || BAYER4[y & 3][x & 3] < s_ghost_density) row.data[x] = c.argb;
  }
}

static void lcd_fill(int x, int y, int w, int h, GColor c) {
  for (int r = 0; r < h; r++) lcd_span(y + r, x, x + w - 1, c, false);
}

static int lcd_floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static void lcd_poly_rows(const int16_t *pts, int n, int h, int *first, int *last) {
  int lo = 1000, hi = 0;
  for (int i = 0; i < n; i++) {
    if (pts[2 * i + 1] < lo) lo = pts[2 * i + 1];
    if (pts[2 * i + 1] > hi) hi = pts[2 * i + 1];
  }
  *first = lo * h / 1000 - 1;
  if (*first < 0) *first = 0;
  *last = hi * h / 1000 + 1;
  if (*last > h - 1) *last = h - 1;
}

// Unlit segment: pixel filled when its centre is inside, drawn as a dither.
static void lcd_fill_poly(int x, int y, int w, int h, const int16_t *pts, int n, int slant) {
  int first, last;
  lcd_poly_rows(pts, n, h, &first, &last);
  for (int r = first; r <= last; r++) {
    int32_t shift = (int32_t)(2 * (h - r) - 1) * slant;
    int32_t yn = (int32_t)(2 * r + 1) * 1000 / (2 * h);
    int32_t xs[8];
    int count = 0;
    for (int i = 0; i < n && count < 8; i++) {
      int32_t x0 = pts[2 * i], y0 = pts[2 * i + 1];
      int32_t x1 = pts[2 * ((i + 1) % n)], y1 = pts[2 * ((i + 1) % n) + 1];
      if ((y0 <= yn) != (y1 <= yn)) xs[count++] = x0 + (x1 - x0) * (yn - y0) / (y1 - y0);
    }
    for (int i = 1; i < count; i++) {
      for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int32_t t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    }
    for (int i = 0; i + 1 < count; i += 2) {
      int c0 = -lcd_floor_div(1000 - 2 * xs[i] * w - shift, 2000);
      int c1 = lcd_floor_div(2 * xs[i + 1] * w + shift - 1000, 2000);
      if (c0 <= c1) lcd_span(y + r, x + c0, x + c1, s_ghost, true);
    }
  }
}

// Lit segment: coverage measured at 4 heights per row, drawn in one of three shades.
static void lcd_fill_poly_aa(int x, int y, int w, int h, const int16_t *pts, int n, int slant) {
  int32_t cov[LCD_AA_MAX_W];
  int cols = w + h * slant / 1000 + 2;
  if (cols > LCD_AA_MAX_W) cols = LCD_AA_MAX_W;
  int first, last;
  lcd_poly_rows(pts, n, h, &first, &last);
  for (int r = first; r <= last; r++) {
    memset(cov, 0, sizeof(cov[0]) * cols);
    for (int k = 0; k < LCD_AA_SUB; k++) {
      int32_t yn = (int32_t)(2 * LCD_AA_SUB * r + 2 * k + 1) * 1000 / (2 * LCD_AA_SUB * h);
      int32_t shift = (int32_t)(2 * LCD_AA_SUB * (h - r) - 2 * k - 1) * slant / (2 * LCD_AA_SUB);
      int32_t xs[8];
      int count = 0;
      for (int i = 0; i < n && count < 8; i++) {
        int32_t x0 = pts[2 * i], y0 = pts[2 * i + 1];
        int32_t x1 = pts[2 * ((i + 1) % n)], y1 = pts[2 * ((i + 1) % n) + 1];
        if ((y0 <= yn) != (y1 <= yn)) xs[count++] = x0 + (x1 - x0) * (yn - y0) / (y1 - y0);
      }
      for (int i = 1; i < count; i++) {
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int32_t t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
      }
      for (int i = 0; i + 1 < count; i += 2) {
        int32_t a = xs[i] * w + shift, b = xs[i + 1] * w + shift;
        if (a < 0) a = 0;
        if (b > (int32_t)cols * 1000) b = (int32_t)cols * 1000;
        for (int c = a / 1000; c < cols && (int32_t)c * 1000 < b; c++) {
          int32_t lo = a > c * 1000 ? a : c * 1000, hi = b < (c + 1) * 1000 ? b : (c + 1) * 1000;
          if (hi > lo) cov[c] += hi - lo;
        }
      }
    }
    int yy = y + r;
    if (yy < 0 || yy >= PBL_DISPLAY_HEIGHT) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, yy);
    for (int c = 0; c < cols; c++) {
      int level = cov[c] >= 3000 ? 2 : cov[c] >= 1800 ? 1 : cov[c] >= 600 ? 0 : -1;
      int xx = x + c;
      if (level < 0 || xx < row.min_x || xx > row.max_x) continue;
      uint8_t cur = row.data[xx];
      int cur_level = cur == s_aa[2] ? 2 : cur == s_aa[1] ? 1 : -1;
      if (level > cur_level) row.data[xx] = s_aa[level];
    }
  }
}

static void lcd_digit(int x, int y, int w, int h, int value) {
  uint8_t on = (value >= 0 && value <= LCD_DIGIT_MINUS) ? DIGIT_SEGS[value] : 0;
  for (int pass = 0; pass <= 1; pass++) {
    if (!pass && !s_ghosts_on) continue;
    uint8_t mask = pass ? on : (uint8_t)(~on & 0x7F);
    for (int i = 0; i < 7; i++) {
      if (!(mask & (1 << i))) continue;
      if (pass) lcd_fill_poly_aa(x, y, w, h, SEG_POLYS[i], SEG_POLY_LEN[i], LCD_SLANT);
      else lcd_fill_poly(x, y, w, h, SEG_POLYS[i], SEG_POLY_LEN[i], LCD_SLANT);
    }
  }
}

static int lcd_dot(int w) { return (244 * w + 500) / 1000; }

static int lcd_cell(char c, int w) { return c == ':' ? lcd_dot(w) + 8 : c == ' ' ? w / 2 : w; }

static int lcd_width(const char *s, int w, int h, int gap) {
  int tot = h * LCD_SLANT / 1000;
  for (const char *c = s; *c; c++) tot += lcd_cell(*c, w) + (c[1] ? gap : 0);
  return tot;
}

// Draw `s` (digits, '-', ':', ' ') with its top-left at (x, y). Call outside any other
// drawing on the context: it captures the framebuffer.
static void lcd_str(GContext *ctx, const char *s, int x, int y, int w, int h, int gap) {
  s_fb = graphics_capture_frame_buffer(ctx);
  if (!s_fb) return;
  for (const char *c = s; *c; c++) {
    if (*c == ':') {
      int d = lcd_dot(w);
      for (int k = 0; k < 2; k++) {
        int cy = h * (k ? 70 : 30) / 100;
        lcd_fill(x + 4 + (h - cy) * LCD_SLANT / 1000, y + cy - d / 2, d, d, s_ink);
      }
    } else if (*c == '-') {
      lcd_digit(x, y, w, h, LCD_DIGIT_MINUS);
    } else if (*c >= '0' && *c <= '9') {
      lcd_digit(x, y, w, h, *c - '0');
    }
    x += lcd_cell(*c, w) + gap;
  }
  graphics_release_frame_buffer(ctx, s_fb);
}

static void lcd_centered(GContext *ctx, const char *s, int w, int h, int gap, GRect box) {
  lcd_str(ctx, s, box.origin.x + (box.size.w - lcd_width(s, w, h, gap)) / 2, box.origin.y + (box.size.h - h) / 2, w, h, gap);
}

// 14-segment letters and digits (DSEG14 outlines, see seg14.h), same renderer as the time digits.
// The width of a glyph follows its height (SEG14_ASPECT); `gap` separates characters.
static int lcd14_w(int h) { return h * SEG14_ASPECT / 1000; }

static void lcd_glyph14(int x, int y, int w, int h, uint32_t mask, bool lit) {
  for (int i = 0; i < SEG14_N; i++) {
    if (!(mask & (1U << i))) continue;
    if (lit) lcd_fill_poly_aa(x, y, w, h, &SEG14_POLYS[i][0][0], SEG14_LEN[i], LCD_SLANT);
    else lcd_fill_poly(x, y, w, h, &SEG14_POLYS[i][0][0], SEG14_LEN[i], LCD_SLANT);
  }
}

// Draw `s` (capitals, digits, spaces) with its top-left at (x, y); returns the x after it.
static int lcd_str14(GContext *ctx, const char *s, int x, int y, int h, int gap) {
  int w = lcd14_w(h);
  s_fb = graphics_capture_frame_buffer(ctx);
  if (!s_fb) return x;
  for (const char *c = s; *c; c++) {
    const char *at = *c == ' ' ? NULL : strchr(SEG14_CHARS, *c);
    if (at) {
      if (s_ghosts_on) lcd_glyph14(x, y, w, h, SEG14_ALL & ~SEG14_MASK[at - SEG14_CHARS], false);
      lcd_glyph14(x, y, w, h, SEG14_MASK[at - SEG14_CHARS], true);
    }
    x += (at ? w : w / 2 + 2) + gap;
  }
  graphics_release_frame_buffer(ctx, s_fb);
  return x;
}
