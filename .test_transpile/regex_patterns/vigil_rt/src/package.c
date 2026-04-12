#include "vigil/package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "platform/platform.h"

/* ── Allocator resolution ────────────────────────────────────────── */

static vigil_allocator_t pkg_resolve_alloc(const vigil_allocator_t *a)
{
    if (a != NULL && vigil_allocator_is_valid(a))
        return *a;
    return vigil_default_allocator();
}

#define PKG_ALLOC(a, sz) (a)->allocate((a)->user_data, (sz))
#define PKG_FREE(a, p) (a)->deallocate((a)->user_data, (p))

/* ── XOR cipher ──────────────────────────────────────────────────── */

static void xor_cipher(unsigned char *data, size_t len, const unsigned char *key, size_t key_len)
{
    size_t i;
    if (key_len == 0)
        return;
    for (i = 0; i < len; i++)
        data[i] ^= key[i % key_len];
}

static int key_is_zero(const unsigned char *key)
{
    size_t i;
    for (i = 0; i < VIGIL_PACKAGE_KEY_LEN; i++)
        if (key[i] != 0)
            return 0;
    return 1;
}

/* ── Minimal ZIP writer (store-only, no compression) ─────────────── */

static void write_u16_le(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void write_u32_le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const unsigned char *p)
{
    return (uint64_t)read_u32_le(p) | ((uint64_t)read_u32_le(p + 4) << 32);
}

static void write_u64_le(unsigned char *p, uint64_t v)
{
    write_u32_le(p, (uint32_t)(v & 0xFFFFFFFF));
    write_u32_le(p + 4, (uint32_t)(v >> 32));
}

/* CRC-32 (used by ZIP format). */
static uint32_t crc32_table[256];
static int crc32_init_done = 0;

static void crc32_init(void)
{
    uint32_t i, j, c;
    if (crc32_init_done)
        return;
    for (i = 0; i < 256; i++)
    {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320U : 0);
        crc32_table[i] = c;
    }
    crc32_init_done = 1;
}

