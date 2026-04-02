#include "vigil_test.h"

#include <string.h>

#include "internal/vigil_utf8.h"

/* ── vigil_utf8_is_scalar ────────────────────────────────────────── */

TEST(VigilUtf8Test, IsScalarAcceptsValidCodepoints)
{
    EXPECT_EQ(vigil_utf8_is_scalar(0x0000U), 1);
    EXPECT_EQ(vigil_utf8_is_scalar(0x0041U), 1);   /* A */
    EXPECT_EQ(vigil_utf8_is_scalar(0x00E9U), 1);   /* é */
    EXPECT_EQ(vigil_utf8_is_scalar(0x20ACU), 1);   /* € */
    EXPECT_EQ(vigil_utf8_is_scalar(0x4E2DU), 1);   /* 中 */
    EXPECT_EQ(vigil_utf8_is_scalar(0x1F3AEU), 1);  /* 🎮 */
    EXPECT_EQ(vigil_utf8_is_scalar(0x10FFFFU), 1); /* max */
    EXPECT_EQ(vigil_utf8_is_scalar(0xD7FFU), 1);   /* just before surrogates */
    EXPECT_EQ(vigil_utf8_is_scalar(0xE000U), 1);   /* just after surrogates */
}

TEST(VigilUtf8Test, IsScalarRejectsSurrogatesAndOutOfRange)
{
    EXPECT_EQ(vigil_utf8_is_scalar(0xD800U), 0);   /* surrogate start */
    EXPECT_EQ(vigil_utf8_is_scalar(0xDBFFU), 0);   /* high surrogate end */
    EXPECT_EQ(vigil_utf8_is_scalar(0xDC00U), 0);   /* low surrogate start */
    EXPECT_EQ(vigil_utf8_is_scalar(0xDFFFU), 0);   /* surrogate end */
    EXPECT_EQ(vigil_utf8_is_scalar(0x110000U), 0); /* above max */
    EXPECT_EQ(vigil_utf8_is_scalar(0xFFFFFFFFU), 0);
}

/* ── vigil_utf8_encode ───────────────────────────────────────────── */

TEST(VigilUtf8Test, EncodeAscii)
{
    char buf[4];
    EXPECT_EQ(vigil_utf8_encode(0x41U, buf), 1U);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(vigil_utf8_encode(0x00U, buf), 1U);
    EXPECT_EQ(buf[0], '\0');
    EXPECT_EQ(vigil_utf8_encode(0x7FU, buf), 1U);
    EXPECT_EQ((unsigned char)buf[0], 0x7FU);
}

TEST(VigilUtf8Test, EncodeTwoByte)
{
    char buf[4];
    /* U+00E9 = é = C3 A9 */
    EXPECT_EQ(vigil_utf8_encode(0x00E9U, buf), 2U);
    EXPECT_EQ((unsigned char)buf[0], 0xC3U);
    EXPECT_EQ((unsigned char)buf[1], 0xA9U);
    /* U+0080 = lowest 2-byte */
    EXPECT_EQ(vigil_utf8_encode(0x0080U, buf), 2U);
    EXPECT_EQ((unsigned char)buf[0], 0xC2U);
    EXPECT_EQ((unsigned char)buf[1], 0x80U);
    /* U+07FF = highest 2-byte */
    EXPECT_EQ(vigil_utf8_encode(0x07FFU, buf), 2U);
    EXPECT_EQ((unsigned char)buf[0], 0xDFU);
    EXPECT_EQ((unsigned char)buf[1], 0xBFU);
}

