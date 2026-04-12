/* vigil_image.h — Thin wrapper around stb_image for decoding images to RGBA. */
#ifndef VIGIL_IMAGE_H
#define VIGIL_IMAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_image
    {
        unsigned char *pixels; /* RGBA8888, row-major, top-to-bottom */
        int width;
        int height;
        int channels; /* always 4 (RGBA) after decode */
    } vigil_image_t;

    /* Decode an image from a file path.  Returns 0 on success, -1 on failure.
       Supports PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PIC.
       The caller must call vigil_image_free() when done. */
    int vigil_image_load_file(const char *path, vigil_image_t *out);

    /* Decode an image from a memory buffer.  Returns 0 on success, -1 on failure.
       The caller must call vigil_image_free() when done. */
    int vigil_image_load_memory(const unsigned char *data, size_t length, vigil_image_t *out);

    /* Free pixel data allocated by vigil_image_load_*. */
    void vigil_image_free(vigil_image_t *img);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_IMAGE_H */
