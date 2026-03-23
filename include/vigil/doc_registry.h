#ifndef VIGIL_DOC_REGISTRY_H
#define VIGIL_DOC_REGISTRY_H

#include <stddef.h>

#include "vigil/doc.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Doc Entry ────────────────────────────────────────────── */

    typedef struct vigil_doc_entry
    {
        const char *name;        /* "print" or "math.sqrt" */
        const char *signature;   /* "print(value: any) -> void" */
        const char *summary;     /* One-line description */
        const char *description; /* Full description (may be NULL) */
        const char *example;     /* Example code (may be NULL) */
    } vigil_doc_entry_t;

    /* ── Doc Registry ─────────────────────────────────────────── */

    /*
     * Lookup documentation by name.
     * Supports: "print", "math", "math.sqrt", "MyClass.method"
     * Returns NULL if not found.
     */
    VIGIL_API const vigil_doc_entry_t *vigil_doc_lookup(const char *name);

    /*
     * List all documented modules.
     * Returns array of module names, sets *count.
     */
    VIGIL_API const char **vigil_doc_list_modules(size_t *count);

    /*
     * List all entries in a module.
     * Returns array of entries, sets *count.
     * Returns NULL if module not found.
     */
    VIGIL_API const vigil_doc_entry_t *vigil_doc_list_module(const char *module_name, size_t *count);

    /*
     * Render a doc entry to formatted text.
     * Caller must free *out_text with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_doc_entry_render(const vigil_allocator_t *allocator, const vigil_doc_entry_t *entry,
                                                    char **out_text, size_t *out_length, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
