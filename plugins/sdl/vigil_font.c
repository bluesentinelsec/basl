/* vigil_font.c — stb_truetype wrapper implementation. */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#define STB_TRUETYPE_IMPLEMENTATION

#include "../../deps/stb/stb_truetype.h"
#include "vigil_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vigil_font
{
    unsigned char *file_data; /* owned copy of the TTF file bytes */
    stbtt_fontinfo info;
};

vigil_font_t *vigil_font_load_file(const char *path)
{
    FILE *f;
    long len;
    unsigned char *buf;
    vigil_font_t *font;

    if (path == NULL)
        return NULL;

    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    if (len <= 0)
    {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    buf = (unsigned char *)malloc((size_t)len);
    if (buf == NULL)
    {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    font = (vigil_font_t *)calloc(1, sizeof(vigil_font_t));
    if (font == NULL)
    {
        free(buf);
        return NULL;
    }
    font->file_data = buf;

    {
        int offset = stbtt_GetFontOffsetForIndex(buf, 0);
        if (offset < 0 || !stbtt_InitFont(&font->info, buf, offset))
        {
            free(buf);
            free(font);
            return NULL;
        }
    }

    return font;
}

vigil_font_t *vigil_font_load_memory(const unsigned char *data, size_t length)
{
    unsigned char *copy;
    vigil_font_t *font;

    if (data == NULL || length == 0)
        return NULL;

    /* Copy the data so the font owns its buffer. */
    copy = (unsigned char *)malloc(length);
    if (copy == NULL)
        return NULL;
    memcpy(copy, data, length);

    font = (vigil_font_t *)calloc(1, sizeof(vigil_font_t));
    if (font == NULL)
    {
        free(copy);
        return NULL;
    }
    font->file_data = copy;

    {
        int offset = stbtt_GetFontOffsetForIndex(copy, 0);
        if (offset < 0 || !stbtt_InitFont(&font->info, copy, offset))
        {
            free(copy);
            free(font);
            return NULL;
        }
    }

    return font;
}

void vigil_font_free(vigil_font_t *font)
{
    if (font != NULL)
    {
        free(font->file_data);
        free(font);
    }
}

int vigil_font_render_text(const vigil_font_t *font, const char *text, float pixel_height, vigil_text_bitmap_t *out)
{
    float scale;
    int ascent, descent, line_gap;
    int total_width, max_height;
    int x_cursor;
    size_t i, len;

    if (font == NULL || text == NULL || out == NULL || pixel_height <= 0.0f)
        return -1;

    out->pixels = NULL;
    out->width = 0;
    out->height = 0;

    scale = stbtt_ScaleForPixelHeight(&font->info, pixel_height);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);

    int scaled_ascent = (int)ceilf((float)ascent * scale);
    int scaled_descent = (int)ceilf((float)(-descent) * scale);
    max_height = scaled_ascent + scaled_descent;

    /* First pass: measure total width. */
    len = strlen(text);
    total_width = 0;
    for (i = 0; i < len; i++)
    {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, text[i], &advance, &lsb);
        total_width += (int)ceilf((float)advance * scale);
        if (i + 1 < len)
            total_width += (int)(stbtt_GetCodepointKernAdvance(&font->info, text[i], text[i + 1]) * scale);
    }

    if (total_width <= 0 || max_height <= 0)
        return -1;

    /* Allocate output bitmap (zeroed). */
    out->pixels = (unsigned char *)calloc(1, (size_t)(total_width * max_height));
    if (out->pixels == NULL)
        return -1;
    out->width = total_width;
    out->height = max_height;

    /* Second pass: render each glyph. */
    x_cursor = 0;
    for (i = 0; i < len; i++)
    {
        int x0, y0, x1, y1;
        int gw, gh;
        unsigned char *glyph_bmp;
        int advance, lsb;

        stbtt_GetCodepointBitmapBox(&font->info, text[i], scale, scale, &x0, &y0, &x1, &y1);
        gw = x1 - x0;
        gh = y1 - y0;

        if (gw > 0 && gh > 0)
        {
            int blit_x = x_cursor + x0;
            int blit_y = scaled_ascent + y0;

            glyph_bmp = stbtt_GetCodepointBitmap(&font->info, scale, scale, text[i], &gw, &gh, NULL, NULL);
            if (glyph_bmp != NULL)
            {
                /* Blit glyph into output bitmap. */
                for (int row = 0; row < gh; row++)
                {
                    int dst_y = blit_y + row;
                    if (dst_y < 0 || dst_y >= max_height)
                        continue;
                    for (int col = 0; col < gw; col++)
                    {
                        int dst_x = blit_x + col;
                        if (dst_x < 0 || dst_x >= total_width)
                            continue;
                        unsigned char val = glyph_bmp[row * gw + col];
                        unsigned char existing = out->pixels[dst_y * total_width + dst_x];
                        /* Additive blend for overlapping glyphs. */
                        int blended = (int)existing + (int)val;
                        out->pixels[dst_y * total_width + dst_x] = (unsigned char)(blended > 255 ? 255 : blended);
                    }
                }
                stbtt_FreeBitmap(glyph_bmp, NULL);
            }
        }

        stbtt_GetCodepointHMetrics(&font->info, text[i], &advance, &lsb);
        x_cursor += (int)ceilf((float)advance * scale);
        if (i + 1 < len)
            x_cursor += (int)(stbtt_GetCodepointKernAdvance(&font->info, text[i], text[i + 1]) * scale);
    }

    return 0;
}

void vigil_text_bitmap_free(vigil_text_bitmap_t *bmp)
{
    if (bmp != NULL && bmp->pixels != NULL)
    {
        free(bmp->pixels);
        bmp->pixels = NULL;
    }
}
