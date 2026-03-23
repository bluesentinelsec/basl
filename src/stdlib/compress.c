/* VIGIL standard library: compress module.
 *
 * Compression and decompression using deflate, zlib, gzip formats.
 * Uses miniz library (MIT license).
 */
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/stdlib.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"
#include "lz4.h"
#include "miniz.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *get_bytes_data(vigil_value_t v, size_t *out_len)
{
    if (vigil_nanbox_is_object(v))
    {
        const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            *out_len = vigil_string_object_length(obj);
            return vigil_string_object_c_str(obj);
        }
    }
    *out_len = 0;
    return NULL;
}

static vigil_status_t push_bytes(vigil_vm_t *vm, const void *data, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), (const char *)data, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t push_empty_bytes(vigil_vm_t *vm, vigil_error_t *error)
{
    return push_bytes(vm, "", 0, error);
}

static int clamp_level(int level)
{
    if (level < 0)
        return 0;
    if (level > 10)
        return 10;
    return level;
}

/* ── CRC32 / Adler32 ────────────────────────────────────────────── */

static vigil_status_t crc32_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);
    mz_uint32 crc = (mz_uint32)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)(src ? src : ""), src_len);
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)crc);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t adler32_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);
    mz_ulong a = mz_adler32(MZ_ADLER32_INIT, (const unsigned char *)(src ? src : ""), src_len);
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)a);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── Zlib ────────────────────────────────────────────────────────── */

static vigil_status_t zlib_compress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong dst_len;
    unsigned char *dst;
    int status;
    vigil_status_t ret;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    dst_len = mz_compressBound((mz_ulong)src_len);
    dst = (unsigned char *)malloc(dst_len);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    status = mz_compress(dst, &dst_len, (const unsigned char *)src, (mz_ulong)src_len);
    if (status != MZ_OK)
    {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, dst, dst_len, error);
    free(dst);
    return ret;
}

static vigil_status_t zlib_compress_level_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    int level;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    level = clamp_level((int)vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 1)));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
        return push_empty_bytes(vm, error);

    mz_ulong dst_len = mz_compressBound((mz_ulong)src_len);
    unsigned char *dst = (unsigned char *)malloc(dst_len);
    if (!dst)
        return push_empty_bytes(vm, error);

    int status = mz_compress2(dst, &dst_len, (const unsigned char *)src, (mz_ulong)src_len, level);
    if (status != MZ_OK)
    {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    vigil_status_t ret = push_bytes(vm, dst, dst_len, error);
    free(dst);
    return ret;
}

static vigil_status_t zlib_decompress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong dst_len;
    unsigned char *dst;
    int status;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    /* Start with 4x source size, grow if needed */
    dst_len = (mz_ulong)(src_len * 4);
    if (dst_len < 1024)
        dst_len = 1024;

    dst = (unsigned char *)malloc(dst_len);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    while (1)
    {
        mz_ulong try_len = dst_len;
        status = mz_uncompress(dst, &try_len, (const unsigned char *)src, (mz_ulong)src_len);
        if (status == MZ_OK)
        {
            vigil_status_t ret = push_bytes(vm, dst, try_len, error);
            free(dst);
            return ret;
        }
        if (status == MZ_BUF_ERROR)
        {
            /* Need more space */
            mz_ulong new_len = dst_len * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_len);
            if (!new_dst)
            {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_len = new_len;
            continue;
        }
        /* Other error */
        free(dst);
        return push_empty_bytes(vm, error);
    }
}

/* ── Deflate (raw) ───────────────────────────────────────────────── */

static vigil_status_t deflate_compress_impl(vigil_vm_t *vm, const char *src, size_t src_len, int level,
                                            vigil_error_t *error)
{
    if (!src || src_len == 0)
        return push_empty_bytes(vm, error);

    mz_ulong bound = mz_deflateBound(NULL, (mz_ulong)src_len);
    unsigned char *dst = (unsigned char *)malloc(bound);
    if (!dst)
        return push_empty_bytes(vm, error);

    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (const unsigned char *)src;
    stream.avail_in = (mz_uint32)src_len;
    stream.next_out = dst;
    stream.avail_out = (mz_uint32)bound;

    /* Use negative window bits for raw deflate (no zlib header/trailer) */
    if (mz_deflateInit2(&stream, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK)
    {
        free(dst);
        return push_empty_bytes(vm, error);
    }
    if (mz_deflate(&stream, MZ_FINISH) != MZ_STREAM_END)
    {
        mz_deflateEnd(&stream);
        free(dst);
        return push_empty_bytes(vm, error);
    }
    size_t out_len = stream.total_out;
    mz_deflateEnd(&stream);

    vigil_status_t ret = push_bytes(vm, dst, out_len, error);
    free(dst);
    return ret;
}

static vigil_status_t deflate_compress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);
    return deflate_compress_impl(vm, src, src_len, MZ_DEFAULT_COMPRESSION, error);
}

