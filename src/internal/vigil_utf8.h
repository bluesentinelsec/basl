/*
 * vigil_utf8.h — Shared UTF-8 encoding, decoding, and validation.
 *
 * Pure C11, no platform dependencies.  Used by the compiler, VM string
 * operations, JSON/TOML/XML parsers, and any other module that needs
 * to work with Unicode codepoints in UTF-8 strings.
 */

#ifndef VIGIL_UTF8_H
#define VIGIL_UTF8_H

#include <stddef.h>
#include <stdint.h>

/* Maximum bytes a single UTF-8 codepoint can occupy. */
#define VIGIL_UTF8_MAX_BYTES 4U

/* Unicode replacement character, returned on decode errors. */
#define VIGIL_UTF8_REPLACEMENT 0xFFFDU

/*
 * Encode a Unicode codepoint as UTF-8.
 * Writes 1–4 bytes to `out` (caller must provide VIGIL_UTF8_MAX_BYTES of space).
 * Returns bytes written, or 0 if the codepoint is invalid (> U+10FFFF
 * or a surrogate U+D800..U+DFFF).
 */
size_t vigil_utf8_encode(uint32_t codepoint, char *out);

/*
 * Decode one UTF-8 codepoint from `text` (up to `length` bytes).
 * Stores the decoded codepoint in `*out_codepoint`.
 * Returns bytes consumed (1–4), or 0 on malformed input.
 * On error, `*out_codepoint` is set to VIGIL_UTF8_REPLACEMENT.
 *
 * Rejects overlong encodings, surrogates, and codepoints above U+10FFFF.
 */
size_t vigil_utf8_decode(const char *text, size_t length, uint32_t *out_codepoint);

/*
 * Return the expected byte width of the UTF-8 sequence starting at
 * `lead_byte`.  Returns 1–4 for valid lead bytes, 1 for invalid bytes
 * (so callers can always advance by at least 1).
 */
size_t vigil_utf8_byte_width(unsigned char lead_byte);

/*
 * Count the number of Unicode codepoints in a UTF-8 string.
 * Malformed sequences each count as one codepoint (replacement).
 */
size_t vigil_utf8_codepoint_count(const char *text, size_t length);

/*
 * Validate that `text` is well-formed UTF-8.
 * Returns 1 if valid, 0 if any malformed sequence is found.
 * If `error_offset` is non-NULL, stores the byte offset of the first
 * error (unchanged if the string is valid).
 */
int vigil_utf8_validate(const char *text, size_t length, size_t *error_offset);

/*
 * Check whether a codepoint is a valid Unicode scalar value
 * (U+0000..U+D7FF, U+E000..U+10FFFF — excludes surrogates).
 */
int vigil_utf8_is_scalar(uint32_t codepoint);

#endif /* VIGIL_UTF8_H */