static uint32_t crc32_compute(const unsigned char *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    size_t i;
    crc32_init();
    for (i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

/* Build a ZIP archive in memory (store-only). */
static unsigned char *zip_write_local_entry(unsigned char *p, const vigil_package_file_t *f, uint32_t *out_offset,
                                            const unsigned char *buf_start)
{
    uint32_t crc = crc32_compute((const unsigned char *)f->data, f->data_length);
    *out_offset = (uint32_t)(p - buf_start);

    write_u32_le(p, 0x04034B50);
    p += 4; /* local file header sig */
    write_u16_le(p, 20);
    p += 2; /* version needed */
    write_u16_le(p, 0);
    p += 2; /* flags */
    write_u16_le(p, 0);
    p += 2; /* compression: store */
    write_u16_le(p, 0);
    p += 2; /* mod time */
    write_u16_le(p, 0);
    p += 2; /* mod date */
    write_u32_le(p, crc);
    p += 4; /* crc-32 */
    write_u32_le(p, (uint32_t)f->data_length);
    p += 4; /* compressed size */
    write_u32_le(p, (uint32_t)f->data_length);
    p += 4; /* uncompressed size */
    write_u16_le(p, (uint16_t)f->path_length);
    p += 2; /* filename length */
    write_u16_le(p, 0);
    p += 2; /* extra field length */
    memcpy(p, f->path, f->path_length);
    p += f->path_length;
    memcpy(p, f->data, f->data_length);
    p += f->data_length;
    return p;
}

static unsigned char *zip_write_cd_entry(unsigned char *p, const vigil_package_file_t *f, uint32_t offset)
{
    uint32_t crc = crc32_compute((const unsigned char *)f->data, f->data_length);

    write_u32_le(p, 0x02014B50);
    p += 4; /* central dir sig */
    write_u16_le(p, 20);
    p += 2; /* version made by */
    write_u16_le(p, 20);
    p += 2; /* version needed */
    write_u16_le(p, 0);
    p += 2; /* flags */
    write_u16_le(p, 0);
    p += 2; /* compression */
    write_u16_le(p, 0);
    p += 2; /* mod time */
    write_u16_le(p, 0);
    p += 2; /* mod date */
    write_u32_le(p, crc);
    p += 4;
    write_u32_le(p, (uint32_t)f->data_length);
    p += 4;
    write_u32_le(p, (uint32_t)f->data_length);
    p += 4;
    write_u16_le(p, (uint16_t)f->path_length);
    p += 2;
    write_u16_le(p, 0);
    p += 2; /* extra field length */
    write_u16_le(p, 0);
    p += 2; /* comment length */
    write_u16_le(p, 0);
    p += 2; /* disk number */
    write_u16_le(p, 0);
    p += 2; /* internal attrs */
    write_u32_le(p, 0);
    p += 4; /* external attrs */
    write_u32_le(p, offset);
    p += 4; /* local header offset */
    memcpy(p, f->path, f->path_length);
    p += f->path_length;
    return p;
}

static unsigned char *zip_build(const vigil_allocator_t *a, const vigil_package_file_t *files, size_t count,
                                size_t *out_size)
{
    /* Calculate total size. */
    size_t total = 0;
    size_t cd_size = 0;
    size_t i;
    unsigned char *buf, *p, *cd_start;
    uint32_t *offsets;

    for (i = 0; i < count; i++)
    {
        total += 30 + files[i].path_length + files[i].data_length; /* local header + data */
        cd_size += 46 + files[i].path_length;                      /* central directory entry */
    }
    total += cd_size + 22; /* end of central directory */

    buf = (unsigned char *)PKG_ALLOC(a, total);
    if (buf == NULL)
    {
        *out_size = 0;
        return NULL;
    }

    offsets = (uint32_t *)PKG_ALLOC(a, count * sizeof(uint32_t));
    if (offsets == NULL)
    {
        PKG_FREE(a, buf);
        *out_size = 0;
        return NULL;
    }

    p = buf;

    /* Write local file headers + data. */
    for (i = 0; i < count; i++)
        p = zip_write_local_entry(p, &files[i], &offsets[i], buf);

    /* Write central directory. */
    cd_start = p;
    for (i = 0; i < count; i++)
        p = zip_write_cd_entry(p, &files[i], offsets[i]);

    /* End of central directory. */
    {
        uint32_t cd_total = (uint32_t)(p - cd_start);
        uint32_t cd_off = (uint32_t)(cd_start - buf);
        write_u32_le(p, 0x06054B50);
        p += 4; /* signature */
        write_u16_le(p, 0);
        p += 2; /* disk number */
        write_u16_le(p, 0);
        p += 2; /* disk with CD */
        write_u16_le(p, (uint16_t)count);
        p += 2; /* entries on disk */
        write_u16_le(p, (uint16_t)count);
        p += 2; /* total entries */
        write_u32_le(p, cd_total);
        p += 4; /* CD size */
        write_u32_le(p, cd_off);
        p += 4; /* CD offset */
        write_u16_le(p, 0);
        p += 2; /* comment length */
    }

    *out_size = (size_t)(p - buf);
    PKG_FREE(a, offsets);
    return buf;
}

/* ── ZIP reader (minimal, store-only) ────────────────────────────── */

static int zip_read(const vigil_allocator_t *a, const unsigned char *data, size_t data_len, char ***out_paths,
                    char ***out_contents, size_t **out_lengths, size_t *out_count)
{
    /* Find end of central directory. */
    size_t eocd_pos;
    uint16_t entry_count;
    uint32_t cd_offset;
    const unsigned char *cd;
    size_t i;
    char **paths, **contents;
    size_t *lengths;

    *out_paths = NULL;
    *out_contents = NULL;
    *out_lengths = NULL;
    *out_count = 0;

    if (data_len < 22)
        return 0;
    /* Search backward for EOCD signature. */
    for (eocd_pos = data_len - 22; eocd_pos > 0; eocd_pos--)
    {
        if (read_u32_le(data + eocd_pos) == 0x06054B50)
            break;
        if (data_len - eocd_pos > 65557)
            return 0; /* max comment size */
    }
    if (read_u32_le(data + eocd_pos) != 0x06054B50)
        return 0;

    entry_count = read_u16_le(data + eocd_pos + 10);
    cd_offset = read_u32_le(data + eocd_pos + 16);
    if (cd_offset >= data_len)
        return 0;

    paths = (char **)PKG_ALLOC(a, entry_count * sizeof(char *));
    contents = (char **)PKG_ALLOC(a, entry_count * sizeof(char *));
    lengths = (size_t *)PKG_ALLOC(a, entry_count * sizeof(size_t));
    if (!paths || !contents || !lengths)
    {
        PKG_FREE(a, paths);
        PKG_FREE(a, contents);
        PKG_FREE(a, lengths);
        return 0;
    }
    memset(paths, 0, entry_count * sizeof(char *));
    memset(contents, 0, entry_count * sizeof(char *));
    memset(lengths, 0, entry_count * sizeof(size_t));

    cd = data + cd_offset;
    for (i = 0; i < entry_count; i++)
    {
        uint16_t name_len, extra_len, comment_len;
        uint32_t comp_size, local_offset;
        uint16_t local_name_len, local_extra_len;
        const unsigned char *local;

        if (cd + 46 > data + data_len)
            goto fail;
        if (read_u32_le(cd) != 0x02014B50)
            goto fail;

        name_len = read_u16_le(cd + 28);
        extra_len = read_u16_le(cd + 30);
        comment_len = read_u16_le(cd + 32);
        comp_size = read_u32_le(cd + 20);
        local_offset = read_u32_le(cd + 42);

        paths[i] = (char *)PKG_ALLOC(a, name_len + 1);
        if (!paths[i])
            goto fail;
        memcpy(paths[i], cd + 46, name_len);
        paths[i][name_len] = '\0';

        /* Read data from local file header. */
        local = data + local_offset;
        if (local + 30 > data + data_len)
            goto fail;
        local_name_len = read_u16_le(local + 26);
        local_extra_len = read_u16_le(local + 28);

        contents[i] = (char *)PKG_ALLOC(a, comp_size + 1);
        if (!contents[i])
            goto fail;
        memcpy(contents[i], local + 30 + local_name_len + local_extra_len, comp_size);
        contents[i][comp_size] = '\0';
        lengths[i] = comp_size;

        cd += 46 + name_len + extra_len + comment_len;
    }

    *out_paths = paths;
    *out_contents = contents;
    *out_lengths = lengths;
    *out_count = entry_count;
    return 1;

fail:
    for (i = 0; i < entry_count; i++)
    {
        PKG_FREE(a, paths[i]);
        PKG_FREE(a, contents[i]);
    }
    PKG_FREE(a, paths);
    PKG_FREE(a, contents);
    PKG_FREE(a, lengths);
    return 0;
}

/* ── vigil_package_build ──────────────────────────────────────────── */

vigil_status_t vigil_package_build(const vigil_allocator_t *allocator, const char *output_path,
                                   const vigil_package_file_t *files, size_t file_count, const char *key,
                                   size_t key_length, vigil_error_t *error)
{
    char self_path[4096];
    char *exe_data = NULL;
    size_t exe_len = 0;
    unsigned char *zip_data = NULL;
    size_t zip_len = 0;
    unsigned char trailer[VIGIL_PACKAGE_TRAILER_LEN];
    FILE *out = NULL;
    vigil_status_t status;
    vigil_allocator_t a = pkg_resolve_alloc(allocator);

    /* Get path to current executable. */
    status = vigil_platform_self_exe(self_path, sizeof(self_path), error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Read current executable. */
    status = vigil_platform_read_file(&a, self_path, &exe_data, &exe_len, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Build ZIP archive. */
    zip_data = zip_build(&a, files, file_count, &zip_len);
    if (zip_data == NULL)
    {
        a.deallocate(a.user_data, exe_data);
        if (error)
        {
            error->type = VIGIL_STATUS_OUT_OF_MEMORY;
            error->value = "zip build failed";
            error->length = 16;
        }
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    /* XOR encrypt if key provided. */
    memset(trailer, 0, sizeof(trailer));
    if (key != NULL && key_length > 0)
    {
        size_t klen = key_length > VIGIL_PACKAGE_KEY_LEN ? VIGIL_PACKAGE_KEY_LEN : key_length;
        memcpy(trailer + 8, key, klen);
        /* Encrypt using the padded key from the trailer so decrypt matches. */
        xor_cipher(zip_data, zip_len, trailer + 8, VIGIL_PACKAGE_KEY_LEN);
    }

    /* Build trailer: [8 bytes payload len][32 bytes key][8 bytes magic] */
    write_u64_le(trailer, (uint64_t)zip_len);
    memcpy(trailer + 8 + VIGIL_PACKAGE_KEY_LEN, VIGIL_PACKAGE_MAGIC, VIGIL_PACKAGE_MAGIC_LEN);

    /* Write output. */
    out = NULL;
#ifdef _WIN32
    fopen_s(&out, output_path, "wb");
#else
    out = fopen(output_path, "wb");
#endif
    if (out == NULL)
    {
        a.deallocate(a.user_data, exe_data);
        a.deallocate(a.user_data, zip_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "cannot create output file";
            error->length = 25;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (fwrite(exe_data, 1, exe_len, out) != exe_len || fwrite(zip_data, 1, zip_len, out) != zip_len ||
        fwrite(trailer, 1, VIGIL_PACKAGE_TRAILER_LEN, out) != VIGIL_PACKAGE_TRAILER_LEN)
    {
        fclose(out);
        a.deallocate(a.user_data, exe_data);
        a.deallocate(a.user_data, zip_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "write failed";
            error->length = 12;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    fclose(out);
    a.deallocate(a.user_data, exe_data);
    a.deallocate(a.user_data, zip_data);

    vigil_platform_make_executable(output_path, NULL);

    return VIGIL_STATUS_OK;
}

/* ── vigil_package_read ───────────────────────────────────────────── */

static vigil_status_t read_bundle_from_file(const vigil_allocator_t *a, const char *path,
                                            vigil_package_bundle_t *out_bundle, vigil_error_t *error)
{
    char *file_data = NULL;
    size_t file_len = 0;
    const unsigned char *trailer;
    uint64_t payload_len;
    unsigned char key[VIGIL_PACKAGE_KEY_LEN];
    unsigned char *zip_data;
    vigil_status_t status;

    memset(out_bundle, 0, sizeof(*out_bundle));
    out_bundle->allocator = *a;

    status = vigil_platform_read_file(a, path, &file_data, &file_len, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (file_len < VIGIL_PACKAGE_TRAILER_LEN)
    {
        a->deallocate(a->user_data, file_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "no bundle trailer";
            error->length = 17;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    trailer = (const unsigned char *)file_data + file_len - VIGIL_PACKAGE_TRAILER_LEN;

    /* Check magic. */
    if (memcmp(trailer + 8 + VIGIL_PACKAGE_KEY_LEN, VIGIL_PACKAGE_MAGIC, VIGIL_PACKAGE_MAGIC_LEN) != 0)
    {
        a->deallocate(a->user_data, file_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "not a packaged binary";
            error->length = 21;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    payload_len = read_u64_le(trailer);
    memcpy(key, trailer + 8, VIGIL_PACKAGE_KEY_LEN);

    if (payload_len == 0 || payload_len > file_len - VIGIL_PACKAGE_TRAILER_LEN)
    {
        a->deallocate(a->user_data, file_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "invalid payload size";
            error->length = 20;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    /* Extract and decrypt zip data. */
    zip_data = (unsigned char *)PKG_ALLOC(a, (size_t)payload_len);
    if (zip_data == NULL)
    {
        a->deallocate(a->user_data, file_data);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(zip_data, file_data + file_len - VIGIL_PACKAGE_TRAILER_LEN - payload_len, (size_t)payload_len);
    a->deallocate(a->user_data, file_data);

    if (!key_is_zero(key))
    {
        xor_cipher(zip_data, (size_t)payload_len, key, VIGIL_PACKAGE_KEY_LEN);
    }

    /* Parse ZIP. */
    if (!zip_read(a, zip_data, (size_t)payload_len, &out_bundle->paths, &out_bundle->contents,
                  &out_bundle->content_lengths, &out_bundle->file_count))
    {
        a->deallocate(a->user_data, zip_data);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "invalid zip archive";
            error->length = 19;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    a->deallocate(a->user_data, zip_data);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_package_read(const vigil_allocator_t *allocator, const char *binary_path,
                                  vigil_package_bundle_t *out_bundle, vigil_error_t *error)
{
    if (binary_path == NULL || out_bundle == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    vigil_allocator_t a = pkg_resolve_alloc(allocator);
    return read_bundle_from_file(&a, binary_path, out_bundle, error);
}

vigil_status_t vigil_package_read_self(const vigil_allocator_t *allocator, vigil_package_bundle_t *out_bundle,
                                       vigil_error_t *error)
{
    char self_path[4096];
    vigil_status_t status;
    if (out_bundle == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    vigil_allocator_t a = pkg_resolve_alloc(allocator);
    status = vigil_platform_self_exe(self_path, sizeof(self_path), error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return read_bundle_from_file(&a, self_path, out_bundle, error);
}

void vigil_package_bundle_free(vigil_package_bundle_t *bundle)
{
    size_t i;
    if (bundle == NULL)
        return;
    vigil_allocator_t a = bundle->allocator;
    for (i = 0; i < bundle->file_count; i++)
    {
        a.deallocate(a.user_data, bundle->paths[i]);
        a.deallocate(a.user_data, bundle->contents[i]);
    }
    a.deallocate(a.user_data, bundle->paths);
    a.deallocate(a.user_data, bundle->contents);
    a.deallocate(a.user_data, bundle->content_lengths);
    memset(bundle, 0, sizeof(*bundle));
}
