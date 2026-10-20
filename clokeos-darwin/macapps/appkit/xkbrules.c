/* ClokeOS: XkbRF_GetNamesProp aus libxkbfile, die PureDarwin fehlt (siehe
 * include/X11/extensions/XKBrules.h). Liest die Eigenschaft _XKB_RULES_NAMES am Wurzelfenster:
 * fünf durch NUL getrennte Zeichenketten (Regeln, Modell, Belegung, Variante, Optionen). Die
 * zurückgegebenen Zeichenketten gehören dem Aufrufer (malloc), wie bei libxkbfile. */
#include <X11/Xatom.h>
#include <X11/extensions/XKBrules.h>
#include <stdlib.h>
#include <string.h>

Bool XkbRF_GetNamesProp(Display *dpy, char **rules_file_rtrn, XkbRF_VarDefsPtr vd) {
    Atom rules = XInternAtom(dpy, _XKB_RF_NAMES_PROP_ATOM, True);
    Atom type;
    int fmt;
    unsigned long n = 0, extra = 0;
    unsigned char *data = NULL;

    if (rules_file_rtrn != NULL)
        *rules_file_rtrn = NULL;
    if (vd != NULL)
        memset(vd, 0, sizeof(*vd));
    if (rules == None)
        return False;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), rules, 0, 1024, False, XA_STRING, &type,
                           &fmt, &n, &extra, &data) != Success || data == NULL)
        return False;
    if (type != XA_STRING || fmt != 8 || n == 0) {
        XFree(data);
        return False;
    }

    char *fields[5] = {NULL};
    unsigned long pos = 0;
    for (int i = 0; i < 5 && pos < n; i++) {
        const char *s = (const char *) data + pos;
        size_t len = strnlen(s, n - pos);
        if (len > 0)
            fields[i] = strndup(s, len);
        pos += len + 1;
    }
    XFree(data);

    if (rules_file_rtrn != NULL)
        *rules_file_rtrn = fields[0];
    else
        free(fields[0]);
    if (vd != NULL) {
        vd->model = fields[1];
        vd->layout = fields[2];
        vd->variant = fields[3];
        vd->options = fields[4];
    } else {
        for (int i = 1; i < 5; i++)
            free(fields[i]);
    }
    return True;
}