static vigil_status_t deflate_compress_level_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    int level = clamp_level((int)vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 1)));
    vigil_vm_stack_pop_n(vm, arg_count);
    return deflate_compress_impl(vm, src, src_len, level, error);
}

static vigil_status_t deflate_decompress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    tinfl_decompressor decomp;
    size_t dst_cap = 1024 * 64;
    size_t dst_len = 0;
    unsigned char *dst;
    tinfl_status status;
    size_t in_pos = 0;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    dst = (unsigned char *)malloc(dst_cap);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    tinfl_init(&decomp);

    while (in_pos < src_len)
    {
        size_t in_bytes = src_len - in_pos;
        size_t out_bytes = dst_cap - dst_len;
        int flags = (in_pos + in_bytes < src_len) ? TINFL_FLAG_HAS_MORE_INPUT : 0;

        status =
            tinfl_decompress(&decomp, (const mz_uint8 *)src + in_pos, &in_bytes, dst, dst + dst_len, &out_bytes, flags);

        in_pos += in_bytes;
        dst_len += out_bytes;

        if (status == TINFL_STATUS_DONE)
        {
            break;
        }
        if (status < 0)
        {
            free(dst);
            return push_empty_bytes(vm, error);
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT || dst_len >= dst_cap)
        {
            size_t new_cap = dst_cap * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_cap);
            if (!new_dst)
            {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }

    {
        vigil_status_t ret = push_bytes(vm, dst, dst_len, error);
        free(dst);
        return ret;
    }
}

/* ── Gzip ────────────────────────────────────────────────────────── */

static vigil_status_t gzip_compress_impl(vigil_vm_t *vm, const char *src, size_t src_len, int level,
                                         vigil_error_t *error)
{
    mz_ulong bound;
    unsigned char *out;
    size_t out_len;
    mz_stream stream;
    mz_uint32 crc;
    vigil_status_t ret;

    if (!src || src_len == 0)
    {
        static const unsigned char empty_gz[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
                                                 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        return push_bytes(vm, empty_gz, sizeof(empty_gz), error);
    }

    bound = mz_deflateBound(NULL, (mz_ulong)src_len);
    out = (unsigned char *)malloc(10 + bound + 8);
    if (!out)
        return push_empty_bytes(vm, error);

    /* Gzip header with proper XFL byte */
    out[0] = 0x1f;
    out[1] = 0x8b;
    out[2] = 0x08;
    out[3] = 0x00;
    out[4] = 0x00;
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = 0x00;
    out[8] = (level >= 9) ? 0x02 : (level <= 1) ? 0x04 : 0x00; /* XFL */
    out[9] = 0x03;                                             /* OS = Unix */

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (const unsigned char *)src;
    stream.avail_in = (mz_uint32)src_len;
    stream.next_out = out + 10;
    stream.avail_out = (mz_uint32)bound;

    if (mz_deflateInit2(&stream, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK)
    {
        free(out);
        return push_empty_bytes(vm, error);
    }
    if (mz_deflate(&stream, MZ_FINISH) != MZ_STREAM_END)
    {
        mz_deflateEnd(&stream);
        free(out);
        return push_empty_bytes(vm, error);
    }

    out_len = 10 + stream.total_out;
    mz_deflateEnd(&stream);

    crc = (mz_uint32)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)src, src_len);
    out[out_len++] = (unsigned char)(crc & 0xff);
    out[out_len++] = (unsigned char)((crc >> 8) & 0xff);
    out[out_len++] = (unsigned char)((crc >> 16) & 0xff);
    out[out_len++] = (unsigned char)((crc >> 24) & 0xff);
    out[out_len++] = (unsigned char)(src_len & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 8) & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 16) & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 24) & 0xff);

    ret = push_bytes(vm, out, out_len, error);
    free(out);
    return ret;
}

static vigil_status_t gzip_compress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);
    return gzip_compress_impl(vm, src, src_len, MZ_DEFAULT_COMPRESSION, error);
}

static vigil_status_t gzip_compress_level_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    int level = clamp_level((int)vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 1)));
    vigil_vm_stack_pop_n(vm, arg_count);
    return gzip_compress_impl(vm, src, src_len, level, error);
}

