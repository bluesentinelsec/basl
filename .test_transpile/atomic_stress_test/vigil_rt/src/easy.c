/* vigil_easy.c — high-level embedding API for running Vigil scripts from C. */

#include "vigil/easy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/compiler.h"
#include "vigil/diagnostic.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/value.h"
#include "vigil/vm.h"

struct vigil_state
{
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    vigil_value_t result;
    vigil_error_t error;
    int has_error;
};

/* ── internal helpers ────────────────────────────────────────────── */

static vigil_status_t state_run_source(vigil_state_t *V, const char *name, const char *text, size_t text_len)
{
    vigil_status_t status;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    vigil_source_registry_init(&registry, V->runtime);
    vigil_diagnostic_list_init(&diagnostics, V->runtime);

    status = vigil_source_registry_register(&registry, name, strlen(name), text, text_len, &source_id, &V->error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = vigil_compile_source(&registry, source_id, &function, &diagnostics, &V->error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_value_release(&V->result);
    vigil_value_init_nil(&V->result);
    status = vigil_vm_execute_function(V->vm, function, &V->result, &V->error);

cleanup:
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    V->has_error = (status != VIGIL_STATUS_OK);
    return status;
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    long len;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    *out_len = fread(buf, 1, (size_t)len, f);
    buf[*out_len] = '\0';
    fclose(f);
    return buf;
}

/* ── public API ──────────────────────────────────────────────────── */

VIGIL_API vigil_state_t *vigil_open(void)
{
    vigil_state_t *V = (vigil_state_t *)calloc(1, sizeof(*V));
    if (!V)
        return NULL;
    vigil_value_init_nil(&V->result);
    if (vigil_runtime_open(&V->runtime, NULL, &V->error) != VIGIL_STATUS_OK)
    {
        free(V);
        return NULL;
    }
    if (vigil_vm_open(&V->vm, V->runtime, NULL, &V->error) != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&V->runtime);
        free(V);
        return NULL;
    }
    return V;
}

VIGIL_API void vigil_close(vigil_state_t *V)
{
    if (!V)
        return;
    vigil_value_release(&V->result);
    vigil_vm_close(&V->vm);
    vigil_runtime_close(&V->runtime);
    free(V);
}

VIGIL_API vigil_status_t vigil_dostring(vigil_state_t *V, const char *source)
{
    if (!V || !source)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    return state_run_source(V, "main.vigil", source, strlen(source));
}

VIGIL_API vigil_status_t vigil_dobytes(vigil_state_t *V, const void *data, size_t len)
{
    if (!V || (!data && len > 0))
        return VIGIL_STATUS_INVALID_ARGUMENT;
    return state_run_source(V, "main.vigil", (const char *)data, len);
}

VIGIL_API vigil_status_t vigil_dofile(vigil_state_t *V, const char *path)
{
    size_t len = 0;
    char *text;
    vigil_status_t status;
    if (!V || !path)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    text = read_file(path, &len);
    if (!text)
    {
        V->has_error = 1;
        return VIGIL_STATUS_INTERNAL;
    }
    status = state_run_source(V, path, text, len);
    free(text);
    return status;
}

VIGIL_API int64_t vigil_get_result_int(const vigil_state_t *V)
{
    if (!V)
        return 0;
    return vigil_value_as_int(&V->result);
}

VIGIL_API double vigil_get_result_float(const vigil_state_t *V)
{
    if (!V)
        return 0.0;
    return vigil_value_as_float(&V->result);
}

VIGIL_API const char *vigil_get_error(const vigil_state_t *V)
{
    if (!V || !V->has_error)
        return NULL;
    return vigil_error_message(&V->error);
}

VIGIL_API int vigil_has_error(const vigil_state_t *V)
{
    if (!V)
        return 0;
    return V->has_error;
}

/* ── one-liners ──────────────────────────────────────────────────── */

VIGIL_API int vigil_run_string(const char *source)
{
    vigil_state_t *V;
    int code;
    if (!source)
        return -1;
    V = vigil_open();
    if (!V)
        return -1;
    if (vigil_dostring(V, source) != VIGIL_STATUS_OK)
    {
        vigil_close(V);
        return -1;
    }
    code = (int)vigil_get_result_int(V);
    vigil_close(V);
    return code;
}

VIGIL_API int vigil_run_bytes(const void *data, size_t len)
{
    vigil_state_t *V;
    int code;
    if (!data && len > 0)
        return -1;
    V = vigil_open();
    if (!V)
        return -1;
    if (vigil_dobytes(V, data, len) != VIGIL_STATUS_OK)
    {
        vigil_close(V);
        return -1;
    }
    code = (int)vigil_get_result_int(V);
    vigil_close(V);
    return code;
}

VIGIL_API int vigil_run_file(const char *path)
{
    vigil_state_t *V;
    int code;
    if (!path)
        return -1;
    V = vigil_open();
    if (!V)
        return -1;
    if (vigil_dofile(V, path) != VIGIL_STATUS_OK)
    {
        vigil_close(V);
        return -1;
    }
    code = (int)vigil_get_result_int(V);
    vigil_close(V);
    return code;
}
