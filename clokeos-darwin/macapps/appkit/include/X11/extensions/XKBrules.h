/* ClokeOS: PureDarwin hat kein libxkbfile. Darlings X11-Backends brauchen daraus nur
 * XkbRF_GetNamesProp (Name der Tastaturbelegung). Struktur wie in libxkbfile, Umsetzung in
 * macapps/appkit/xkbrules.c. */
#ifndef CLOKE_XKBRULES_H
#define CLOKE_XKBRULES_H
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

typedef struct _XkbRF_VarDefs {
    char *model;
    char *layout;
    char *variant;
    char *options;
    unsigned short sz_extra;
    unsigned short num_extra;
    char *extra_names;
    char **extra_values;
} XkbRF_VarDefsRec, *XkbRF_VarDefsPtr;

#define _XKB_RF_NAMES_PROP_ATOM "_XKB_RULES_NAMES"

_XFUNCPROTOBEGIN
Bool XkbRF_GetNamesProp(Display *dpy, char **rules_file_rtrn, XkbRF_VarDefsPtr var_defs_rtrn);
_XFUNCPROTOEND
#endif