static vigil_status_t gzip_decompress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    size_t hdr_len = 10;
    const unsigned char *usrc;
    size_t deflate_len;
    tinfl_decompressor decomp;
    size_t dst_cap = 1024 * 64;
    size_t dst_len = 0;
    unsigned char *dst;
    size_t in_pos = 0;
    tinfl_status status;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len < 18)
    {
        return push_empty_bytes(vm, error);
    }

    usrc = (const unsigned char *)src;

    /* Verify gzip magic */
    if (usrc[0] != 0x1f || usrc[1] != 0x8b)
    {
        return push_empty_bytes(vm, error);
    }

    /* Skip optional fields */
    {
        unsigned char flags = usrc[3];
        if (flags & 0x04)
        {
            if (hdr_len + 2 > src_len)
                return push_empty_bytes(vm, error);
            hdr_len += 2 + (usrc[hdr_len] | (usrc[hdr_len + 1] << 8));
        }
        if (flags & 0x08)
        {
            while (hdr_len < src_len && usrc[hdr_len])
                hdr_len++;
            hdr_len++;
        }
        if (flags & 0x10)
        {
            while (hdr_len < src_len && usrc[hdr_len])
                hdr_len++;
            hdr_len++;
        }
        if (flags & 0x02)
            hdr_len += 2;
    }

    if (hdr_len + 8 > src_len)
    {
        return push_empty_bytes(vm, error);
    }

    deflate_len = src_len - hdr_len - 8;

    dst = (unsigned char *)malloc(dst_cap);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    tinfl_init(&decomp);

    while (in_pos < deflate_len)
    {
        size_t in_bytes = deflate_len - in_pos;
        size_t out_bytes = dst_cap - dst_len;
        int flags = (in_pos + in_bytes < deflate_len) ? TINFL_FLAG_HAS_MORE_INPUT : 0;

        status = tinfl_decompress(&decomp, usrc + hdr_len + in_pos, &in_bytes, dst, dst + dst_len, &out_bytes, flags);

        in_pos += in_bytes;
        dst_len += out_bytes;

        if (status == TINFL_STATUS_DONE)
            break;
        if (status < 0)
        {
            free(dst);
            return push_empty_bytes(vm, error);
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT || dst_len >= dst_cap)
        {
            size_t new_cap = dst_cap * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_cap);
            if (!new_dst)
            {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }

    {
        vigil_status_t ret = push_bytes(vm, dst, dst_len, error);
        free(dst);
        return ret;
    }
}

/* ── LZ4 ─────────────────────────────────────────────────────────── */

static vigil_status_t lz4_compress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    int dst_cap, compressed_size;
    char *dst;
    vigil_status_t ret;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    dst_cap = LZ4_compressBound((int)src_len);
    dst = (char *)malloc((size_t)dst_cap);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    compressed_size = LZ4_compress_default(src, dst, (int)src_len, dst_cap);
    if (compressed_size <= 0)
    {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, dst, (size_t)compressed_size, error);
    free(dst);
    return ret;
}

static vigil_status_t lz4_decompress_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    int dst_cap, decompressed_size;
    char *dst;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    /* Start with 4x source size, grow if needed */
    dst_cap = (int)(src_len * 4);
    if (dst_cap < 1024)
        dst_cap = 1024;

    dst = (char *)malloc((size_t)dst_cap);
    if (!dst)
    {
        return push_empty_bytes(vm, error);
    }

    while (1)
    {
        decompressed_size = LZ4_decompress_safe(src, dst, (int)src_len, dst_cap);
        if (decompressed_size > 0)
        {
            vigil_status_t ret = push_bytes(vm, dst, (size_t)decompressed_size, error);
            free(dst);
            return ret;
        }
        if (decompressed_size == 0)
        {
            /* Empty result */
            free(dst);
            return push_empty_bytes(vm, error);
        }
        /* Negative = error, try larger buffer */
        if (dst_cap > 256 * 1024 * 1024)
        {
            /* Give up after 256MB */
            free(dst);
            return push_empty_bytes(vm, error);
        }
        {
            int new_cap = dst_cap * 2;
            char *new_dst = (char *)realloc(dst, (size_t)new_cap);
            if (!new_dst)
            {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }
}

/* ── ZIP ─────────────────────────────────────────────────────────── */

static vigil_status_t zip_list_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_zip_archive zip;
    mz_uint i, num_files;
    vigil_object_t *arr = NULL;
    vigil_status_t s;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    if (!src || src_len == 0)
    {
        goto done;
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, src, src_len, 0))
    {
        goto done;
    }

    num_files = mz_zip_reader_get_num_files(&zip);
    for (i = 0; i < num_files; i++)
    {
        char filename[512];
        mz_zip_reader_get_filename(&zip, i, filename, sizeof(filename));

        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), filename, strlen(filename), &str_obj, error);
        if (s == VIGIL_STATUS_OK)
        {
            vigil_value_t val;
            vigil_value_init_object(&val, &str_obj);
            vigil_array_object_append(arr, &val, error);
            vigil_value_release(&val);
        }
    }
    mz_zip_reader_end(&zip);

