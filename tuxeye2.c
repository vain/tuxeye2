#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "libff.h"

Display *dpy;
int screen;
Window root, win;
GC gc;

struct Mover
{
    struct FFImage img;
    double center_x, center_y, radius;
};

struct Images
{
    struct FFImage canvas, bg, fg, mask;
    struct Mover moving[2];
} pics;

void
die_false(bool v)
{
    if (!v)
        exit(EXIT_FAILURE);
}

void
die_false_msg(bool v, char *msg)
{
    if (!v)
    {
        fprintf(stderr, "%s\n", msg);
        exit(EXIT_FAILURE);
    }
}

void
create_window(void)
{
    Atom atom_motif;
    uint32_t mwm_hints[] = { 0x2, 0x0, 0x0, 0x0, 0x0 };
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask,
    };

    win = XCreateWindow(dpy, root, 0, 0, 800, 600, 0,
                        DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWBackPixmap | CWEventMask,
                        &wa);
    atom_motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, win, atom_motif, atom_motif, 32, PropModeReplace,
                    (unsigned char *)&mwm_hints, 5);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
}

void
create_images(void)
{
    FILE *fp = NULL;
    int a, b, c, d;

    /* TODO Make number of movers a theme option */

    die_false(ff_load(THEME_PATH "themes/tux/mask.ff", &pics.mask));
    die_false(ff_load(THEME_PATH "themes/tux/bg.ff", &pics.bg));
    die_false(ff_load(THEME_PATH "themes/tux/fg.ff", &pics.fg));
    die_false(ff_load(THEME_PATH "themes/tux/moving1.ff", &pics.moving[0].img));
    die_false(ff_load(THEME_PATH "themes/tux/moving2.ff", &pics.moving[1].img));

    fp = fopen(THEME_PATH "themes/tux/positions", "r");
    die_false_msg(fp != NULL, "Could not open 'positions' from theme");

    a = fscanf(fp, "%lf %lf\n", &pics.moving[0].center_x, &pics.moving[0].center_y);
    b = fscanf(fp, "%lf\n", &pics.moving[0].radius);
    c = fscanf(fp, "%lf %lf\n", &pics.moving[1].center_x, &pics.moving[1].center_y);
    d = fscanf(fp, "%lf\n", &pics.moving[1].radius);
    fclose(fp);

    die_false_msg(a == 2 && b == 1 && c == 2 && d == 1,
                  "Malformed 'positions' in themes");

    pics.canvas.width = pics.bg.width;
    pics.canvas.height = pics.bg.height;
    die_false(ff_init_empty(&pics.canvas));
}

void
create_mask(void)
{
    XImage *ximg;
    Pixmap mask_pm;
    GC mono_gc;

    ximg = ff_to_ximage_mono(&pics.mask, dpy, screen);
    die_false(ximg != NULL);
    mask_pm = XCreatePixmap(dpy, win, pics.mask.width, pics.mask.height, 1);
    mono_gc = XCreateGC(dpy, mask_pm, 0, NULL);
    XPutImage(dpy, mask_pm, mono_gc, ximg,
              0, 0, 0, 0,
              pics.mask.width, pics.mask.height);
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask_pm, ShapeSet);
    XFreePixmap(dpy, mask_pm);
    XDestroyImage(ximg);
    XFreeGC(dpy, mono_gc);
}

void
overlay_mover(struct FFImage *canvas, struct Mover *mover, int x, int y)
{
    double dx, dy, ld;
    int mx, my;

    /* x, y is the mouse position relative to the canvas' origin (top
     * left corner). Translate this to a position relative to this
     * mover's center. */
    dx = x - mover->center_x;
    dy = y - mover->center_y;

    /* Normalize vector from mover center to mouse and rescale, but only
     * if the mouse is outside of the desired radius. */
    ld = sqrt(dx * dx + dy * dy);
    if (ld > mover->radius)
    {
        dx /= ld;
        dy /= ld;
        dx *= mover->radius;
        dy *= mover->radius;
    }

    /* New center of this mover. */
    mx = mover->center_x + dx;
    my = mover->center_y + dy;

    /* New top left corner of this mover. */
    mx -= mover->img.width * 0.5;
    my -= mover->img.height * 0.5;

    ff_overlay(canvas, &mover->img, mx, my);
}

void
update(void)
{
    int x, y, di, tx, ty;
    unsigned int dui;
    Window dummy;
    XImage *ximg;

    XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui);
    XTranslateCoordinates(dpy, root, win, x, y, &tx, &ty, &dummy);

    ff_clear(&pics.canvas, 1);
    ff_overlay(&pics.canvas, &pics.bg, 0, 0);
    overlay_mover(&pics.canvas, &pics.moving[0], tx, ty);
    overlay_mover(&pics.canvas, &pics.moving[1], tx, ty);
    ff_overlay(&pics.canvas, &pics.fg, 0, 0);
    ximg = ff_to_ximage(&pics.canvas, dpy, screen);
    die_false(ximg != NULL);

    XPutImage(dpy, win, gc, ximg,
              0, 0, 0, 0,
              pics.canvas.width, pics.canvas.height);

    /* Note: This also frees the data that was initially passed to
     * XCreateImage(). */
    XDestroyImage(ximg);
}

int
main()
{
    XEvent ev;
    unsigned char mask_bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof mask_bits, mask_bits };

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "Could not open X display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XISetMask(mask.mask, XI_RawMotion);
    XISelectEvents(dpy, root, &mask, 1);

    create_images();
    create_window();
    create_mask();

    for (;;)
    {
        XNextEvent(dpy, &ev);
        switch (ev.type)
        {
            case Expose:
            case GenericEvent:
                update();
                break;
        }
    }
}
