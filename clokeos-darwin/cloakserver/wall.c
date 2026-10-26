/*
 * Hintergrundbild "Goldenes Tor": eine Hängebrücke im Nebel, hell am Tag und dunkel am Abend.
 * Zeile für Zeile übertragen aus drawBay() in clokeos.html (Canvas 2D -> cairo), mit demselben
 * Zufallsgenerator, damit Sterne, Lichter und Nebel an denselben Stellen sitzen.
 */
#include "cloakserver.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static long long seed;
static double rnd(void) { seed = (seed * 16807) % 2147483647; return (double)seed / 2147483647; }
/* (x).toFixed(2) */
static double fixed2(double x) { return round(x * 100) / 100; }

static void hex(cairo_t *cr, unsigned rgb) {
  cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
}
static void stop(cairo_pattern_t *p, double off, unsigned rgb, double a) {
  cairo_pattern_add_color_stop_rgba(p, off, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0,
                                    (rgb & 255) / 255.0, a);
}
static void fill_rect(cairo_t *cr, double x, double y, double w, double h) {
  cairo_rectangle(cr, x, y, w, h);
  cairo_fill(cr);
}
/* quadraticCurveTo als kubische Kurve */
static void quad_to(cairo_t *cr, double x0, double y0, double qx, double qy, double x, double y) {
  cairo_curve_to(cr, x0 + 2.0 / 3 * (qx - x0), y0 + 2.0 / 3 * (qy - y0),
                 x + 2.0 / 3 * (qx - x), y + 2.0 / 3 * (qy - y), x, y);
}
static double quad(double p0, double p1, double p2, double t) {
  return (1 - t) * (1 - t) * p0 + 2 * (1 - t) * t * p1 + t * t * p2;
}

static void hill(cairo_t *cr, double W, double hz, double x0, double x1, double h, unsigned col, double wob) {
  cairo_new_path(cr);
  cairo_move_to(cr, x0, hz + 1);
  for (double x = x0; x <= x1; x += W / 300) {
    double t = (x - x0) / (x1 - x0);
    double y = hz - h * pow(sin(M_PI * fmin(1, t * 1.15)), .75) *
                        (1 + .07 * sin(t * 19 + wob) + .04 * sin(t * 41 + wob * 2));
    cairo_line_to(cr, x, y);
  }
  cairo_line_to(cr, x1, hz + 1);
  cairo_close_path(cr);
  hex(cr, col);
  cairo_fill(cr);
}