done: {
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}
}

static vigil_status_t zip_read_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t zip_len, name_len;
    const char *zip_data, *name;
    mz_zip_archive zip;
    int file_index;
    void *data;
    size_t data_size;
    vigil_status_t ret;

    zip_data = get_bytes_data(vigil_vm_stack_get(vm, base), &zip_len);
    name = get_bytes_data(vigil_vm_stack_get(vm, base + 1), &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!zip_data || zip_len == 0 || !name || name_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, zip_data, zip_len, 0))
    {
        return push_empty_bytes(vm, error);
    }

    file_index = mz_zip_reader_locate_file(&zip, name, NULL, 0);
    if (file_index < 0)
    {
        mz_zip_reader_end(&zip);
        return push_empty_bytes(vm, error);
    }

    data = mz_zip_reader_extract_to_heap(&zip, (mz_uint)file_index, &data_size, 0);
    mz_zip_reader_end(&zip);

    if (!data)
    {
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, data, data_size, error);
    mz_free(data);
    return ret;
}

/* zip_create: takes two arrays - names and contents */
static vigil_status_t zip_create_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t names_val = vigil_vm_stack_get(vm, base);
    vigil_value_t contents_val = vigil_vm_stack_get(vm, base + 1);
    const vigil_object_t *names_obj, *contents_obj;
    mz_zip_archive zip;
    void *zip_data = NULL;
    size_t zip_size = 0;
    size_t i, count;
    vigil_status_t ret;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (!vigil_nanbox_is_object(names_val) || !vigil_nanbox_is_object(contents_val))
    {
        return push_empty_bytes(vm, error);
    }
    names_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(names_val);
    contents_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(contents_val);
    if (!names_obj || vigil_object_type(names_obj) != VIGIL_OBJECT_ARRAY || !contents_obj ||
        vigil_object_type(contents_obj) != VIGIL_OBJECT_ARRAY)
    {
        return push_empty_bytes(vm, error);
    }

    count = vigil_array_object_length(names_obj);
    if (vigil_array_object_length(contents_obj) < count)
    {
        count = vigil_array_object_length(contents_obj);
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
    {
        return push_empty_bytes(vm, error);
    }

    for (i = 0; i < count; i++)
    {
        vigil_value_t name_val, content_val;
        size_t name_len, data_len;
        const char *name, *data;

        if (!vigil_array_object_get(names_obj, i, &name_val))
            continue;
        if (!vigil_array_object_get(contents_obj, i, &content_val))
        {
            vigil_value_release(&name_val);
            continue;
        }

        name = get_bytes_data(name_val, &name_len);
        data = get_bytes_data(content_val, &data_len);
        if (!name)
        {
            vigil_value_release(&name_val);
            vigil_value_release(&content_val);
            continue;
        }

        mz_zip_writer_add_mem(&zip, name, data ? data : "", (mz_uint)data_len, MZ_DEFAULT_LEVEL);
        vigil_value_release(&name_val);
        vigil_value_release(&content_val);
    }

    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size))
    {
        mz_zip_writer_end(&zip);
        return push_empty_bytes(vm, error);
    }
    mz_zip_writer_end(&zip);

    ret = push_bytes(vm, zip_data, zip_size, error);
    mz_free(zip_data);
    return ret;
}

/* zip_create_level: takes two arrays + level */
static vigil_status_t zip_create_level_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t names_val = vigil_vm_stack_get(vm, base);
    vigil_value_t contents_val = vigil_vm_stack_get(vm, base + 1);
    int level = clamp_level((int)vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 2)));
    const vigil_object_t *names_obj, *contents_obj;
    mz_zip_archive zip;
    void *zip_data = NULL;
    size_t zip_size = 0;
    size_t i, count;
    vigil_status_t ret;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (!vigil_nanbox_is_object(names_val) || !vigil_nanbox_is_object(contents_val))
    {
        return push_empty_bytes(vm, error);
    }
    names_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(names_val);
    contents_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(contents_val);
    if (!names_obj || vigil_object_type(names_obj) != VIGIL_OBJECT_ARRAY || !contents_obj ||
        vigil_object_type(contents_obj) != VIGIL_OBJECT_ARRAY)
    {
        return push_empty_bytes(vm, error);
    }

    count = vigil_array_object_length(names_obj);
    if (vigil_array_object_length(contents_obj) < count)
    {
        count = vigil_array_object_length(contents_obj);
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
    {
        return push_empty_bytes(vm, error);
    }

    for (i = 0; i < count; i++)
    {
        vigil_value_t name_val, content_val;
        size_t name_len, data_len;
        const char *name, *data;

        if (!vigil_array_object_get(names_obj, i, &name_val))
            continue;
        if (!vigil_array_object_get(contents_obj, i, &content_val))
        {
            vigil_value_release(&name_val);
            continue;
        }

        name = get_bytes_data(name_val, &name_len);
        data = get_bytes_data(content_val, &data_len);
        if (!name)
        {
            vigil_value_release(&name_val);
            vigil_value_release(&content_val);
            continue;
        }

        mz_zip_writer_add_mem(&zip, name, data ? data : "", (mz_uint)data_len, (mz_uint)level);
        vigil_value_release(&name_val);
        vigil_value_release(&content_val);
    }

    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size))
    {
        mz_zip_writer_end(&zip);
        return push_empty_bytes(vm, error);
    }
    mz_zip_writer_end(&zip);

    ret = push_bytes(vm, zip_data, zip_size, error);
    mz_free(zip_data);
    return ret;
}

