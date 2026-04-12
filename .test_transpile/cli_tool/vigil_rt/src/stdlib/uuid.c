/* uuid.c — uuid module: UUID v4 generation. */

#include <stdio.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

/* Use the vendored crypto random bytes for secure randomness. */
extern void vigil_crypto_random_bytes(unsigned char *buf, size_t len);

static vigil_status_t push_string(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *obj = NULL;
    vigil_value_t val;
    status = vigil_string_object_new(vigil_vm_runtime(vm), text, len, &obj, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &obj);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

/* uuid.v4() -> string
 * Generate a random UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * where y is one of 8, 9, a, b. */
static vigil_status_t uuid_v4(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    unsigned char bytes[16];
    char buf[37]; /* 36 chars + null */

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_crypto_random_bytes(bytes, 16);

    /* Set version 4 (0100 in bits 4-7 of byte 6). */
    bytes[6] = (unsigned char)((bytes[6] & 0x0FU) | 0x40U);
    /* Set variant 1 (10xx in bits 6-7 of byte 8). */
    bytes[8] = (unsigned char)((bytes[8] & 0x3FU) | 0x80U);

    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
             bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
             bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);

    return push_string(vm, buf, 36, error);
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const vigil_native_module_function_t uuid_functions[] = {
    {"v4", 2U, uuid_v4, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_uuid = {
    "uuid", 4U, uuid_functions, sizeof(uuid_functions) / sizeof(uuid_functions[0]), NULL, 0U, NULL, NULL, 0U};
