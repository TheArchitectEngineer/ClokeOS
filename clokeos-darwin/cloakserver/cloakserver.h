/* CloakServer: der Fensterserver von ClokeOS. Gemeinsame Deklarationen. */
#ifndef CLOAKSERVER_H
#define CLOAKSERVER_H

#include <X11/Xlib.h>
#include <cairo.h>
#include <stdbool.h>

/* wall.c: Hintergrundbild "Goldenes Tor" aus clokeos.html */
void wall_draw(cairo_t *cr, double W, double H, bool dark);

/* cloakserver.c */
void rrect(cairo_t *cr, double x, double y, double w, double h, double r);

/* Wie der Compositor ein Fenster zeigt: Rahmen und Glas mit runden Ecken und Schatten */
enum { WK_PLAIN, WK_FRAME, WK_GLASS };
typedef struct {
  int kind;
  double radius;
  int blur, offset; /* Schatten: Unschärfe und Versatz nach unten in Pixeln, 0 = keiner */
  double alpha;
} WinStyle;
WinStyle cs_window_style(Window w);
Pixmap cs_wallpaper(void);

/* comp.c: Compositor (Composite, Damage, XFixes) */
extern bool comp_on;
extern Visual *comp_argb;
extern Colormap comp_argb_cmap;
bool comp_init(Display *dpy, int scr, Window root, int sw, int sh);
void comp_handle(XEvent *ev);
void comp_dirty(int x, int y, int w, int h);
void comp_dirty_all(void);
void comp_repaint(void);
bool comp_quiet_error(XErrorEvent *e);

#endif