/* ── TAR ─────────────────────────────────────────────────────────── */

/* POSIX ustar header (512 bytes) */
typedef struct
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} tar_header_t;

static unsigned int tar_checksum(const tar_header_t *h)
{
    const unsigned char *p = (const unsigned char *)h;
    unsigned int sum = 0;
    size_t i;
    for (i = 0; i < 512; i++)
    {
        if (i >= 148 && i < 156)
            sum += ' '; /* checksum field treated as spaces */
        else
            sum += p[i];
    }
    return sum;
}

static size_t tar_parse_octal(const char *s, size_t len)
{
    size_t val = 0;
    size_t i;
    for (i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++)
    {
        val = val * 8 + (size_t)(s[i] - '0');
    }
    return val;
}

static void tar_write_octal(char *dst, size_t len, size_t val)
{
    size_t i = len - 1;
    dst[i--] = '\0';
    while (i > 0)
    {
        dst[i--] = '0' + (char)(val & 7);
        val >>= 3;
    }
    dst[0] = '0';
}

static vigil_status_t tar_list_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    vigil_object_t *arr = NULL;
    vigil_status_t s;
    size_t pos = 0;

    src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    while (pos + 512 <= src_len)
    {
        const tar_header_t *h = (const tar_header_t *)(src + pos);
        size_t file_size;
        char name[256];
        size_t name_len;

        if (h->name[0] == '\0')
            break; /* End of archive */

        /* Build full name from prefix + name */
        if (h->prefix[0] && memcmp(h->magic, "ustar", 5) == 0)
        {
            size_t plen = strnlen(h->prefix, 155);
            size_t nlen = strnlen(h->name, 100);
            memcpy(name, h->prefix, plen);
            name[plen] = '/';
            memcpy(name + plen + 1, h->name, nlen);
            name_len = plen + 1 + nlen;
        }
        else
        {
            name_len = strnlen(h->name, 100);
            memcpy(name, h->name, name_len);
        }
        name[name_len] = '\0';

        file_size = tar_parse_octal(h->size, 12);

        /* Add to array */
        {
            vigil_object_t *str_obj = NULL;
            s = vigil_string_object_new(vigil_vm_runtime(vm), name, name_len, &str_obj, error);
            if (s == VIGIL_STATUS_OK)
            {
                vigil_value_t val;
                vigil_value_init_object(&val, &str_obj);
                vigil_array_object_append(arr, &val, error);
                vigil_value_release(&val);
            }
        }

        pos += 512 + ((file_size + 511) & ~511);
    }

    {
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }
}

static vigil_status_t tar_read_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t tar_len, name_len;
    const char *tar_data, *name;
    size_t pos = 0;

    tar_data = get_bytes_data(vigil_vm_stack_get(vm, base), &tar_len);
    name = get_bytes_data(vigil_vm_stack_get(vm, base + 1), &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!tar_data || tar_len == 0 || !name || name_len == 0)
    {
        return push_empty_bytes(vm, error);
    }

    while (pos + 512 <= tar_len)
    {
        const tar_header_t *h = (const tar_header_t *)(tar_data + pos);
        size_t file_size;
        char entry_name[256];
        size_t entry_len;

        if (h->name[0] == '\0')
            break;

        if (h->prefix[0] && memcmp(h->magic, "ustar", 5) == 0)
        {
            size_t plen = strnlen(h->prefix, 155);
            size_t nlen = strnlen(h->name, 100);
            memcpy(entry_name, h->prefix, plen);
            entry_name[plen] = '/';
            memcpy(entry_name + plen + 1, h->name, nlen);
            entry_len = plen + 1 + nlen;
        }
        else
        {
            entry_len = strnlen(h->name, 100);
            memcpy(entry_name, h->name, entry_len);
        }
        entry_name[entry_len] = '\0';

        file_size = tar_parse_octal(h->size, 12);

        if (entry_len == name_len && memcmp(entry_name, name, name_len) == 0)
        {
            if (pos + 512 + file_size <= tar_len)
            {
                return push_bytes(vm, tar_data + pos + 512, file_size, error);
            }
            break;
        }

        pos += 512 + ((file_size + 511) & ~511);
    }

    return push_empty_bytes(vm, error);
}