static void wall_draw_bridge(cairo_t *cr, double W, double H, bool dark) {
  cairo_surface_t *target = cairo_get_target(cr);
  seed = 7;
  double hz = H * .64, deck = hz - H * .085;
  cairo_pattern_t *g;

  /* Himmel */
  g = cairo_pattern_create_linear(0, 0, 0, hz);
  if (dark) { stop(g, 0, 0x0a0e2c, 1); stop(g, .5, 0x2c1f55, 1); stop(g, .82, 0xa34470, 1); stop(g, 1, 0xf2955e, 1); }
  else { stop(g, 0, 0x4f8fdc, 1); stop(g, .55, 0xa3cbef, 1); stop(g, .88, 0xf4e0cd, 1); stop(g, 1, 0xf9cfa8, 1); }
  cairo_set_source(cr, g); fill_rect(cr, 0, 0, W, hz + 2); cairo_pattern_destroy(g);
  if (dark)
    for (int i = 0; i < 220; i++) {
      double a = fixed2(rnd() * .7 + .1);
      double s = rnd() * 1.6 * (W / 1400) + .5;
      double x = rnd() * W, y = rnd() * hz * .55;
      cairo_set_source_rgba(cr, 1, 1, 1, a);
      fill_rect(cr, x, y, s, s);
    }
  double sx = W * .8, sy = hz - H * .02;
  g = cairo_pattern_create_radial(sx, sy, 0, sx, sy, H * .6);
  if (dark) { stop(g, 0, 0xffa064, .6); stop(g, .25, 0xf06e6e, .22); }
  else { stop(g, 0, 0xfff4de, .95); stop(g, .25, 0xffe2be, .45); }
  stop(g, 1, 0xffc896, 0);
  cairo_set_source(cr, g); fill_rect(cr, 0, 0, W, hz); cairo_pattern_destroy(g);

  /* Landzungen und das ferne Ufer */
  hill(cr, W, hz, -W * .05, W * .5, H * .2, dark ? 0x2d2250 : 0x9fb0cf, 1);
  hill(cr, W, hz, -W * .1, W * .34, H * .14, dark ? 0x1d1838 : 0x7e93b6, 3);
  hill(cr, W, hz, W * .52, W * 1.05, H * .045, dark ? 0x2a2048 : 0xb5bfd4, 5);
  for (int i = 0; i < 140; i++) {
    double x = W * (.55 + rnd() * .45), y = hz - rnd() * H * .03 - 1;
    if (dark) {
      int gr = (int)(190 + rnd() * 50);
      cairo_set_source_rgba(cr, 1, gr / 255.0, 140 / 255.0, fixed2(.5 + rnd() * .5));
    } else {
      cairo_set_source_rgba(cr, 1, 1, 1, fixed2(.35 + rnd() * .4));
    }
    fill_rect(cr, x, y, fmax(1, W / 900), fmax(1, H / 600) * (dark ? 1 : 2.5));
  }

  /* Wasser */
  g = cairo_pattern_create_linear(0, hz, 0, H);
  if (dark) { stop(g, 0, 0x4a2c5e, 1); stop(g, 1, 0x090b20, 1); } else { stop(g, 0, 0xa9c6e0, 1); stop(g, 1, 0x2c5b8c, 1); }
  cairo_set_source(cr, g); fill_rect(cr, 0, hz, W, H - hz); cairo_pattern_destroy(g);

  /* Brücke */
  double x1 = W * .37, x2 = W * .75, top = hz - H * .44, lw = fmax(1, W / 1600);
  unsigned orange = dark ? 0x7d2c35 : 0xc4432c, orangeD = dark ? 0x5a1f2c : 0x9b3020;
  double spans[3][6] = {
    { W * .1, deck - H * .004, x1 - W * .08, deck - H * .05, x1, top + H * .02 },
    { x1, top + H * .02, (x1 + x2) / 2, 2 * (deck - H * .012) - (top + H * .02), x2, top + H * .02 },
    { x2, top + H * .02, x2 + W * .1, deck - H * .07, W * 1.08, deck - H * .004 },
  };
  for (int k = 0; k < 3; k++) {
    double *s = spans[k];
    cairo_set_line_width(cr, lw * .8);
    cairo_push_group(cr);
    hex(cr, orange);
    int n = (int)round((s[4] - s[0]) / (W / 150));
    for (int i = 1; i < n; i++) {
      double t = (double)i / n, x = quad(s[0], s[2], s[4], t), y = quad(s[1], s[3], s[5], t);
      cairo_move_to(cr, x, y); cairo_line_to(cr, x, deck); cairo_stroke(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, .75);
    hex(cr, orange);
    cairo_set_line_width(cr, lw * 2.6);
    cairo_move_to(cr, s[0], s[1]); quad_to(cr, s[0], s[1], s[2], s[3], s[4], s[5]); cairo_stroke(cr);
  }
  double xs[2] = { x1, x2 };
  for (int k = 0; k < 2; k++) {
    double x = xs[k], leg = W * .0085, gap = W * .011, bot = hz + H * .02;
    hex(cr, orange);
    for (int sg = -1; sg <= 1; sg += 2) {
      double lx = x + sg * gap / 2 - (sg < 0 ? leg : 0);
      cairo_move_to(cr, lx + leg * .12, top); cairo_line_to(cr, lx + leg * .88, top);
      cairo_line_to(cr, lx + leg, bot); cairo_line_to(cr, lx, bot); cairo_close_path(cr);
      cairo_fill(cr);
    }
    hex(cr, orangeD);
    double fs[4] = { 0, .17, .36, .58 };
    for (int i = 0; i < 4; i++)
      fill_rect(cr, x - gap / 2, top + (deck - top) * fs[i] + (i ? 0 : H * .006), gap, H * (i ? .012 : .02));
    fill_rect(cr, x - gap / 2 - leg, deck - H * .004, gap + leg * 2, H * .01);
  }
  hex(cr, orangeD); fill_rect(cr, W * .09, deck, W, H * .012);
  hex(cr, orange); fill_rect(cr, W * .09, deck - H * .003, W, H * .005);
  if (dark) {
    cairo_set_source_rgba(cr, 1, 214 / 255.0, 150 / 255.0, .95);
    for (double x = W * .1; x < W; x += W / 70) fill_rect(cr, x, deck - H * .006, fmax(1.5, W / 900), fmax(1.5, W / 900));
    for (int k = 0; k < 2; k++) {
      cairo_set_source_rgba(cr, 1, 90 / 255.0, 80 / 255.0, .95);
      cairo_new_path(cr);
      cairo_arc(cr, xs[k], top - H * .004, fmax(2, W / 700), 0, 2 * M_PI);
      cairo_fill(cr);
    }
  }

  /* Spiegelung: Streifen über dem Horizont, gestaucht und gewellt ins Wasser kopiert */
  cairo_surface_flush(target);
  cairo_surface_t *snap = cairo_surface_create_similar_image(target, CAIRO_FORMAT_RGB24, (int)W, (int)ceil(hz) + 1);
  cairo_t *sc = cairo_create(snap);
  cairo_set_source_surface(sc, target, 0, 0);
  cairo_paint(sc);
  cairo_destroy(sc);
  int step = (int)fmax(2, round(H / 400));
  for (double y = hz; y < H; y += step) {
    double d = y - hz, sy2 = hz - d * 1.05;
    if (sy2 < 0) break;
    double a = fmax(0, .24 - d / (H - hz) * .3);
    double dx = sin(y * .11) * W * .0012 * (1 + d / H * 4);
    cairo_save(cr);
    cairo_rectangle(cr, dx, y, W, step);
    cairo_clip(cr);
    cairo_set_source_surface(cr, snap, dx, y - sy2);
    cairo_paint_with_alpha(cr, a);
    cairo_restore(cr);
  }
  cairo_surface_destroy(snap);
  if (!dark) {
    g = cairo_pattern_create_radial(sx, hz + H * .05, 0, sx, hz + H * .05, H * .25);
    stop(g, 0, 0xfff0d7, .45); stop(g, 1, 0xfff0d7, 0);
    cairo_set_source(cr, g); fill_rect(cr, 0, hz, W, H - hz); cairo_pattern_destroy(g);
  }

  /* Nebel ("Karl the Fog") */
  for (int i = 0; i < 9; i++) {
    double fx = W * (-.1 + i * .14 + rnd() * .05), fy = deck + H * (.01 + rnd() * .06);
    double rx = W * (.16 + rnd() * .12), ry = H * (.035 + rnd() * .03);
    cairo_save(cr);
    cairo_translate(cr, fx, fy);
    cairo_scale(cr, 1, ry / rx);
    g = cairo_pattern_create_radial(0, 0, 0, 0, 0, rx);
    if (dark) stop(g, 0, 0xd2aae6, .22); else stop(g, 0, 0xffffff, .62);
    stop(g, 1, 0xffffff, 0);
    cairo_set_source(cr, g);
    cairo_new_path(cr);
    cairo_arc(cr, 0, 0, rx, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_restore(cr);
  }
  g = cairo_pattern_create_radial(W / 2, H * .45, fmin(W, H) * .3, W / 2, H / 2, fmax(W, H) * .8);
  stop(g, 0, 0x000000, 0);
  if (dark) stop(g, 1, 0x00000a, .45); else stop(g, 1, 0x141e3c, .14);
  cairo_set_source(cr, g); fill_rect(cr, 0, 0, W, H); cairo_pattern_destroy(g);
}

/* Standardhintergrund: Papercut-Schichten in Rosa, dunkel in Pflaume (wallpapers/make.swift erzeugt beide
 * aus der Vorlage, overlay-desktop/Library/ClokeOS/Wallpapers/{light,dark}.png). Das Bild füllt den
 * Bildschirm ("cover", aus der Mitte beschnitten). Fehlt die Datei, bleibt der alte Hintergrund
 * "Goldenes Tor". */
static void wall_draw_default(cairo_t *cr, double W, double H, bool dark) {
  static cairo_surface_t *img[2];
  int k = dark ? 1 : 0;
  if (!img[k]) {
    cairo_surface_t *s = cairo_image_surface_create_from_png(dark ? "/Library/ClokeOS/Wallpapers/dark.png"
                                                                  : "/Library/ClokeOS/Wallpapers/light.png");
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(s);
      s = NULL;
    }
    img[k] = s ? s : (cairo_surface_t *)1;
  }
  if (img[k] == (cairo_surface_t *)1) {
    wall_draw_bridge(cr, W, H, dark);
    return;
  }
  double iw = cairo_image_surface_get_width(img[k]), ih = cairo_image_surface_get_height(img[k]);
  double sc = fmax(W / iw, H / ih);
  cairo_save(cr);
  cairo_translate(cr, (W - iw * sc) / 2, (H - ih * sc) / 2);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, img[k], 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
  cairo_paint(cr);
  cairo_restore(cr);
}

/* ---------------------------------------------------------------------------------------- */
/* Auswahl: das Standardbild, vier gezeichnete Motive aus dem Prototyp (drawWall in clokeos.html)  */
/* und die Bilder von Pixel Makes Art (instagram.com/pixelmakesart, mit Erlaubnis).               */

static const struct {
  const char *key;
  unsigned light_bg[2], light_f[7], dark_bg[2], dark_f[7];
} PROC[4] = {
  {"velvet", {0xcdbcff, 0xf4d8ea}, {0x9a78ff, 0xb993ff, 0xe79fd2, 0x7553e6, 0xf2b7da, 0x8c63f2, 0xd98ccd},
   {0x120c25, 0x2b1236}, {0x3c2190, 0x5a2aa0, 0x7e2b76, 0x26155f, 0x9a3d80, 0x4a238f, 0x6c2470}},
  {"dusk", {0xffd6b5, 0xffb2a6}, {0xff9468, 0xf0667e, 0xffb874, 0xd9557e, 0xffc9a0, 0xe8746a, 0xf59a8c},
   {0x1a0f1d, 0x3b1521}, {0x7a2539, 0xa8343f, 0x5c1b3b, 0xc45a3b, 0x41162f, 0x8e2c3d, 0x6a1f37}},
  {"tide", {0xc2e7f3, 0xd9dcff}, {0x64b6db, 0x7d8df0, 0x52d2c6, 0x6b80e1, 0xa6d9f4, 0x5aa0e0, 0x88b8f0},
   {0x06141f, 0x0f1739}, {0x0f4b6c, 0x26348b, 0x0d5d5b, 0x1b2b6f, 0x1f6f8f, 0x143d7a, 0x0e5470}},
  {"graphite", {0xe4e2e8, 0xc9c6d0}, {0xb4b0bd, 0x9d98a8, 0xcfccd6, 0x8a8597, 0xbdb9c6, 0xa6a2b0, 0x94909f},
   {0x121115, 0x24222a}, {0x2e2c35, 0x3a3742, 0x26242c, 0x45424e, 0x302e37, 0x3d3a45, 0x2a2830}},
};
static const char *const PROC_TITLES[4] = {"Velvet", "Dusk", "Tide", "Graphite"};
#define N_ART 13
#define WALL_N (1 + 4 + N_ART)

static const char *const ART_TITLES[N_ART] = {"Rainbow Chalk", "Smile Collage", "Halftone Bloom", "Hatch Waves", "Stamp Circles",
                                              "Violet Dust", "Bokeh", "Neon Streaks", "Neon Glow", "Light Streaks",
                                              "Smile Flower", "Green Folds", "Violet Arches"};

int wall_sel; /* Index; 0 = Standard */
int wall_count(void) { return WALL_N; }

const char *wall_title(int i) {
  if (i == 0) return "ClokeOS";
  if (i <= 4) return PROC_TITLES[i - 1];
  return ART_TITLES[i - 5];
}

const char *wall_key(int i) {
  static char k[N_ART][8];
  if (i == 0) return "default";
  if (i <= 4) return PROC[i - 1].key;
  snprintf(k[i - 5], sizeof k[0], "art%02d", i - 4);
  return k[i - 5];
}

int wall_find(const char *key) {
  for (int i = 0; i < WALL_N; i++)
    if (!strcmp(key, wall_key(i))) return i;
  return 0;
}

static void wall_draw_proc(cairo_t *cr, double W, double H, bool dark, int k) {
  const unsigned *bg = dark ? PROC[k].dark_bg : PROC[k].light_bg, *f = dark ? PROC[k].dark_f : PROC[k].light_f;
  cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, W * .6, H);
  stop(g, 0, bg[0], 1);
  stop(g, 1, bg[1], 1);
  cairo_set_source(cr, g);
  fill_rect(cr, 0, 0, W, H);
  cairo_pattern_destroy(g);
  double step = fmax(2, W / 120);
  for (int i = 0; i < 7; i++) {
    double y0 = H * (.16 + i * .115), amp = H * (.07 + .025 * sin(i * 1.7)), fr = 1.05 + i * .21, ph = i * 1.37;
#define YAT(x) (y0 + sin((x) / W * M_PI * fr + ph) * amp + sin((x) / W * M_PI * 3.3 + ph * 2) * amp * .3)
    cairo_new_path(cr);
    cairo_move_to(cr, 0, H);
    for (double x = 0; x <= W + step; x += step) cairo_line_to(cr, x, YAT(x));
    cairo_line_to(cr, W, H);
    cairo_close_path(cr);
    cairo_push_group(cr);
    cairo_pattern_t *lg = cairo_pattern_create_linear(0, y0 - amp, 0, y0 + H * .5);
    stop(lg, 0, f[i % 7], 1);
    stop(lg, 1, bg[1], 1);
    cairo_set_source(cr, lg);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(lg);
    cairo_new_path(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, .55 + i * .05);
    cairo_new_path(cr);
    bool first = true;
    for (double x = 0; x <= W + step; x += step) {
      double y = YAT(x) + 1;
      if (first) cairo_move_to(cr, x, y), first = false;
      else cairo_line_to(cr, x, y);
    }
    cairo_set_source_rgba(cr, 1, 1, 1, dark ? .16 : .38);
    cairo_set_line_width(cr, fmax(1, W / 1100));
    cairo_stroke(cr);
  }
  g = cairo_pattern_create_radial(W / 2, H * .42, fmin(W, H) * .2, W / 2, H / 2, fmax(W, H) * .78);
  stop(g, 0, 0x000000, 0);
  if (dark) stop(g, 1, 0x000000, .45);
  else stop(g, 1, 0x28143c, .12);
  cairo_set_source(cr, g);
  fill_rect(cr, 0, 0, W, H);
  cairo_pattern_destroy(g);
}

