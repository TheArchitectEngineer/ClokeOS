/* CloakServer: der Fensterserver von ClokeOS. Gemeinsame Deklarationen. */
#ifndef CLOAKSERVER_H
#define CLOAKSERVER_H

#include <cairo.h>
#include <stdbool.h>

/* wall.c: Hintergrundbild "Goldenes Tor" aus clokeos.html */
void wall_draw(cairo_t *cr, double W, double H, bool dark);

#endif