static vigil_status_t tar_create_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t names_val = vigil_vm_stack_get(vm, base);
    vigil_value_t contents_val = vigil_vm_stack_get(vm, base + 1);
    const vigil_object_t *names_obj, *contents_obj;
    size_t i, count;
    unsigned char *tar_data = NULL;
    size_t tar_size = 0, tar_cap = 0;
    vigil_status_t ret;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (!vigil_nanbox_is_object(names_val) || !vigil_nanbox_is_object(contents_val))
    {
        return push_empty_bytes(vm, error);
    }
    names_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(names_val);
    contents_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(contents_val);
    if (!names_obj || vigil_object_type(names_obj) != VIGIL_OBJECT_ARRAY || !contents_obj ||
        vigil_object_type(contents_obj) != VIGIL_OBJECT_ARRAY)
    {
        return push_empty_bytes(vm, error);
    }

    count = vigil_array_object_length(names_obj);
    if (vigil_array_object_length(contents_obj) < count)
    {
        count = vigil_array_object_length(contents_obj);
    }

    for (i = 0; i < count; i++)
    {
        vigil_value_t name_val, content_val;
        size_t name_len, data_len;
        const char *name, *data;
        tar_header_t h;
        size_t padded_size, needed;
        unsigned int cksum;

        if (!vigil_array_object_get(names_obj, i, &name_val))
            continue;
        if (!vigil_array_object_get(contents_obj, i, &content_val))
        {
            vigil_value_release(&name_val);
            continue;
        }

        name = get_bytes_data(name_val, &name_len);
        data = get_bytes_data(content_val, &data_len);
        if (!name || name_len == 0)
        {
            vigil_value_release(&name_val);
            vigil_value_release(&content_val);
            continue;
        }
        if (name_len > 100)
            name_len = 100;

        memset(&h, 0, sizeof(h));
        memcpy(h.name, name, name_len);
        memcpy(h.mode, "0000644", 7);
        memcpy(h.uid, "0000000", 7);
        memcpy(h.gid, "0000000", 7);
        tar_write_octal(h.size, 12, data_len);
        tar_write_octal(h.mtime, 12, 0);
        h.typeflag = '0';
        memcpy(h.magic, "ustar", 5);
        h.version[0] = '0';
        h.version[1] = '0';

        memset(h.checksum, ' ', 8);
        cksum = tar_checksum(&h);
        snprintf(h.checksum, 8, "%06o", cksum);
        h.checksum[6] = '\0';
        h.checksum[7] = ' ';

        padded_size = (data_len + 511) & ~511;
        needed = tar_size + 512 + padded_size;
        if (needed > tar_cap)
        {
            size_t new_cap = tar_cap ? tar_cap * 2 : 4096;
            while (new_cap < needed)
                new_cap *= 2;
            unsigned char *new_data = (unsigned char *)realloc(tar_data, new_cap);
            if (!new_data)
            {
                free(tar_data);
                vigil_value_release(&name_val);
                vigil_value_release(&content_val);
                return push_empty_bytes(vm, error);
            }
            tar_data = new_data;
            tar_cap = new_cap;
        }

        memcpy(tar_data + tar_size, &h, 512);
        tar_size += 512;
        if (data && data_len > 0)
        {
            memcpy(tar_data + tar_size, data, data_len);
        }
        if (padded_size > data_len)
        {
            memset(tar_data + tar_size + data_len, 0, padded_size - data_len);
        }
        tar_size += padded_size;
        vigil_value_release(&name_val);
        vigil_value_release(&content_val);
    }

    /* Add two empty blocks at end */
    {
        size_t needed = tar_size + 1024;
        if (needed > tar_cap)
        {
            unsigned char *new_data = (unsigned char *)realloc(tar_data, needed);
            if (!new_data)
            {
                free(tar_data);
                return push_empty_bytes(vm, error);
            }
            tar_data = new_data;
        }
        memset(tar_data + tar_size, 0, 1024);
        tar_size += 1024;
    }

    ret = push_bytes(vm, tar_data, tar_size, error);
    free(tar_data);
    return ret;
}

/* ── TAR.GZ convenience ──────────────────────────────────────────── */

