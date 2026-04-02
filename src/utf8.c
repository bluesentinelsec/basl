/*
 * utf8.c — Shared UTF-8 encoding, decoding, and validation.
 *
 * All functions are pure C11 with no platform dependencies.
 * See vigil_utf8.h for API documentation.
 */

#include "internal/vigil_utf8.h"

int vigil_utf8_is_scalar(uint32_t codepoint)
{
    return codepoint <= 0x10FFFFU && (codepoint < 0xD800U || codepoint > 0xDFFFU);
}

size_t vigil_utf8_encode(uint32_t codepoint, char *out)
{
    if (out == NULL)
        return 0U;

    if (!vigil_utf8_is_scalar(codepoint))
        return 0U;

    if (codepoint <= 0x7FU)
    {
        out[0] = (char)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7FFU)
    {
        out[0] = (char)(0xC0U | (codepoint >> 6U));
        out[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2U;
    }
    if (codepoint <= 0xFFFFU)
    {
        out[0] = (char)(0xE0U | (codepoint >> 12U));
        out[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[2] = (char)(0x80U | (codepoint & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (codepoint >> 18U));
    out[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3FU));
    out[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
    out[3] = (char)(0x80U | (codepoint & 0x3FU));
    return 4U;
}

size_t vigil_utf8_byte_width(unsigned char lead_byte)
{
    if (lead_byte < 0x80U)
        return 1U;
    if ((lead_byte & 0xE0U) == 0xC0U)
        return 2U;
    if ((lead_byte & 0xF0U) == 0xE0U)
        return 3U;
    if ((lead_byte & 0xF8U) == 0xF0U)
        return 4U;
    /* Invalid lead byte — return 1 so callers can always advance. */
    return 1U;
}

size_t vigil_utf8_decode(const char *text, size_t length, uint32_t *out_codepoint)
{
    uint32_t cp;
    size_t width;
    size_t i;
    unsigned char lead;

    if (text == NULL || length == 0U || out_codepoint == NULL)
    {
        if (out_codepoint != NULL)
            *out_codepoint = VIGIL_UTF8_REPLACEMENT;
        return 0U;
    }

    lead = (unsigned char)text[0];

    /* Single-byte ASCII. */
    if (lead < 0x80U)
    {
        *out_codepoint = lead;
        return 1U;
    }

    /* Determine expected width and initial codepoint bits. */
    if ((lead & 0xE0U) == 0xC0U)
    {
        width = 2U;
        cp = lead & 0x1FU;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        width = 3U;
        cp = lead & 0x0FU;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        width = 4U;
        cp = lead & 0x07U;
    }
    else
    {
        /* Invalid lead byte (continuation byte or 0xFE/0xFF). */
        *out_codepoint = VIGIL_UTF8_REPLACEMENT;
        return 0U;
    }

    /* Check that we have enough bytes. */
    if (width > length)
    {
        *out_codepoint = VIGIL_UTF8_REPLACEMENT;
        return 0U;
    }

    /* Read and validate continuation bytes. */
    for (i = 1U; i < width; i++)
    {
        unsigned char cb = (unsigned char)text[i];
        if ((cb & 0xC0U) != 0x80U)
        {
            *out_codepoint = VIGIL_UTF8_REPLACEMENT;
            return 0U;
        }
        cp = (cp << 6U) | (cb & 0x3FU);
    }

    /* Reject overlong encodings. */
    if ((width == 2U && cp < 0x80U) || (width == 3U && cp < 0x800U) || (width == 4U && cp < 0x10000U))
    {
        *out_codepoint = VIGIL_UTF8_REPLACEMENT;
        return 0U;
    }

    /* Reject surrogates and out-of-range codepoints. */
    if (!vigil_utf8_is_scalar(cp))
    {
        *out_codepoint = VIGIL_UTF8_REPLACEMENT;
        return 0U;
    }

    *out_codepoint = cp;
    return width;
}

size_t vigil_utf8_codepoint_count(const char *text, size_t length)
{
    size_t count;
    size_t index;
    uint32_t cp;
    size_t consumed;

    if (text == NULL)
        return 0U;

    count = 0U;
    index = 0U;
    while (index < length)
    {
        consumed = vigil_utf8_decode(text + index, length - index, &cp);
        if (consumed == 0U)
        {
            /* Malformed byte — skip one byte, count as one codepoint. */
            index += 1U;
        }
        else
        {
            index += consumed;
        }
        count += 1U;
    }
    return count;
}

int vigil_utf8_validate(const char *text, size_t length, size_t *error_offset)
{
    size_t index;
    uint32_t cp;
    size_t consumed;

    if (text == NULL)
        return length == 0U;

    index = 0U;
    while (index < length)
    {
        consumed = vigil_utf8_decode(text + index, length - index, &cp);
        if (consumed == 0U)
        {
            if (error_offset != NULL)
                *error_offset = index;
            return 0;
        }
        index += consumed;
    }
    return 1;
}
