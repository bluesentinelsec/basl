/* vigil_image.c — stb_image wrapper implementation. */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO /* we handle file I/O ourselves for portability */
#define STBI_NO_HDR   /* skip HDR float format — not needed for games */

#include "vigil_image.h"
#include "../../deps/stb/stb_image.h"

#include <stdio.h>
#include <stdlib.h>

int vigil_image_load_file(const char *path, vigil_image_t *out)
{
    FILE *f;
    unsigned char *buf;
    long len;
    int result;

    if (path == NULL || out == NULL)
        return -1;

    out->pixels = NULL;
    out->width = 0;
    out->height = 0;
    out->channels = 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    if (len <= 0)
    {
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_SET);

    buf = (unsigned char *)malloc((size_t)len);
    if (buf == NULL)
    {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    result = vigil_image_load_memory(buf, (size_t)len, out);
    free(buf);
    return result;
}

int vigil_image_load_memory(const unsigned char *data, size_t length, vigil_image_t *out)
{
    int w, h, channels_in_file;

    if (data == NULL || out == NULL || length == 0)
        return -1;

    out->pixels = NULL;
    out->width = 0;
    out->height = 0;
    out->channels = 0;

    /* Always request 4 channels (RGBA) regardless of source format. */
    out->pixels = stbi_load_from_memory(data, (int)length, &w, &h, &channels_in_file, 4);
    if (out->pixels == NULL)
        return -1;

    out->width = w;
    out->height = h;
    out->channels = 4;
    return 0;
}

void vigil_image_free(vigil_image_t *img)
{
    if (img != NULL && img->pixels != NULL)
    {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
    }
}