static vigil_status_t tar_gz_create_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Build tar in memory, then gzip it.
     * We call tar_create_fn which pops our args and pushes the tar bytes. */
    vigil_status_t s = tar_create_fn(vm, arg_count, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    /* tar_create pushed the tar bytes; pop and gzip them */
    vigil_value_t tar_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    size_t tar_len;
    const char *tar_data = get_bytes_data(tar_val, &tar_len);

    /* We need to copy tar_data before popping since the string may be freed */
    char *tar_copy = NULL;
    if (tar_data && tar_len > 0)
    {
        tar_copy = (char *)malloc(tar_len);
        if (tar_copy)
            memcpy(tar_copy, tar_data, tar_len);
    }
    vigil_vm_stack_pop_n(vm, 1);

    s = gzip_compress_impl(vm, tar_copy, tar_copy ? tar_len : 0, MZ_DEFAULT_COMPRESSION, error);
    free(tar_copy);
    return s;
}

/* ── Bounded decompression ───────────────────────────────────────── */

static vigil_status_t gzip_decompress_max_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    int64_t max_bytes = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 1));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (max_bytes <= 0 || !src || src_len < 18)
        return push_empty_bytes(vm, error);

    const unsigned char *usrc = (const unsigned char *)src;
    if (usrc[0] != 0x1f || usrc[1] != 0x8b)
        return push_empty_bytes(vm, error);

    /* Skip gzip header */
    size_t hdr_len = 10;
    {
        unsigned char flags = usrc[3];
        if (flags & 0x04)
        {
            if (hdr_len + 2 > src_len)
                return push_empty_bytes(vm, error);
            hdr_len += 2 + (usrc[hdr_len] | (usrc[hdr_len + 1] << 8));
        }
        if (flags & 0x08)
        {
            while (hdr_len < src_len && usrc[hdr_len])
                hdr_len++;
            hdr_len++;
        }
        if (flags & 0x10)
        {
            while (hdr_len < src_len && usrc[hdr_len])
                hdr_len++;
            hdr_len++;
        }
        if (flags & 0x02)
            hdr_len += 2;
    }
    if (hdr_len + 8 > src_len)
        return push_empty_bytes(vm, error);

    size_t deflate_len = src_len - hdr_len - 8;
    size_t cap = (size_t)max_bytes;
    unsigned char *dst = (unsigned char *)malloc(cap);
    if (!dst)
        return push_empty_bytes(vm, error);

    /* Use mz_stream for bounded inflate — simpler than tinfl for partial output */
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = usrc + hdr_len;
    stream.avail_in = (mz_uint32)deflate_len;
    stream.next_out = dst;
    stream.avail_out = (mz_uint32)cap;

    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
    {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    mz_inflate(&stream, MZ_FINISH);
    size_t out_len = stream.total_out;
    mz_inflateEnd(&stream);

    vigil_status_t ret = push_bytes(vm, dst, out_len, error);
    free(dst);
    return ret;
}

/* ── Gzip header info ────────────────────────────────────────────── */

static vigil_status_t push_map_str(vigil_vm_t *vm, vigil_object_t *map, const char *key, const char *val,
                                   size_t val_len, vigil_error_t *error)
{
    vigil_object_t *k_obj = NULL, *v_obj = NULL;
    vigil_status_t s;
    s = vigil_string_object_new(vigil_vm_runtime(vm), key, strlen(key), &k_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_string_object_new(vigil_vm_runtime(vm), val, val_len, &v_obj, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_object_release(&k_obj);
        return s;
    }
    vigil_value_t kv, vv;
    vigil_value_init_object(&kv, &k_obj);
    vigil_value_init_object(&vv, &v_obj);
    s = vigil_map_object_set(map, &kv, &vv, error);
    vigil_value_release(&kv);
    vigil_value_release(&vv);
    return s;
}

static vigil_status_t gzip_info_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src = get_bytes_data(vigil_vm_stack_get(vm, base), &src_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *map = NULL;
    vigil_status_t s = vigil_map_object_new(vigil_vm_runtime(vm), &map, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    if (!src || src_len < 18)
        goto done;
    const unsigned char *u = (const unsigned char *)src;
    if (u[0] != 0x1f || u[1] != 0x8b)
        goto done;

    /* Method */
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", u[2]);
        push_map_str(vm, map, "method", buf, strlen(buf), error);
    }
    /* XFL */
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", u[8]);
        push_map_str(vm, map, "xfl", buf, strlen(buf), error);
    }
    /* OS */
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", u[9]);
        push_map_str(vm, map, "os", buf, strlen(buf), error);
    }
    /* Flags */
    {
        unsigned char flags = u[3];
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", flags);
        push_map_str(vm, map, "flags", buf, strlen(buf), error);

        size_t pos = 10;
        /* FEXTRA */
        if (flags & 0x04)
        {
            if (pos + 2 <= src_len)
            {
                size_t xlen = u[pos] | (u[pos + 1] << 8);
                pos += 2 + xlen;
            }
        }
        /* FNAME */
        if (flags & 0x08)
        {
            const char *name_start = (const char *)(u + pos);
            size_t name_len = 0;
            while (pos + name_len < src_len && u[pos + name_len])
                name_len++;
            push_map_str(vm, map, "filename", name_start, name_len, error);
            pos += name_len + 1;
        }
        /* FCOMMENT */
        if (flags & 0x10)
        {
            const char *comment_start = (const char *)(u + pos);
            size_t comment_len = 0;
            while (pos + comment_len < src_len && u[pos + comment_len])
                comment_len++;
            push_map_str(vm, map, "comment", comment_start, comment_len, error);
        }
    }
    /* Original size (last 4 bytes, mod 2^32) */
    {
        uint32_t orig_size = (uint32_t)u[src_len - 4] | ((uint32_t)u[src_len - 3] << 8) |
                             ((uint32_t)u[src_len - 2] << 16) | ((uint32_t)u[src_len - 1] << 24);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", orig_size);
        push_map_str(vm, map, "size", buf, strlen(buf), error);
    }

