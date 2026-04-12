#ifndef VIGIL_EASY_H
#define VIGIL_EASY_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Opaque state handle wrapping runtime, VM, and compilation context. */
    typedef struct vigil_state vigil_state_t;

    /* ── One-liners ──────────────────────────────────────────────
       Run a script and return the i32 exit code, or -1 on error. */

    VIGIL_API int vigil_run_string(const char *source);
    VIGIL_API int vigil_run_bytes(const void *data, size_t len);
    VIGIL_API int vigil_run_file(const char *path);

    /* ── Stateful API ────────────────────────────────────────────
       Open a state, run one or more scripts, inspect results. */

    VIGIL_API vigil_state_t *vigil_open(void);
    VIGIL_API vigil_status_t vigil_dostring(vigil_state_t *V, const char *source);
    VIGIL_API vigil_status_t vigil_dobytes(vigil_state_t *V, const void *data, size_t len);
    VIGIL_API vigil_status_t vigil_dofile(vigil_state_t *V, const char *path);
    VIGIL_API int64_t vigil_get_result_int(const vigil_state_t *V);
    VIGIL_API double vigil_get_result_float(const vigil_state_t *V);
    VIGIL_API const char *vigil_get_error(const vigil_state_t *V);
    VIGIL_API int vigil_has_error(const vigil_state_t *V);
    VIGIL_API void vigil_close(vigil_state_t *V);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_EASY_H */
