#ifndef VIGIL_AOT_H
#define VIGIL_AOT_H

#include "vigil/status.h"
#include "vigil/value.h"

typedef struct vigil_vm vigil_vm_t;
typedef struct vigil_reg_chunk vigil_reg_chunk_t;
typedef struct vigil_runtime vigil_runtime_t;
typedef struct vigil_aot_cache vigil_aot_cache_t;

typedef vigil_status_t (*vigil_aot_entry_fn)(vigil_vm_t *vm, vigil_value_t *out_value, vigil_error_t *error);

struct vigil_aot_cache
{
    void *context;
    vigil_aot_entry_fn entry;
    int generator_initialized;
};

void vigil_aot_cache_free(vigil_aot_cache_t *cache);
vigil_status_t vigil_aot_ensure(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache, vigil_error_t *error);
vigil_status_t vigil_reg_execute_cached(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                        vigil_error_t *error);
int vigil_aot_supported(void);

#endif
