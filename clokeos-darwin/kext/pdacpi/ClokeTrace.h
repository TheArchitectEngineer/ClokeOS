/*
 * Bildschirmausgabe für den Mehrkernstart. Nach initialize_screen(op 6) zeigt der Kernel keinen Text und
 * keine Panik mehr; ein Stillstand sieht dann von außen immer gleich aus. Diese Zeilen malt der Treiber
 * selbst in den Framebuffer (unten links, 5x7-Schrift in doppelter Größe), mit Boot-Arg cloke_trace=1 oder
 * über cloke_trace_show(), wenn der Mehrkernstart auf einen Kern zurückfällt. Die Zeilen werden immer gesammelt.
 */
#ifndef CLOKE_TRACE_H
#define CLOKE_TRACE_H
#ifdef __cplusplus
extern "C" {
#endif
void cloke_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void cloke_trace_show(unsigned seconds);
#ifdef __cplusplus
}
#endif
#endif
