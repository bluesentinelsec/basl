/* vigil_font.h — TTF font loading and text rendering via stb_truetype. */
#ifndef VIGIL_FONT_H
#define VIGIL_FONT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_font vigil_font_t;

    /* Load a TTF font from a file path.  Returns NULL on failure. */
    vigil_font_t *vigil_font_load_file(const char *path);

    /* Load a TTF font from a memory buffer.  The buffer must remain valid
       for the lifetime of the font.  Returns NULL on failure. */
    vigil_font_t *vigil_font_load_memory(const unsigned char *data, size_t length);

    /* Free a font loaded by vigil_font_load_*. */
    void vigil_font_free(vigil_font_t *font);

    /* Rendered text bitmap. */
    typedef struct vigil_text_bitmap
    {
        unsigned char *pixels; /* 8-bit grayscale, row-major */
        int width;
        int height;
    } vigil_text_bitmap_t;

    /* Render a string to a grayscale bitmap at the given pixel height.
       Returns 0 on success, -1 on failure.
       The caller must call vigil_text_bitmap_free() when done. */
    int vigil_font_render_text(const vigil_font_t *font, const char *text, float pixel_height,
                               vigil_text_bitmap_t *out);

    /* Free a bitmap allocated by vigil_font_render_text. */
    void vigil_text_bitmap_free(vigil_text_bitmap_t *bmp);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_FONT_H */
