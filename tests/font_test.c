/* vigil_font_test.c — Unit tests for the stb_truetype wrapper. */
#include "vigil_test.h"

#include <string.h>

#include "../../plugins/sdl/vigil_font.h"

TEST(VigilFontTest, LoadFileRejectsNonexistent)
{
    vigil_font_t *font = vigil_font_load_file("/tmp/nonexistent_vigil_font.ttf");
    EXPECT_EQ(font, NULL);
}

TEST(VigilFontTest, LoadFileRejectsNull)
{
    vigil_font_t *font = vigil_font_load_file(NULL);
    EXPECT_EQ(font, NULL);
}

TEST(VigilFontTest, LoadMemoryRejectsInvalidData)
{
    unsigned char garbage[] = {0x00, 0x01, 0x02, 0x03};
    vigil_font_t *font = vigil_font_load_memory(garbage, sizeof(garbage));
    EXPECT_EQ(font, NULL);
}

TEST(VigilFontTest, LoadMemoryRejectsNull)
{
    EXPECT_EQ(vigil_font_load_memory(NULL, 0), NULL);
    EXPECT_EQ(vigil_font_load_memory(NULL, 100), NULL);
}

TEST(VigilFontTest, FreeHandlesNull)
{
    vigil_font_free(NULL);
    EXPECT_EQ(1, 1);
}

TEST(VigilFontTest, RenderTextRejectsNullArgs)
{
    vigil_text_bitmap_t bmp;
    EXPECT_EQ(vigil_font_render_text(NULL, NULL, 24.0f, &bmp), -1);
    EXPECT_EQ(vigil_font_render_text(NULL, "hello", 0.0f, &bmp), -1);
    EXPECT_EQ(vigil_font_render_text(NULL, "hello", -1.0f, &bmp), -1);
}

TEST(VigilFontTest, BitmapFreeHandlesNull)
{
    vigil_text_bitmap_t bmp;
    memset(&bmp, 0, sizeof(bmp));
    vigil_text_bitmap_free(&bmp);
    vigil_text_bitmap_free(NULL);
    EXPECT_EQ(bmp.pixels, NULL);
}

void register_font_tests(void)
{
    REGISTER_TEST(VigilFontTest, LoadFileRejectsNonexistent);
    REGISTER_TEST(VigilFontTest, LoadFileRejectsNull);
    REGISTER_TEST(VigilFontTest, LoadMemoryRejectsInvalidData);
    REGISTER_TEST(VigilFontTest, LoadMemoryRejectsNull);
    REGISTER_TEST(VigilFontTest, FreeHandlesNull);
    REGISTER_TEST(VigilFontTest, RenderTextRejectsNullArgs);
    REGISTER_TEST(VigilFontTest, BitmapFreeHandlesNull);
}
