#ifndef LIBFF_H
#define LIBFF_H

#define __FF__ "libff"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>

struct FFImage
{
    uint32_t width, height;
    uint16_t *data;
};

void
ff_clear(struct FFImage *image, double alpha)
{
    uint32_t x, y;

    memset(image->data, 0, image->width * image->height * 4 * sizeof (uint16_t));

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            image->data[(y * image->width + x) * 4 + 3] = alpha * 65535;
}

void
ff_init_empty(struct FFImage *image)
{
    image->data = calloc(image->width * image->height * 4, sizeof (uint16_t));
    if (!image->data)
        fprintf(stderr, __FF__": Could not allocate memory in ff_init_empty\n");
}

bool
ff_load(char *path, struct FFImage *image)
{
    FILE *fp = NULL;
    uint32_t hdr[4], x, y;

    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, __FF__": Could not open image file '%s'\n", path);
        goto cleanout;
    }

    if (fread(hdr, sizeof (uint32_t), 4, fp) != 4)
    {
        fprintf(stderr, __FF__": Could not read farbfeld header from '%s'\n",
                path);
        goto cleanout;
    }

    if (memcmp("farbfeld", hdr, (sizeof "farbfeld") - 1) != 0)
    {
        fprintf(stderr, __FF__": Magic number is not 'farbfeld', path '%s'\n",
                path);
        goto cleanout;
    }

    image->width = ntohl(hdr[2]);
    image->height = ntohl(hdr[3]);

    image->data = calloc(image->width * image->height * 4, sizeof (uint16_t));
    if (!image->data)
    {
        fprintf(stderr, __FF__": Could not allocate memory, image->data for '%s'\n",
                path);
        goto cleanout;
    }

    if (fread(image->data,
              image->width * image->height * 4 * sizeof (uint16_t), 1, fp) != 1)
    {
        fprintf(stderr, __FF__": Unexpected EOF when reading '%s'\n", path);
        goto cleanout;
    }

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            image->data[(y * image->width + x) * 4] =
                ntohs(image->data[(y * image->width + x) * 4]);

    fclose(fp);
    return true;

cleanout:
    if (image && image->data)
        free(image->data);

    if (fp)
        fclose(fp);

    return false;
}

void
ff_overlay(struct FFImage *onto, struct FFImage *other)
{
    uint32_t x, y, w, h, rgb;
    double a;

    w = onto->width;
    h = onto->height;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            a = (double)other->data[(y * w + x) * 4 + 3] / 65536;
            for (rgb = 0; rgb < 3; rgb++)
                onto->data[(y * w + x) * 4 + rgb] =
                    a       * other->data[(y * w + x) * 4 + rgb] + 
                    (1 - a) * onto->data[(y * w + x) * 4 + rgb];
        }
    }
}

bool
ff_save(char *path, struct FFImage *image)
{
    FILE *fp = NULL;
    uint32_t x, y, w, h, rgba;
    uint16_t px;

    fp = fopen(path, "w");
    if (!fp)
    {
        fprintf(stderr, __FF__": Could not open image file '%s'\n", path);
        goto cleanout;
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

cleanout:
    if (fp)
        fclose(fp);

    return false;
}

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

#undef __FF__

#endif /* LIBFF_H */
