#include "internal/vigil_aot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal/vigil_regvm.h"
#include "platform/platform.h"

#if defined(VIGIL_ENABLE_AOT)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#endif
#include "mir-gen.h"
#include "mir.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#define VIGIL_AOT_CACHE_STATE_EMPTY 0LL
#define VIGIL_AOT_CACHE_STATE_BUILDING 1LL
#define VIGIL_AOT_CACHE_STATE_READY 2LL

void vigil_aot_cache_free(vigil_aot_cache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

#if defined(VIGIL_ENABLE_AOT)
    if (cache->context != NULL)
    {
        MIR_context_t ctx = (MIR_context_t)cache->context;

        if (cache->generator_initialized)
        {
            MIR_gen_finish(ctx);
        }
        MIR_finish(ctx);
    }
#endif

    free(cache);
}

int vigil_aot_supported(void)
{
#if defined(VIGIL_ENABLE_AOT)
    return 1;
#else
    return 0;
#endif
}

#if defined(VIGIL_ENABLE_AOT)
static vigil_status_t vigil_aot_build(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                      vigil_error_t *error)
{
    vigil_aot_cache_t *cache = NULL;
    MIR_context_t ctx = NULL;
    MIR_module_t module;
    MIR_item_t proto;
    MIR_item_t import_item;
    MIR_item_t func;
    MIR_type_t res_type = MIR_T_I64;
    MIR_reg_t status_reg;
    MIR_reg_t vm_reg;
    MIR_reg_t out_reg;
    MIR_reg_t error_reg;
    char module_name[64];
    char func_name[64];

    if (out_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_cache = NULL;

    cache = (vigil_aot_cache_t *)calloc(1U, sizeof(*cache));
    if (cache == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    ctx = MIR_init();
    cache->context = ctx;

    (void)snprintf(module_name, sizeof(module_name), "vigil_aot_%p", (const void *)rc);
    (void)snprintf(func_name, sizeof(func_name), "vigil_entry_%p", (const void *)rc);

    module = MIR_new_module(ctx, module_name);
    proto = MIR_new_proto(ctx, "vigil_regvm_execute_proto", 1U, &res_type, 4U, MIR_T_P, "vm", MIR_T_P, "rc",
                          MIR_T_P, "out_value", MIR_T_P, "error");
    import_item = MIR_new_import(ctx, "vigil_regvm_execute");
    func = MIR_new_func(ctx, func_name, 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_P, "out_value", MIR_T_P, "error");

    status_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$status");
    vm_reg = MIR_reg(ctx, "vm", func->u.func);
    out_reg = MIR_reg(ctx, "out_value", func->u.func);
    error_reg = MIR_reg(ctx, "error", func->u.func);

    MIR_append_insn(ctx, func,
                    MIR_new_call_insn(ctx, 7U, MIR_new_ref_op(ctx, proto), MIR_new_ref_op(ctx, import_item),
                                      MIR_new_reg_op(ctx, status_reg), MIR_new_reg_op(ctx, vm_reg),
                                      MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc), MIR_new_reg_op(ctx, out_reg),
                                      MIR_new_reg_op(ctx, error_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1U, MIR_new_reg_op(ctx, status_reg)));
    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    MIR_load_module(ctx, module);
    MIR_load_external(ctx, "vigil_regvm_execute", (void *)vigil_regvm_execute);
    MIR_gen_init(ctx);
    cache->generator_initialized = 1;
    MIR_link(ctx, MIR_set_gen_interface, NULL);

    cache->entry = (vigil_aot_entry_fn)MIR_gen(ctx, func);
    if (cache->entry == NULL)
    {
        vigil_aot_cache_free(cache);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to generate MIR native entry");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_cache = cache;
    return VIGIL_STATUS_OK;
}
#endif

vigil_status_t vigil_aot_ensure(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache, vigil_error_t *error)
{
    vigil_reg_chunk_t *mutable_rc = (vigil_reg_chunk_t *)rc;

    if (out_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_cache = NULL;

    if (mutable_rc == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "reg chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

#if !defined(VIGIL_ENABLE_AOT)
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "AOT is not enabled in this build");
    return VIGIL_STATUS_UNSUPPORTED;
#else
    for (;;)
    {
        int64_t state = vigil_atomic_load(&mutable_rc->aot_cache_state);

        if (state == VIGIL_AOT_CACHE_STATE_READY)
        {
            if (mutable_rc->aot_cache == NULL)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "reg chunk AOT cache ready state was null");
                return VIGIL_STATUS_INTERNAL;
            }
            *out_cache = mutable_rc->aot_cache;
            return VIGIL_STATUS_OK;
        }

        if (state == VIGIL_AOT_CACHE_STATE_EMPTY &&
            vigil_atomic_cas(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_EMPTY, VIGIL_AOT_CACHE_STATE_BUILDING))
        {
            vigil_status_t status = vigil_aot_build(rc, out_cache, error);

            if (status != VIGIL_STATUS_OK)
            {
                vigil_atomic_store(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_EMPTY);
                return status;
            }

            mutable_rc->aot_cache = *out_cache;
            vigil_atomic_store(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_READY);
            return VIGIL_STATUS_OK;
        }

        while (vigil_atomic_load(&mutable_rc->aot_cache_state) == VIGIL_AOT_CACHE_STATE_BUILDING)
        {
        }
    }
#endif
}

vigil_status_t vigil_reg_execute_cached(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                        vigil_error_t *error)
{
    if (vm == NULL || rc == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cached reg execute arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vm->aot_enabled || vm->debug_hook != NULL || !vigil_aot_supported())
    {
        return vigil_regvm_execute(vm, rc, out_value, error);
    }

#if defined(VIGIL_ENABLE_AOT)
    vigil_aot_cache_t *cache = NULL;
    vigil_status_t status = vigil_aot_ensure(rc, &cache, error);

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (cache == NULL || cache->entry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "AOT cache missing native entry");
        return VIGIL_STATUS_INTERNAL;
    }
    return cache->entry(vm, out_value, error);
#else
    return vigil_regvm_execute(vm, rc, out_value, error);
#endif
}
