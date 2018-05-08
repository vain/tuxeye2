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

#define PATH_SZ 256
#define FPS 30

Atom atom_delete, atom_protocols;
Display *dpy;
int screen, last_x, last_y;
unsigned int win_w, win_h, last_win_w, last_win_h;
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
    int num_movers;
    struct Mover *moving;
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
    long int mwm_hints[] = { 0x2, 0x0, 0x0, 0x0, 0x0 };
    XSetWindowAttributes wa = {
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask | StructureNotifyMask,
    };
    XClassHint ch = {
        .res_class = "Tuxeye2",
        .res_name = "tuxeye2",
    };
    XSizeHints sh = {
        .flags = PMinSize | PMaxSize,
        .min_width = pics.bg.width,
        .max_width = pics.bg.width,
        .min_height = pics.bg.height,
        .max_height = pics.bg.height,
    };

    win = XCreateWindow(dpy, root, 0, 0, pics.bg.width, pics.bg.height, 0,
                        DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWBackPixmap | CWEventMask,
                        &wa);
    atom_motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, win, atom_motif, atom_motif, 32, PropModeReplace,
                    (unsigned char *)&mwm_hints, 5);
    XSetClassHint(dpy, win, &ch);
    XSetWMNormalHints(dpy, win, &sh);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);

    atom_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &atom_delete, 1);
}

char *
make_path(char *theme, char *filename, char *target_buf, size_t len)
{
    snprintf(target_buf, len - 1, "%s/themes/%s/%s", THEME_PATH, theme,
             filename);
    target_buf[len - 1] = 0;
    return target_buf;
}

void
create_images(char *theme)
{
    FILE *fp = NULL;
    char buf[2 * PATH_SZ] = "", buf_fname[PATH_SZ] = "";
    int tokens;
    int i;

    fp = fopen(make_path(theme, "movers", buf, sizeof buf), "r");
    die_false_msg(fp != NULL, "Could not open 'movers' from theme");
    tokens = fscanf(fp, "%d\n", &pics.num_movers);
    die_false_msg(tokens == 1 && pics.num_movers > 0,
                  "Malformed 'movers' in theme");
    fclose(fp);

    die_false(ff_load(make_path(theme, "mask.ff", buf, sizeof buf), &pics.mask));
    die_false(ff_load(make_path(theme, "bg.ff", buf, sizeof buf), &pics.bg));
    die_false(ff_load(make_path(theme, "fg.ff", buf, sizeof buf), &pics.fg));

    pics.moving = calloc(pics.num_movers, sizeof (struct Mover));
    die_false_msg(pics.moving != NULL, "Could not allocate memory for movers");
    for (i = 0; i < pics.num_movers; i++)
    {
        snprintf(buf_fname, sizeof buf_fname, "moving%d.ff", i);
        die_false(ff_load(make_path(theme, buf_fname, buf, sizeof buf),
                  &pics.moving[i].img));
    }

    fp = fopen(make_path(theme, "positions", buf, sizeof buf), "r");
    die_false_msg(fp != NULL, "Could not open 'positions' from theme");
    for (i = 0; i < pics.num_movers; i++)
    {
        tokens = fscanf(fp, "%lf %lf\n", &pics.moving[i].center_x,
                        &pics.moving[i].center_y);
        die_false_msg(tokens == 2, "Malformed 'positions' (center) in theme");

        tokens = fscanf(fp, "%lf\n", &pics.moving[i].radius);
        die_false_msg(tokens == 1, "Malformed 'positions' (radius) in theme");
    }
    fclose(fp);

    die_false(ff_init_empty(&pics.canvas, pics.bg.width, pics.bg.height));
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
update_mask(bool force_update)
{
    XImage *ximg;
    Pixmap mask_pm;
    GC mono_gc;
    int di;
    unsigned int dui;
    Window dummy;

    XGetGeometry(dpy, win, &dummy, &di, &di, &win_w, &win_h, &dui, &dui);
    if (!force_update && win_w == last_win_w && win_h == last_win_h)
        return;

    ximg = ff_to_ximage_mono(&pics.mask, dpy, screen);
    die_false(ximg != NULL);
    mask_pm = XCreatePixmap(dpy, win, win_w, win_h, 1);
    mono_gc = XCreateGC(dpy, mask_pm, 0, NULL);
    XSetForeground(dpy, mono_gc, 0);
    XFillRectangle(dpy, mask_pm, mono_gc, 0, 0, win_w, win_h);
    XPutImage(dpy, mask_pm, mono_gc, ximg,
              0, 0,
              (win_w - pics.mask.width) * 0.5,
              (win_h - pics.mask.height) * 0.5,
              pics.mask.width, pics.mask.height);
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask_pm, ShapeSet);
    XFreePixmap(dpy, mask_pm);
    XDestroyImage(ximg);
    XFreeGC(dpy, mono_gc);

    last_win_w = win_w;
    last_win_h = win_h;
}

void
update(bool force_update)
{
    int x, y, di, tx, ty, i;
    unsigned int dui;
    Window dummy;
    XImage *ximg;

    XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui);
    XTranslateCoordinates(dpy, root, win, x, y, &tx, &ty, &dummy);

    /* Account for: Tux is centered in the window. */
    tx -= (win_w - pics.canvas.width) * 0.5;
    ty -= (win_h - pics.canvas.height) * 0.5;

    if (!force_update && tx == last_x && ty == last_y)
        return;

    ff_clear(&pics.canvas, 1);
    ff_overlay(&pics.canvas, &pics.bg, 0, 0);
    for (i = 0; i < pics.num_movers; i++)
        overlay_mover(&pics.canvas, &pics.moving[i], tx, ty);
    ff_overlay(&pics.canvas, &pics.fg, 0, 0);
    ximg = ff_to_ximage(&pics.canvas, dpy, screen);
    die_false(ximg != NULL);

    XPutImage(dpy, win, gc, ximg,
              0, 0,
              (win_w - pics.canvas.width) * 0.5,
              (win_h - pics.canvas.height) * 0.5,
              pics.canvas.width, pics.canvas.height);

    /* Note: This also frees the data that was initially passed to
     * XCreateImage(). */
    XDestroyImage(ximg);

    last_x = tx;
    last_y = ty;
}

int
main(int argc, char **argv)
{
    XEvent ev;
    XClientMessageEvent *cm;
    int xfd, sret;
    fd_set fds;
    struct timeval timeout;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "Could not open X display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    if (argc == 2)
        create_images(argv[1]);
    else
        create_images("tux");
    create_window();
    update_mask(true);

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
            update(false);

        while (XPending(dpy))
        {
            XNextEvent(dpy, &ev);
            switch (ev.type)
            {
                case Expose:
                    update(true);
                    break;
                case ConfigureNotify:
                    update_mask(false);
                    break;
                case ClientMessage:
                    cm = &ev.xclient;
                    if (cm->message_type == atom_protocols &&
                        (Atom)cm->data.l[0] == atom_delete)
                    {
                        exit(EXIT_SUCCESS);
                    }
                    break;
            }
        }
    }
}
