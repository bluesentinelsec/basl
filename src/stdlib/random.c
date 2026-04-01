/* VIGIL standard library: random module.
 *
 * Provides random number generation using xorshift128+ for quality
 * and portability. Seeded from time by default.
 */
#include <stdint.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── xorshift128+ state ──────────────────────────────────────────── */

static uint64_t rng_state[2] = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

static uint64_t xorshift128plus(void)
{
    uint64_t s1 = rng_state[0];
    uint64_t s0 = rng_state[1];
    uint64_t result = s0 + s1;
    rng_state[0] = s0;
    s1 ^= s1 << 23;
    rng_state[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
    return result;
}

/* ── random.seed(n: i32) ─────────────────────────────────────────── */

static vigil_status_t vigil_random_seed(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t v;
    int32_t seed;
    (void)arg_count;
    (void)error;

    v = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1U);
    vigil_vm_stack_pop_n(vm, 1U);
    seed = vigil_nanbox_decode_i32(v);

    rng_state[0] = (uint64_t)(uint32_t)seed;
    rng_state[1] = (uint64_t)(uint32_t)seed ^ 0x6a09e667bb67ae85ULL;
    /* Warm up */
    (void)xorshift128plus();
    (void)xorshift128plus();

    return VIGIL_STATUS_OK;
}

/* ── random.i64() -> i64 ─────────────────────────────────────────── */

static vigil_status_t vigil_random_i64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t val;
    (void)arg_count;

    val = vigil_nanbox_encode_int((int64_t)xorshift128plus());
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── random.i32() -> i32 ─────────────────────────────────────────── */

static vigil_status_t vigil_random_i32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t val;
    (void)arg_count;

    val = vigil_nanbox_encode_i32((int32_t)(xorshift128plus() >> 32));
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── random.f64() -> f64 in [0, 1) ───────────────────────────────── */

static vigil_status_t vigil_random_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t val;
    double d;
    (void)arg_count;

    /* Use upper 53 bits for full double precision */
    d = (double)(xorshift128plus() >> 11) * (1.0 / 9007199254740992.0);
    val = vigil_nanbox_encode_double(d);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── random.range(min: i32, max: i32) -> i32 ─────────────────────── */

static vigil_status_t vigil_random_range(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t v;
    int32_t min_val, max_val, result;
    uint32_t range;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    (void)arg_count;

    v = vigil_vm_stack_get(vm, base);
    min_val = vigil_nanbox_decode_i32(v);
    v = vigil_vm_stack_get(vm, base + 1);
    max_val = vigil_nanbox_decode_i32(v);
    vigil_vm_stack_pop_n(vm, 2U);

    if (max_val <= min_val)
    {
        result = min_val;
    }
    else
    {
        range = (uint32_t)(max_val - min_val);
        result = min_val + (int32_t)((uint32_t)(xorshift128plus() >> 32) % range);
    }

    v = vigil_nanbox_encode_i32(result);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── random.gaussian() -> f64 (Box-Muller, mean=0, stddev=1) ─────── */

#include <math.h>

/* Cached spare value for Box-Muller (generates pairs). */
static double gaussian_spare;
static int gaussian_has_spare = 0;

static vigil_status_t vigil_random_gaussian(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t val;
    double result;
    (void)arg_count;

    if (gaussian_has_spare)
    {
        gaussian_has_spare = 0;
        result = gaussian_spare;
    }
    else
    {
        double u1, u2, r;
        do
        {
            u1 = (double)(xorshift128plus() >> 11) * (1.0 / 9007199254740992.0);
        } while (u1 < 1e-300);
        u2 = (double)(xorshift128plus() >> 11) * (1.0 / 9007199254740992.0);
        r = sqrt(-2.0 * log(u1));
        result = r * cos(6.28318530717958647692 * u2);
        gaussian_spare = r * sin(6.28318530717958647692 * u2);
        gaussian_has_spare = 1;
    }

    val = vigil_nanbox_encode_double(result);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int seed_params[] = {VIGIL_TYPE_I32};
static const int range_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};

static const vigil_native_module_function_t vigil_random_functions[] = {
    {"seed", 4U, vigil_random_seed, 1U, seed_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"i64", 3U, vigil_random_i64, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"i32", 3U, vigil_random_i32, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"f64", 3U, vigil_random_f64, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"gaussian", 8U, vigil_random_gaussian, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"range", 5U, vigil_random_range, 2U, range_params, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
};

#define RANDOM_FUNCTION_COUNT (sizeof(vigil_random_functions) / sizeof(vigil_random_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_random = {
    "random", 6U, vigil_random_functions, RANDOM_FUNCTION_COUNT, NULL, 0U, NULL};
