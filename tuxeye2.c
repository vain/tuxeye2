#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    struct FFImage ff;
    double center, radius;
};

struct Images
{
    struct FFImage canvas, bg, fg;
    struct Mover moving[2];
} pics;

void
create_window(void)
{
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
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
}

void
create_images(void)
{
    ff_load("themes/tux/bg.png.ff", &pics.bg);
    ff_load("themes/tux/fg.png.ff", &pics.fg);
    ff_load("themes/tux/moving1.png.ff", &pics.moving[0].ff);
    ff_load("themes/tux/moving2.png.ff", &pics.moving[1].ff);

    pics.canvas.width = pics.bg.width;
    pics.canvas.height = pics.bg.height;
    ff_init_empty(&pics.canvas);
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
    fprintf(stderr, "%d %d\n", tx, ty);

    ff_clear(&pics.canvas, 1);
    ff_overlay(&pics.canvas, &pics.bg);
    ff_overlay(&pics.canvas, &pics.fg);
    ximg = ff_to_ximage(&pics.canvas, dpy, screen);

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
