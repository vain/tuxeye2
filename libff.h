#ifndef LIBFF_H
#define LIBFF_H

/* libff
 *
 * A very slim library to work with Farbfeld images.
 *
 * Upstream, format reference: http://tools.suckless.org/farbfeld/ */

#define __FF__ "libff"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

struct FFImage
{
    uint32_t width, height;
    uint16_t *data;
};

/* Reset all pixels to black, alpha channel can be customized. 'alpha'
 * must be 0 <= alpha <= 1 (0 being fully transparent, 1 being opaque). */
void
ff_clear(struct FFImage *image, double alpha)
{
    uint32_t x, y;

    memset(image->data, 0, image->width * image->height * 4 * sizeof (uint16_t));

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            image->data[(y * image->width + x) * 4 + 3] = alpha * 65535;
}

/* Initialize a struct FFImage of the given size. Careful, discards any
 * existing data. */
bool
ff_init_empty(struct FFImage *image, uint32_t w, uint32_t h)
{
    image->width = w;
    image->height = h;
    image->data = calloc(image->width * image->height * 4, sizeof (uint16_t));
    if (!image->data)
    {
        fprintf(stderr, __FF__": Could not allocate memory in ff_init_empty\n");
        return false;
    }
    return true;
}

/* Load a farbfeld image from a file. */
bool
ff_load(char *path, struct FFImage *image)
{
    FILE *fp = NULL;
    uint32_t hdr[4], x, y;

    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, __FF__": Could not open image file '%s'\n", path);
        goto errout;
    }

    if (fread(hdr, sizeof (uint32_t), 4, fp) != 4)
    {
        fprintf(stderr, __FF__": Could not read farbfeld header from '%s'\n",
                path);
        goto errout;
    }

    if (memcmp("farbfeld", hdr, (sizeof "farbfeld") - 1) != 0)
    {
        fprintf(stderr, __FF__": Magic number is not 'farbfeld', path '%s'\n",
                path);
        goto errout;
    }

    image->width = ntohl(hdr[2]);
    image->height = ntohl(hdr[3]);

    image->data = calloc(image->width * image->height * 4, sizeof (uint16_t));
    if (!image->data)
    {
        fprintf(stderr, __FF__": Could not allocate memory, image->data for '%s'\n",
                path);
        goto errout;
    }

    if (fread(image->data,
              image->width * image->height * 4 * sizeof (uint16_t), 1, fp) != 1)
    {
        fprintf(stderr, __FF__": Unexpected EOF when reading '%s'\n", path);
        goto errout;
    }

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            image->data[(y * image->width + x) * 4] =
                ntohs(image->data[(y * image->width + x) * 4]);

    fclose(fp);
    return true;

errout:
    if (image && image->data)
        free(image->data);

    if (fp)
        fclose(fp);

    return false;
}

/* Render 'other' on top of 'onto'. 'other' will be moved to the
 * position ('off_x', 'off_y') inside of 'onto'. Offsets can't have
 * negative values. */
void
ff_overlay(struct FFImage *onto, struct FFImage *other,
           uint16_t off_x, uint16_t off_y)
{
    uint32_t x, y, x_onto, y_onto, rgb;
    size_t i_onto, i_other;
    uint16_t a;

    for (y = 0; y < other->height; y++)
    {
        for (x = 0; x < other->width; x++)
        {
            x_onto = x + off_x;
            y_onto = y + off_y;
            if (x_onto < onto->width && y_onto < onto->height)
            {
                i_onto = (y_onto * onto->width + x_onto) * 4;
                i_other = (y * other->width + x) * 4;
                a = other->data[i_other + 3];
                for (rgb = 0; rgb < 3; rgb++)
                    onto->data[i_onto + rgb] =
                        ((a * other->data[i_other + rgb]) >> 16) +
                        (((0xffff - a) * onto->data[i_onto + rgb]) >> 16);
            }
        }
    }
}

/* Write a farbfeld image to a file. */
bool
ff_save(struct FFImage *image, char *path)
{
    FILE *fp = NULL;
    uint32_t x, y, w, h, rgba;
    uint16_t px;

    fp = fopen(path, "w");
    if (!fp)
    {
        fprintf(stderr, __FF__": Could not open image file '%s'\n", path);
        goto errout;
    }

    fprintf(fp, "farbfeld");
    w = htonl(image->width);
    h = htonl(image->height);
    fwrite(&w, sizeof (uint32_t), 1, fp);
    fwrite(&h, sizeof (uint32_t), 1, fp);

    for (y = 0; y < image->height; y++)
    {
        for (x = 0; x < image->width; x++)
        {
            for (rgba = 0; rgba < 4; rgba++)
            {
                px = htons(image->data[(y * image->width + x) * 4 + rgba]);
                fwrite(&px, sizeof (uint16_t), 1, fp);
            }
        }
    }

    fclose(fp);

    return true;

errout:
    if (fp)
        fclose(fp);

    return false;
}

/* Convert a farbfeld image into an XImage that can then be sent to an
 * X11 server via XPutImage(). */
XImage *
ff_to_ximage(struct FFImage *image, Display *dpy, int screen)
{
    uint32_t *ximg_data = NULL, x, y;

    ximg_data = calloc(image->width * image->height, sizeof (uint32_t));
    if (!ximg_data)
    {
        fprintf(stderr, __FF__": Could not allocate memory, ximg_data\n");
        return NULL;
    }

    for (y = 0; y < image->height; y++)
    {
        for (x = 0; x < image->width; x++)
        {
            ximg_data[y * image->width + x] =
                ((image->data[(y * image->width + x) * 4    ] / 256) << 16) |
                ((image->data[(y * image->width + x) * 4 + 1] / 256) << 8) |
                 (image->data[(y * image->width + x) * 4 + 2] / 256);
        }
    }

    return XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                        (char *)ximg_data, image->width, image->height, 32, 0);
}

/* Same as ff_to_ximage_mono(), but the depth of the XImage will be 1,
 * i.e. it will be a monochrome bitmap. */
XImage *
ff_to_ximage_mono(struct FFImage *image, Display *dpy, int screen)
{
    uint32_t x, y;
    uint8_t *ximg_data = NULL;
    XImage *ximg;

    /* Create a monochrome image, i.e. depth of 1 bits per pixel. Yes,
     * literally one bit. One row of the image could be $width / 8
     * bytes. We don't want to fiddle with bits, though, and make our
     * program a little more robust. Tell X11 that each row will be
     * $width bytes long. We use XPutPixel to actually set the pixels,
     * we don't do that manually. This isn't as fast as it could be and
     * it wastes memory (since XPutPixel will internally set *bits*
     * while we think of "one byte per pixel"), but it clearly is fast
     * enough. It doesn't matter on modern machines. */

    ximg_data = calloc(image->width * image->height, sizeof (uint8_t));
    if (!ximg_data)
    {
        fprintf(stderr, __FF__": Could not allocate memory, ximg_data\n");
        return NULL;
    }

    ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), 1, ZPixmap, 0,
                        (char *)ximg_data, image->width, image->height, 8,
                        image->width);

    for (y = 0; y < image->height; y++)
    {
        for (x = 0; x < image->width; x++)
        {
            XPutPixel(ximg, x, y,
                (image->data[(y * image->width + x) * 4    ] != 0 ||
                 image->data[(y * image->width + x) * 4 + 1] != 0 ||
                 image->data[(y * image->width + x) * 4 + 2] != 0) ? 1 : 0);
        }
    }

    return ximg;
}

#undef __FF__

#endif /* LIBFF_H */
