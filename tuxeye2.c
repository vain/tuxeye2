#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <X11/extensions/shape.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "libff.h"

#define FPS 30

Atom atom_delete, atom_protocols;
Display *dpy;
int screen, last_x, last_y;
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
create_window(bool use_location, int x, int y)
{
    Atom atom_motif;
    long int mwm_hints[] = { 0x2, 0x0, 0x0, 0x0, 0x0 };
    XSetWindowAttributes wa = {
        .override_redirect = False,
        .background_pixmap = ParentRelative,
        .event_mask = ButtonReleaseMask | ExposureMask,
    };
    XSizeHints sh = {
        .flags = PMinSize | PMaxSize,
        .min_width = pics.bg.width,
        .max_width = pics.bg.width,
        .min_height = pics.bg.height,
        .max_height = pics.bg.height,
    };

    if (use_location)
        wa.override_redirect = True;

    win = XCreateWindow(dpy, root, 0, 0, pics.bg.width, pics.bg.height, 0,
                        DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWOverrideRedirect | CWBackPixmap | CWEventMask,
                        &wa);
    atom_motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, win, atom_motif, atom_motif, 32, PropModeReplace,
                    (unsigned char *)&mwm_hints, 5);
    XSetWMNormalHints(dpy, win, &sh);
    if (use_location)
        XMoveWindow(dpy, win, x, y);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);

    atom_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &atom_delete, 1);
}

void
create_images(void)
{
    FILE *fp = NULL;
    int a, b, c, d;

    /* TODO Make number of movers a theme option */
    /* TODO Make the actual theme ("tux" or something else) an option */

    die_false(ff_load(THEME_PATH "/themes/tux/mask.ff", &pics.mask));
    die_false(ff_load(THEME_PATH "/themes/tux/bg.ff", &pics.bg));
    die_false(ff_load(THEME_PATH "/themes/tux/fg.ff", &pics.fg));
    die_false(ff_load(THEME_PATH "/themes/tux/moving1.ff", &pics.moving[0].img));
    die_false(ff_load(THEME_PATH "/themes/tux/moving2.ff", &pics.moving[1].img));

    fp = fopen(THEME_PATH "/themes/tux/positions", "r");
    die_false_msg(fp != NULL, "Could not open 'positions' from theme");

    a = fscanf(fp, "%lf %lf\n", &pics.moving[0].center_x, &pics.moving[0].center_y);
    b = fscanf(fp, "%lf\n", &pics.moving[0].radius);
    c = fscanf(fp, "%lf %lf\n", &pics.moving[1].center_x, &pics.moving[1].center_y);
    d = fscanf(fp, "%lf\n", &pics.moving[1].radius);
    fclose(fp);

    die_false_msg(a == 2 && b == 1 && c == 2 && d == 1,
                  "Malformed 'positions' in themes");

    die_false(ff_init_empty(&pics.canvas, pics.bg.width, pics.bg.height));
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
    if (tx == last_x && ty == last_y)
        return;

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

    last_x = tx;
    last_y = ty;
}

void
show_position(void)
{
    Window dummy;
    int x, y;
    unsigned int dui;

    XGetGeometry(dpy, win, &dummy, &x, &y, &dui, &dui, &dui, &dui);
    printf("%d %d\n", x, y);
}

int
main(int argc, char **argv)
{
    XEvent ev;
    XClientMessageEvent *cm;
    XButtonEvent *be;
    int x = 0, y = 0, xfd, sret;
    bool use_location = false;
    fd_set fds;
    struct timeval timeout;

    if (argc == 3)
    {
        x = atoi(argv[1]);
        y = atoi(argv[2]);
        use_location = true;
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "Could not open X display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XSelectInput(dpy, root, SubstructureNotifyMask);

    create_images();
    create_window(use_location, x, y);
    create_mask();

    /* The xlib docs say: On a POSIX system, the connection number is
     * the file descriptor associated with the connection. */
    xfd = ConnectionNumber(dpy);

    XSync(dpy, False);
    for (;;)
    {
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 1000000 / FPS;

        sret = select(xfd + 1, &fds, NULL, NULL, &timeout);
        if (sret == -1)
        {
            perror("select() returned with error");
            exit(EXIT_FAILURE);
        }
        else if (sret == 0)
            /* No FDs ready, means timeout expired. */
            update();

        while (XPending(dpy))
        {
            XNextEvent(dpy, &ev);
            switch (ev.type)
            {
                case Expose:
                    update();
                    break;
                case ClientMessage:
                    cm = &ev.xclient;
                    if (cm->message_type == atom_protocols &&
                        (Atom)cm->data.l[0] == atom_delete)
                    {
                        exit(EXIT_SUCCESS);
                    }
                    break;
                case ButtonRelease:
                    be = &ev.xbutton;
                    if (be->button == Button1)
                        show_position();
                    else if (be->button == Button3)
                        exit(EXIT_SUCCESS);
                    break;
                default:
                    /* Auto-raise, requires selecting for SubstructureNotify. */
                    if (use_location)
                        XRaiseWindow(dpy, win);
            }
        }
    }
}