TEST(VigilUtf8Test, EncodeThreeByte)
{
    char buf[4];
    /* U+20AC = € = E2 82 AC */
    EXPECT_EQ(vigil_utf8_encode(0x20ACU, buf), 3U);
    EXPECT_EQ((unsigned char)buf[0], 0xE2U);
    EXPECT_EQ((unsigned char)buf[1], 0x82U);
    EXPECT_EQ((unsigned char)buf[2], 0xACU);
    /* U+4E2D = 中 */
    EXPECT_EQ(vigil_utf8_encode(0x4E2DU, buf), 3U);
    EXPECT_EQ((unsigned char)buf[0], 0xE4U);
    EXPECT_EQ((unsigned char)buf[1], 0xB8U);
    EXPECT_EQ((unsigned char)buf[2], 0xADU);
}

TEST(VigilUtf8Test, EncodeFourByte)
{
    char buf[4];
    /* U+1F3AE = 🎮 = F0 9F 8E AE */
    EXPECT_EQ(vigil_utf8_encode(0x1F3AEU, buf), 4U);
    EXPECT_EQ((unsigned char)buf[0], 0xF0U);
    EXPECT_EQ((unsigned char)buf[1], 0x9FU);
    EXPECT_EQ((unsigned char)buf[2], 0x8EU);
    EXPECT_EQ((unsigned char)buf[3], 0xAEU);
    /* U+10FFFF = max codepoint */
    EXPECT_EQ(vigil_utf8_encode(0x10FFFFU, buf), 4U);
    EXPECT_EQ((unsigned char)buf[0], 0xF4U);
    EXPECT_EQ((unsigned char)buf[1], 0x8FU);
    EXPECT_EQ((unsigned char)buf[2], 0xBFU);
    EXPECT_EQ((unsigned char)buf[3], 0xBFU);
}

TEST(VigilUtf8Test, EncodeRejectsInvalid)
{
    char buf[4];
    EXPECT_EQ(vigil_utf8_encode(0xD800U, buf), 0U);   /* surrogate */
    EXPECT_EQ(vigil_utf8_encode(0xDFFFU, buf), 0U);   /* surrogate */
    EXPECT_EQ(vigil_utf8_encode(0x110000U, buf), 0U); /* above max */
    EXPECT_EQ(vigil_utf8_encode(0x41U, NULL), 0U);    /* null output */
}

/* ── vigil_utf8_decode ───────────────────────────────────────────── */

TEST(VigilUtf8Test, DecodeAscii)
{
    uint32_t cp;
    EXPECT_EQ(vigil_utf8_decode("A", 1, &cp), 1U);
    EXPECT_EQ(cp, 0x41U);
    EXPECT_EQ(vigil_utf8_decode("\x7F", 1, &cp), 1U);
    EXPECT_EQ(cp, 0x7FU);
    EXPECT_EQ(vigil_utf8_decode("\0", 1, &cp), 1U);
    EXPECT_EQ(cp, 0x00U);
}

TEST(VigilUtf8Test, DecodeTwoByte)
{
    uint32_t cp;
    /* é = C3 A9 */
    EXPECT_EQ(vigil_utf8_decode("\xC3\xA9", 2, &cp), 2U);
    EXPECT_EQ(cp, 0x00E9U);
    /* U+0080 = C2 80 */
    EXPECT_EQ(vigil_utf8_decode("\xC2\x80", 2, &cp), 2U);
    EXPECT_EQ(cp, 0x0080U);
}

TEST(VigilUtf8Test, DecodeThreeByte)
{
    uint32_t cp;
    /* € = E2 82 AC */
    EXPECT_EQ(vigil_utf8_decode("\xE2\x82\xAC", 3, &cp), 3U);
    EXPECT_EQ(cp, 0x20ACU);
}

TEST(VigilUtf8Test, DecodeFourByte)
{
    uint32_t cp;
    /* 🎮 = F0 9F 8E AE */
    EXPECT_EQ(vigil_utf8_decode("\xF0\x9F\x8E\xAE", 4, &cp), 4U);
    EXPECT_EQ(cp, 0x1F3AEU);
    /* U+10FFFF = F4 8F BF BF */
    EXPECT_EQ(vigil_utf8_decode("\xF4\x8F\xBF\xBF", 4, &cp), 4U);
    EXPECT_EQ(cp, 0x10FFFFU);
}