done:;
    vigil_value_t val;
    vigil_value_init_object(&val, &map);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int bytes_param[] = {VIGIL_TYPE_STRING};
static const int two_bytes_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int two_arrays_param[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT};

static const int bytes_int_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32};

static const int two_arrays_int_param[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32};

/* Extended type info for functions that take array<string> parameters */
static const vigil_native_type_t create_params_ext[] = {VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING),
                                                        VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING)};

static const vigil_native_type_t create_level_params_ext[] = {VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING),
                                                              VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING),
                                                              VIGIL_NATIVE_TYPE_PRIMITIVE(VIGIL_TYPE_I32)};

/* Extended type info for functions that return array<string> */
static const vigil_native_type_t array_string_return = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);

/* Extended type info for map<string, string> return.
 * Note: triggers a small compiler leak (~8KB) when the compress module is
 * imported in the C test harness under ASAN. This is a pre-existing compiler
 * issue with map type interning, not a compress module bug. */
static const vigil_native_type_t map_ss_return = VIGIL_NATIVE_TYPE_MAP(VIGIL_TYPE_STRING, VIGIL_TYPE_STRING);

static const vigil_native_module_function_t compress_functions[] = {
    {"deflate_compress", 16U, deflate_compress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"deflate_compress_level", 22U, deflate_compress_level_fn, 2U, bytes_int_param, VIGIL_TYPE_STRING, 1U, NULL, 0,
     NULL, NULL},
    {"deflate_decompress", 18U, deflate_decompress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zlib_compress", 13U, zlib_compress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zlib_compress_level", 19U, zlib_compress_level_fn, 2U, bytes_int_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL,
     NULL},
    {"zlib_decompress", 15U, zlib_decompress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"gzip_compress", 13U, gzip_compress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"gzip_compress_level", 19U, gzip_compress_level_fn, 2U, bytes_int_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL,
     NULL},
    {"gzip_decompress", 15U, gzip_decompress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"lz4_compress", 12U, lz4_compress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"lz4_decompress", 14U, lz4_decompress_fn, 1U, bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"crc32", 5U, crc32_fn, 1U, bytes_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"adler32", 7U, adler32_fn, 1U, bytes_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"zip_list", 8U, zip_list_fn, 1U, bytes_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL,
     &array_string_return},
    {"zip_read", 8U, zip_read_fn, 2U, two_bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zip_create", 10U, zip_create_fn, 2U, two_arrays_param, VIGIL_TYPE_STRING, 1U, NULL, 0, create_params_ext, NULL},
    {"zip_create_level", 16U, zip_create_level_fn, 3U, two_arrays_int_param, VIGIL_TYPE_STRING, 1U, NULL, 0,
     create_level_params_ext, NULL},
    {"tar_list", 8U, tar_list_fn, 1U, bytes_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL,
     &array_string_return},
    {"tar_read", 8U, tar_read_fn, 2U, two_bytes_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"tar_create", 10U, tar_create_fn, 2U, two_arrays_param, VIGIL_TYPE_STRING, 1U, NULL, 0, create_params_ext, NULL},
    {"tar_gz_create", 13U, tar_gz_create_fn, 2U, two_arrays_param, VIGIL_TYPE_STRING, 1U, NULL, 0, create_params_ext,
     NULL},
    {"gzip_decompress_max", 19U, gzip_decompress_max_fn, 2U, bytes_int_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL,
     NULL},
    {"gzip_info", 9U, gzip_info_fn, 1U, bytes_param, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, &map_ss_return},
};

#define COMPRESS_FUNCTION_COUNT (sizeof(compress_functions) / sizeof(compress_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_compress = {
    "compress", 8U, compress_functions, COMPRESS_FUNCTION_COUNT, NULL, 0U};