/* Bilddatei, aus der Mitte beschnitten ("cover") */
static bool wall_draw_file(cairo_t *cr, double W, double H, const char *path) {
  cairo_surface_t *s = cairo_image_surface_create_from_png(path);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return false;
  }
  double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
  double sc = fmax(W / iw, H / ih);
  cairo_save(cr);
  cairo_translate(cr, (W - iw * sc) / 2, (H - ih * sc) / 2);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, s, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_surface_destroy(s);
  return true;
}

static void wall_draw_idx(cairo_t *cr, double W, double H, bool dark, int idx, bool thumb) {
  char p[96];
  if (idx >= 1 && idx <= 4) return wall_draw_proc(cr, W, H, dark, idx - 1);
  if (idx >= 5) {
    snprintf(p, sizeof p, "/Library/ClokeOS/Wallpapers/%s%s.png", wall_key(idx), thumb ? "-t" : "");
    if (wall_draw_file(cr, W, H, p)) return;
  } else if (thumb) {
    snprintf(p, sizeof p, "/Library/ClokeOS/Wallpapers/%s-t.png", dark ? "dark" : "light");
    if (wall_draw_file(cr, W, H, p)) return;
  }
  wall_draw_default(cr, W, H, dark);
}

void wall_draw(cairo_t *cr, double W, double H, bool dark) { wall_draw_idx(cr, W, H, dark, wall_sel, false); }

/* Vorschau für die Systemeinstellungen, einmal gezeichnet und gemerkt */
void wall_thumb(cairo_t *cr, int idx, double x, double y, double w, double h, bool dark) {
  static cairo_surface_t *cache[WALL_N][2];
  int k = dark ? 1 : 0;
  if (!cache[idx][k]) {
    cache[idx][k] = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 160, 100);
    cairo_t *c = cairo_create(cache[idx][k]);
    wall_draw_idx(c, 160, 100, dark, idx, true);
    cairo_destroy(c);
  }
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, w / 160, h / 100);
  cairo_set_source_surface(cr, cache[idx][k], 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}