TEST(VigilUtf8Test, DecodeRejectsTruncated)
{
    uint32_t cp;
    /* 2-byte lead but only 1 byte available */
    EXPECT_EQ(vigil_utf8_decode("\xC3", 1, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* 3-byte lead but only 2 bytes available */
    EXPECT_EQ(vigil_utf8_decode("\xE2\x82", 2, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* 4-byte lead but only 3 bytes available */
    EXPECT_EQ(vigil_utf8_decode("\xF0\x9F\x8E", 3, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsInvalidContinuation)
{
    uint32_t cp;
    /* 2-byte lead followed by non-continuation byte */
    EXPECT_EQ(vigil_utf8_decode("\xC3\x41", 2, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* 3-byte lead, first continuation ok, second is not */
    EXPECT_EQ(vigil_utf8_decode("\xE2\x82\x41", 3, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsOverlong)
{
    uint32_t cp;
    /* Overlong 2-byte encoding of '/' (U+002F): C0 AF */
    EXPECT_EQ(vigil_utf8_decode("\xC0\xAF", 2, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* Overlong 2-byte encoding of NUL (U+0000): C0 80 */
    EXPECT_EQ(vigil_utf8_decode("\xC0\x80", 2, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* Overlong 3-byte encoding of '/' (U+002F): E0 80 AF */
    EXPECT_EQ(vigil_utf8_decode("\xE0\x80\xAF", 3, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* Overlong 4-byte encoding of U+0080: F0 80 82 80 */
    EXPECT_EQ(vigil_utf8_decode("\xF0\x80\x82\x80", 4, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsSurrogates)
{
    uint32_t cp;
    /* U+D800 encoded as 3 bytes: ED A0 80 */
    EXPECT_EQ(vigil_utf8_decode("\xED\xA0\x80", 3, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    /* U+DFFF encoded as 3 bytes: ED BF BF */
    EXPECT_EQ(vigil_utf8_decode("\xED\xBF\xBF", 3, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsAboveMax)
{
    uint32_t cp;
    /* U+110000 encoded as 4 bytes: F4 90 80 80 */
    EXPECT_EQ(vigil_utf8_decode("\xF4\x90\x80\x80", 4, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsLoneContinuationByte)
{
    uint32_t cp;
    EXPECT_EQ(vigil_utf8_decode("\x80", 1, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    EXPECT_EQ(vigil_utf8_decode("\xBF", 1, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeRejectsInvalidLeadBytes)
{
    uint32_t cp;
    EXPECT_EQ(vigil_utf8_decode("\xFE", 1, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    EXPECT_EQ(vigil_utf8_decode("\xFF", 1, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
}

TEST(VigilUtf8Test, DecodeHandlesNullInputs)
{
    uint32_t cp = 0U;
    EXPECT_EQ(vigil_utf8_decode(NULL, 0, &cp), 0U);
    EXPECT_EQ(cp, VIGIL_UTF8_REPLACEMENT);
    EXPECT_EQ(vigil_utf8_decode("A", 0, &cp), 0U);
    EXPECT_EQ(vigil_utf8_decode("A", 1, NULL), 0U);
}

/* ── vigil_utf8_encode / vigil_utf8_decode round-trip ────────────── */

TEST(VigilUtf8Test, EncodeDecodeRoundTrip)
{
    static const uint32_t codepoints[] = {0x00U,   0x41U,   0x7FU,    0x80U,    0xFFU,    0x07FFU,
                                          0x0800U, 0x00E9U, 0x20ACU,  0x4E2DU,  0xD7FFU,  0xE000U,
                                          0xFFFDU, 0xFFFFU, 0x10000U, 0x1F3AEU, 0x10FFFFU};
    size_t i;
    for (i = 0U; i < sizeof(codepoints) / sizeof(codepoints[0]); i++)
    {
        char buf[4];
        uint32_t decoded;
        size_t encoded_len = vigil_utf8_encode(codepoints[i], buf);
        size_t decoded_len;
        EXPECT_NE(encoded_len, 0U);
        decoded_len = vigil_utf8_decode(buf, encoded_len, &decoded);
        EXPECT_EQ(decoded_len, encoded_len);
        EXPECT_EQ(decoded, codepoints[i]);
    }
}

/* ── vigil_utf8_byte_width ───────────────────────────────────────── */

TEST(VigilUtf8Test, ByteWidthReturnsCorrectWidths)
{
    /* ASCII range */
    EXPECT_EQ(vigil_utf8_byte_width(0x00U), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0x41U), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0x7FU), 1U);
    /* 2-byte lead */
    EXPECT_EQ(vigil_utf8_byte_width(0xC0U), 2U);
    EXPECT_EQ(vigil_utf8_byte_width(0xC3U), 2U);
    EXPECT_EQ(vigil_utf8_byte_width(0xDFU), 2U);
    /* 3-byte lead */
    EXPECT_EQ(vigil_utf8_byte_width(0xE0U), 3U);
    EXPECT_EQ(vigil_utf8_byte_width(0xE2U), 3U);
    EXPECT_EQ(vigil_utf8_byte_width(0xEFU), 3U);
    /* 4-byte lead */
    EXPECT_EQ(vigil_utf8_byte_width(0xF0U), 4U);
    EXPECT_EQ(vigil_utf8_byte_width(0xF4U), 4U);
    EXPECT_EQ(vigil_utf8_byte_width(0xF7U), 4U);
    /* Invalid lead bytes → 1 */
    EXPECT_EQ(vigil_utf8_byte_width(0x80U), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0xBFU), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0xF8U), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0xFEU), 1U);
    EXPECT_EQ(vigil_utf8_byte_width(0xFFU), 1U);
}

/* ── vigil_utf8_codepoint_count ──────────────────────────────────── */

TEST(VigilUtf8Test, CodepointCountAscii)
{
    EXPECT_EQ(vigil_utf8_codepoint_count("hello", 5), 5U);
    EXPECT_EQ(vigil_utf8_codepoint_count("", 0), 0U);
}

TEST(VigilUtf8Test, CodepointCountMixed)
{
    /* "café" = 63 61 66 C3 A9 = 5 bytes, 4 codepoints */
    EXPECT_EQ(vigil_utf8_codepoint_count("caf\xC3\xA9", 5), 4U);
    /* "中文" = E4 B8 AD E6 96 87 = 6 bytes, 2 codepoints */
    EXPECT_EQ(vigil_utf8_codepoint_count("\xE4\xB8\xAD\xE6\x96\x87", 6), 2U);
    /* "🎮" = F0 9F 8E AE = 4 bytes, 1 codepoint */
    EXPECT_EQ(vigil_utf8_codepoint_count("\xF0\x9F\x8E\xAE", 4), 1U);
}

TEST(VigilUtf8Test, CodepointCountMalformedCountsAsOne)
{
    /* Lone continuation byte counts as 1 */
    EXPECT_EQ(vigil_utf8_codepoint_count("\x80", 1), 1U);
    /* "A" + lone continuation + "B" = 3 */
    EXPECT_EQ(vigil_utf8_codepoint_count("A\x80"
                                         "B",
                                         3),
              3U);
}

TEST(VigilUtf8Test, CodepointCountNullReturnsZero)
{
    EXPECT_EQ(vigil_utf8_codepoint_count(NULL, 0), 0U);
    EXPECT_EQ(vigil_utf8_codepoint_count(NULL, 5), 0U);
}

/* ── vigil_utf8_validate ─────────────────────────────────────────── */

TEST(VigilUtf8Test, ValidateAcceptsValidStrings)
{
    EXPECT_EQ(vigil_utf8_validate("hello", 5, NULL), 1);
    EXPECT_EQ(vigil_utf8_validate("", 0, NULL), 1);
    EXPECT_EQ(vigil_utf8_validate("caf\xC3\xA9", 5, NULL), 1);
    EXPECT_EQ(vigil_utf8_validate("\xE4\xB8\xAD", 3, NULL), 1);
    EXPECT_EQ(vigil_utf8_validate("\xF0\x9F\x8E\xAE", 4, NULL), 1);
}

TEST(VigilUtf8Test, ValidateRejectsMalformed)
{
    size_t offset = 999U;
    /* Lone continuation byte */
    EXPECT_EQ(vigil_utf8_validate("\x80", 1, &offset), 0);
    EXPECT_EQ(offset, 0U);
    /* Truncated 2-byte */
    offset = 999U;
    EXPECT_EQ(vigil_utf8_validate("A\xC3", 2, &offset), 0);
    EXPECT_EQ(offset, 1U);
    /* Overlong */
    offset = 999U;
    EXPECT_EQ(vigil_utf8_validate("\xC0\xAF", 2, &offset), 0);
    EXPECT_EQ(offset, 0U);
    /* Surrogate */
    offset = 999U;
    EXPECT_EQ(vigil_utf8_validate("\xED\xA0\x80", 3, &offset), 0);
    EXPECT_EQ(offset, 0U);
}

TEST(VigilUtf8Test, ValidateNullInput)
{
    EXPECT_EQ(vigil_utf8_validate(NULL, 0, NULL), 1);
    EXPECT_EQ(vigil_utf8_validate(NULL, 5, NULL), 0);
}

/* ── Registration ────────────────────────────────────────────────── */

void register_utf8_tests(void)
{
    REGISTER_TEST(VigilUtf8Test, IsScalarAcceptsValidCodepoints);
    REGISTER_TEST(VigilUtf8Test, IsScalarRejectsSurrogatesAndOutOfRange);
    REGISTER_TEST(VigilUtf8Test, EncodeAscii);
    REGISTER_TEST(VigilUtf8Test, EncodeTwoByte);
    REGISTER_TEST(VigilUtf8Test, EncodeThreeByte);
    REGISTER_TEST(VigilUtf8Test, EncodeFourByte);
    REGISTER_TEST(VigilUtf8Test, EncodeRejectsInvalid);
    REGISTER_TEST(VigilUtf8Test, DecodeAscii);
    REGISTER_TEST(VigilUtf8Test, DecodeTwoByte);
    REGISTER_TEST(VigilUtf8Test, DecodeThreeByte);
    REGISTER_TEST(VigilUtf8Test, DecodeFourByte);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsTruncated);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsInvalidContinuation);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsOverlong);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsSurrogates);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsAboveMax);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsLoneContinuationByte);
    REGISTER_TEST(VigilUtf8Test, DecodeRejectsInvalidLeadBytes);
    REGISTER_TEST(VigilUtf8Test, DecodeHandlesNullInputs);
    REGISTER_TEST(VigilUtf8Test, EncodeDecodeRoundTrip);
    REGISTER_TEST(VigilUtf8Test, ByteWidthReturnsCorrectWidths);
    REGISTER_TEST(VigilUtf8Test, CodepointCountAscii);
    REGISTER_TEST(VigilUtf8Test, CodepointCountMixed);
    REGISTER_TEST(VigilUtf8Test, CodepointCountMalformedCountsAsOne);
    REGISTER_TEST(VigilUtf8Test, CodepointCountNullReturnsZero);
    REGISTER_TEST(VigilUtf8Test, ValidateAcceptsValidStrings);
    REGISTER_TEST(VigilUtf8Test, ValidateRejectsMalformed);
    REGISTER_TEST(VigilUtf8Test, ValidateNullInput);
}
