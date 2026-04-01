/* VIGIL standard library: math module.
 *
 * All functions use only C11 <math.h> — fully platform-universal.
 */
#include <math.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static double vigil_math_pop_f64(vigil_vm_t *vm)
{
    vigil_value_t v = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1U);
    vigil_vm_stack_pop_n(vm, 1U);
    return vigil_nanbox_decode_double(v);
}

static vigil_status_t vigil_math_push_f64(vigil_vm_t *vm, double d, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_double(d);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t vigil_math_push_bool(vigil_vm_t *vm, int b, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_bool(&val, b);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── f64 -> f64 callbacks ────────────────────────────────────────── */

#define MATH_UNARY(name, cfn)                                                                                          \
    static vigil_status_t vigil_math_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                    \
    {                                                                                                                  \
        double a;                                                                                                      \
        (void)arg_count;                                                                                               \
        a = vigil_math_pop_f64(vm);                                                                                    \
        return vigil_math_push_f64(vm, cfn(a), error);                                                                 \
    }

MATH_UNARY(floor, floor)
MATH_UNARY(ceil, ceil)
MATH_UNARY(round, round)
MATH_UNARY(abs, fabs)
MATH_UNARY(sqrt, sqrt)
MATH_UNARY(cbrt, cbrt)
MATH_UNARY(sin, sin)
MATH_UNARY(cos, cos)
MATH_UNARY(tan, tan)
MATH_UNARY(log, log)
MATH_UNARY(log2, log2)
MATH_UNARY(log10, log10)
MATH_UNARY(exp, exp)
MATH_UNARY(trunc, trunc)

/* ── () -> f64 callbacks ─────────────────────────────────────────── */

static vigil_status_t vigil_math_pi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return vigil_math_push_f64(vm, 3.14159265358979323846, error);
}

static vigil_status_t vigil_math_e(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return vigil_math_push_f64(vm, 2.71828182845904523536, error);
}

static vigil_status_t vigil_math_tau(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return vigil_math_push_f64(vm, 6.28318530717958647692, error);
}

static vigil_status_t vigil_math_epsilon(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return vigil_math_push_f64(vm, 2.2204460492503131e-16, error);
}

/* ── (f64) -> bool callbacks ─────────────────────────────────────── */

static vigil_status_t vigil_math_is_nan(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double a = vigil_math_pop_f64(vm);
    return vigil_math_push_bool(vm, isnan(a) != 0, error);
}

static vigil_status_t vigil_math_is_inf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double a = vigil_math_pop_f64(vm);
    return vigil_math_push_bool(vm, isinf(a) != 0, error);
}

static vigil_status_t vigil_math_is_finite(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double a = vigil_math_pop_f64(vm);
    return vigil_math_push_bool(vm, isfinite(a) != 0, error);
}

/* ── (f64, f64) -> f64 callbacks ─────────────────────────────────── */

static vigil_status_t vigil_math_pow(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double base_val, exp_val;
    (void)arg_count;
    exp_val = vigil_math_pop_f64(vm);
    base_val = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, pow(base_val, exp_val), error);
}

static vigil_status_t vigil_math_min(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double a, b;
    (void)arg_count;
    b = vigil_math_pop_f64(vm);
    a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, fmin(a, b), error);
}

static vigil_status_t vigil_math_max(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double a, b;
    (void)arg_count;
    b = vigil_math_pop_f64(vm);
    a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, fmax(a, b), error);
}

static vigil_status_t vigil_math_atan2(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double y, x;
    (void)arg_count;
    x = vigil_math_pop_f64(vm);
    y = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, atan2(y, x), error);
}

static vigil_status_t vigil_math_hypot(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double a, b;
    (void)arg_count;
    b = vigil_math_pop_f64(vm);
    a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, hypot(a, b), error);
}

static vigil_status_t vigil_math_fmod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double a, b;
    (void)arg_count;
    b = vigil_math_pop_f64(vm);
    a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, fmod(a, b), error);
}

static vigil_status_t vigil_math_sign(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double a;
    (void)arg_count;
    a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, (a > 0.0) - (a < 0.0), error);
}

MATH_UNARY(asin, asin)
MATH_UNARY(acos, acos)
MATH_UNARY(atan, atan)

static vigil_status_t vigil_math_deg2rad(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double d = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, d * 3.14159265358979323846 / 180.0, error);
}

static vigil_status_t vigil_math_rad2deg(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double r = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, r * 180.0 / 3.14159265358979323846, error);
}

/* ── (f64, f64, f64) -> f64 callbacks ────────────────────────────── */

static vigil_status_t vigil_math_clamp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    double val, lo, hi;
    (void)arg_count;
    hi = vigil_math_pop_f64(vm);
    lo = vigil_math_pop_f64(vm);
    val = vigil_math_pop_f64(vm);
    if (val < lo)
        val = lo;
    if (val > hi)
        val = hi;
    return vigil_math_push_f64(vm, val, error);
}

static vigil_status_t vigil_math_lerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double t = vigil_math_pop_f64(vm);
    double b = vigil_math_pop_f64(vm);
    double a = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, a + (b - a) * t, error);
}

static vigil_status_t vigil_math_normalize(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double end = vigil_math_pop_f64(vm);
    double start = vigil_math_pop_f64(vm);
    double val = vigil_math_pop_f64(vm);
    double range = end - start;
    return vigil_math_push_f64(vm, (range == 0.0) ? 0.0 : (val - start) / range, error);
}

static vigil_status_t vigil_math_wrap(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double hi = vigil_math_pop_f64(vm);
    double lo = vigil_math_pop_f64(vm);
    double val = vigil_math_pop_f64(vm);
    double range = hi - lo;
    if (range == 0.0)
        return vigil_math_push_f64(vm, lo, error);
    double result = fmod(val - lo, range);
    if (result < 0.0)
        result += range;
    return vigil_math_push_f64(vm, result + lo, error);
}

/* ── (f64, f64, f64, f64, f64) -> f64 callbacks ─────────────────── */

static const int vigil_math_f64x5_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                              VIGIL_TYPE_F64};

static vigil_status_t vigil_math_remap(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double out_end = vigil_math_pop_f64(vm);
    double out_start = vigil_math_pop_f64(vm);
    double in_end = vigil_math_pop_f64(vm);
    double in_start = vigil_math_pop_f64(vm);
    double val = vigil_math_pop_f64(vm);
    double in_range = in_end - in_start;
    double t = (in_range == 0.0) ? 0.0 : (val - in_start) / in_range;
    return vigil_math_push_f64(vm, out_start + (out_end - out_start) * t, error);
}

static vigil_status_t vigil_math_inverselerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double val = vigil_math_pop_f64(vm);
    double b = vigil_math_pop_f64(vm);
    double a = vigil_math_pop_f64(vm);
    double range = b - a;
    return vigil_math_push_f64(vm, (range == 0.0) ? 0.0 : (val - a) / range, error);
}

static vigil_status_t vigil_math_smoothstep(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double x = vigil_math_pop_f64(vm);
    double edge1 = vigil_math_pop_f64(vm);
    double edge0 = vigil_math_pop_f64(vm);
    double range = edge1 - edge0;
    double t = (range == 0.0) ? 0.0 : (x - edge0) / range;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    return vigil_math_push_f64(vm, t * t * (3.0 - 2.0 * t), error);
}

static vigil_status_t vigil_math_step(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    double x = vigil_math_pop_f64(vm);
    double edge = vigil_math_pop_f64(vm);
    return vigil_math_push_f64(vm, (x >= edge) ? 1.0 : 0.0, error);
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int vigil_math_f64_params[] = {VIGIL_TYPE_F64};
static const int vigil_math_f64f64_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int vigil_math_f64f64f64_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};

#define MATH_FN0(id, n, nl) {n, nl, vigil_math_##id, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

#define MATH_FN1(id, n, nl)                                                                                            \
    {n, nl, vigil_math_##id, 1U, vigil_math_f64_params, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

#define MATH_FN1_BOOL(id, n, nl)                                                                                       \
    {n, nl, vigil_math_##id, 1U, vigil_math_f64_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

#define MATH_FN2(id, n, nl)                                                                                            \
    {n, nl, vigil_math_##id, 2U, vigil_math_f64f64_params, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

#define MATH_FN3(id, n, nl)                                                                                            \
    {n, nl, vigil_math_##id, 3U, vigil_math_f64f64f64_params, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

#define MATH_FN5(id, n, nl)                                                                                            \
    {n, nl, vigil_math_##id, 5U, vigil_math_f64x5_params, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}

static const vigil_native_module_function_t vigil_math_functions[] = {
    MATH_FN0(pi, "pi", 2U),
    MATH_FN0(e, "e", 1U),
    MATH_FN0(tau, "tau", 3U),
    MATH_FN0(epsilon, "epsilon", 7U),
    MATH_FN1(floor, "floor", 5U),
    MATH_FN1(ceil, "ceil", 4U),
    MATH_FN1(round, "round", 5U),
    MATH_FN1(trunc, "trunc", 5U),
    MATH_FN1(abs, "abs", 3U),
    MATH_FN1(sign, "sign", 4U),
    MATH_FN1(sqrt, "sqrt", 4U),
    MATH_FN1(cbrt, "cbrt", 4U),
    MATH_FN1(sin, "sin", 3U),
    MATH_FN1(cos, "cos", 3U),
    MATH_FN1(tan, "tan", 3U),
    MATH_FN1(asin, "asin", 4U),
    MATH_FN1(acos, "acos", 4U),
    MATH_FN1(atan, "atan", 4U),
    MATH_FN1(log, "log", 3U),
    MATH_FN1(log2, "log2", 4U),
    MATH_FN1(log10, "log10", 5U),
    MATH_FN1(exp, "exp", 3U),
    MATH_FN1(deg2rad, "deg2rad", 7U),
    MATH_FN1(rad2deg, "rad2deg", 7U),
    MATH_FN2(pow, "pow", 3U),
    MATH_FN2(min, "min", 3U),
    MATH_FN2(max, "max", 3U),
    MATH_FN2(atan2, "atan2", 5U),
    MATH_FN2(hypot, "hypot", 5U),
    MATH_FN2(fmod, "fmod", 4U),
    MATH_FN2(step, "step", 4U),
    MATH_FN1_BOOL(is_nan, "isNaN", 5U),
    MATH_FN1_BOOL(is_inf, "isInf", 5U),
    MATH_FN1_BOOL(is_finite, "isFinite", 8U),
    MATH_FN3(clamp, "clamp", 5U),
    MATH_FN3(lerp, "lerp", 4U),
    MATH_FN3(inverselerp, "inverseLerp", 11U),
    MATH_FN3(smoothstep, "smoothstep", 10U),
    MATH_FN3(normalize, "normalize", 9U),
    MATH_FN3(wrap, "wrap", 4U),
    MATH_FN5(remap, "remap", 5U),
};

#define VIGIL_MATH_FUNCTION_COUNT (sizeof(vigil_math_functions) / sizeof(vigil_math_functions[0]))

/* ── Vec2 class ──────────────────────────────────────────────────── */

/*
 * Vec2/Vec3 methods receive self (the instance) as the first stack arg.
 * self is at stack[base], additional args follow.
 */

/* Helper: extract f64 field from an instance at a given stack slot. */
static double vigil_vec_get_field(vigil_vm_t *vm, size_t slot, size_t idx)
{
    vigil_value_t val = vigil_vm_stack_get(vm, slot);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, idx, &field);
    double result = vigil_nanbox_decode_double(field);
    vigil_value_release(&field);
    return result;
}

/* Forward declarations for cross-class helpers. */
static vigil_object_t *vigil_mat4_get_data(vigil_vm_t *vm, size_t slot);
static double vigil_mat4_read(vigil_object_t *arr, size_t idx);
static vigil_status_t vigil_mat4_push_new(vigil_vm_t *vm, const double m[16], size_t class_index, vigil_error_t *error);

/* Helper: push a new Vec2 instance with given x, y. */
static vigil_status_t vigil_vec2_push_new(vigil_vm_t *vm, double x, double y, size_t class_index, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t fields[2];
    vigil_object_t *inst;
    vigil_value_t result;
    vigil_status_t s;
    fields[0] = vigil_nanbox_encode_double(x);
    fields[1] = vigil_nanbox_encode_double(y);
    s = vigil_instance_object_new(rt, class_index, fields, 2U, &inst, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&result, &inst);
    s = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return s;
}

/* Get class_index from self instance. */
static size_t vigil_vec_self_class(vigil_vm_t *vm, size_t base)
{
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    return vigil_instance_object_class_index(obj);
}

static vigil_status_t vigil_vec2_length(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(x * x + y * y), error);
}

static vigil_status_t vigil_vec2_dot(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, x1 * x2 + y1 * y2, error);
}

static vigil_status_t vigil_vec2_vnormalize(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    double len = sqrt(x * x + y * y);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0)
        return vigil_vec2_push_new(vm, 0.0, 0.0, ci, error);
    return vigil_vec2_push_new(vm, x / len, y / len, ci, error);
}

static vigil_status_t vigil_vec2_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, x1 + x2, y1 + y2, ci, error);
}

static vigil_status_t vigil_vec2_sub(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, x1 - x2, y1 - y2, ci, error);
}

static vigil_status_t vigil_vec2_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_value_t sv = vigil_vm_stack_get(vm, base + 1U);
    double s = vigil_nanbox_decode_double(sv);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, x * s, y * s, ci, error);
}

static vigil_status_t vigil_vec2_distance(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double dx = vigil_vec_get_field(vm, base, 0U) - vigil_vec_get_field(vm, base + 1U, 0U);
    double dy = vigil_vec_get_field(vm, base, 1U) - vigil_vec_get_field(vm, base + 1U, 1U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(dx * dx + dy * dy), error);
}

static vigil_status_t vigil_vec2_lengthsqr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, x * x + y * y, error);
}

static vigil_status_t vigil_vec2_negate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, -x, -y, ci, error);
}

static vigil_status_t vigil_vec2_vlerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    vigil_value_t tv = vigil_vm_stack_get(vm, base + 2U);
    double t = vigil_nanbox_decode_double(tv);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, ci, error);
}

static vigil_status_t vigil_vec2_reflect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double vx = vigil_vec_get_field(vm, base, 0U);
    double vy = vigil_vec_get_field(vm, base, 1U);
    double nx = vigil_vec_get_field(vm, base + 1U, 0U);
    double ny = vigil_vec_get_field(vm, base + 1U, 1U);
    double d2 = 2.0 * (vx * nx + vy * ny);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, vx - d2 * nx, vy - d2 * ny, ci, error);
}

/* Helper: primitive field descriptor (object_kind=0, no class/element). */
#define VIGIL_PFIELD(n, nl, t) {n, nl, t, 0, NULL, 0U, 0, NULL, NULL}

/* Helper: instance method descriptor (is_static=0). */
#define VIGIL_METHOD(n, nl, fn, pc, pt, rt, rc, rts)                                                                   \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL}

/* Helper: static method descriptor (is_static=1). */
#define VIGIL_STATIC(n, nl, fn, pc, pt, rt, rc, rts)                                                                   \
    {n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0, NULL, NULL, NULL, NULL}

/* Helper: instance method returning a different class. */
#define VIGIL_METHOD_RET(n, nl, fn, pc, pt, rt, rc, rts, cn, cnl)                                                     \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, cn, cnl, 0, NULL, NULL, NULL, NULL}

/* Helper: static method returning a different class. */
#define VIGIL_STATIC_RET(n, nl, fn, pc, pt, rt, rc, rts, cn, cnl)                                                     \
    {n, nl, fn, pc, pt, rt, rc, rts, 1, cn, cnl, 0, NULL, NULL, NULL, NULL}

/* Helper: read class_index from hidden first arg (static methods). */
static size_t vigil_static_class_index(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    return (size_t)vigil_nanbox_decode_i32(v);
}

/* Vec2 angle: atan2(y, x) */
static vigil_status_t vigil_vec2_angle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, atan2(y, x), error);
}

/* Vec2 rotate by angle (radians) */
static vigil_status_t vigil_vec2_rotate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_value_t av = vigil_vm_stack_get(vm, base + 1U);
    double a = vigil_nanbox_decode_double(av);
    double c = cos(a), s = sin(a);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, x * c - y * s, x * s + y * c, ci, error);
}

/* Vec2.zero() */
static vigil_status_t vigil_vec2_zero(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, 0.0, 0.0, ci, error);
}

/* Vec2.one() */
static vigil_status_t vigil_vec2_one(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec2_push_new(vm, 1.0, 1.0, ci, error);
}

static const vigil_native_class_field_t vigil_vec2_fields[] = {
    VIGIL_PFIELD("x", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("y", 1U, VIGIL_TYPE_F64),
};

static const int vigil_vec_obj_params[] = {VIGIL_TYPE_OBJECT};
static const int vigil_vec_f64_params[] = {VIGIL_TYPE_F64};
static const int vigil_vec_obj_f64_params[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64};

static const vigil_native_class_method_t vigil_vec2_methods[] = {
    VIGIL_STATIC("zero", 4U, vigil_vec2_zero, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("one", 3U, vigil_vec2_one, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("length", 6U, vigil_vec2_length, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("lengthSqr", 9U, vigil_vec2_lengthsqr, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("dot", 3U, vigil_vec2_dot, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("distance", 8U, vigil_vec2_distance, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("normalize", 9U, vigil_vec2_vnormalize, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("negate", 6U, vigil_vec2_negate, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("add", 3U, vigil_vec2_add, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("sub", 3U, vigil_vec2_sub, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("scale", 5U, vigil_vec2_scale, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("lerp", 4U, vigil_vec2_vlerp, 2U, vigil_vec_obj_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("reflect", 7U, vigil_vec2_reflect, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("angle", 5U, vigil_vec2_angle, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("rotate", 6U, vigil_vec2_rotate, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
};

/* ── Vec3 class ──────────────────────────────────────────────────── */

static vigil_status_t vigil_vec3_push_new(vigil_vm_t *vm, double x, double y, double z, size_t class_index,
                                          vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t fields[3];
    vigil_object_t *inst;
    vigil_value_t result;
    vigil_status_t s;
    fields[0] = vigil_nanbox_encode_double(x);
    fields[1] = vigil_nanbox_encode_double(y);
    fields[2] = vigil_nanbox_encode_double(z);
    s = vigil_instance_object_new(rt, class_index, fields, 3U, &inst, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&result, &inst);
    s = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return s;
}

static vigil_status_t vigil_vec3_length(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(x * x + y * y + z * z), error);
}

static vigil_status_t vigil_vec3_dot(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, x1 * x2 + y1 * y2 + z1 * z2, error);
}

static vigil_status_t vigil_vec3_cross(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, y1 * z2 - z1 * y2, z1 * x2 - x1 * z2, x1 * y2 - y1 * x2, ci, error);
}

static vigil_status_t vigil_vec3_vnormalize(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    double len = sqrt(x * x + y * y + z * z);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0)
        return vigil_vec3_push_new(vm, 0.0, 0.0, 0.0, ci, error);
    return vigil_vec3_push_new(vm, x / len, y / len, z / len, ci, error);
}

static vigil_status_t vigil_vec3_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, x1 + x2, y1 + y2, z1 + z2, ci, error);
}

static vigil_status_t vigil_vec3_sub(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, x1 - x2, y1 - y2, z1 - z2, ci, error);
}

static vigil_status_t vigil_vec3_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_value_t sv = vigil_vm_stack_get(vm, base + 1U);
    double s = vigil_nanbox_decode_double(sv);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, x * s, y * s, z * s, ci, error);
}

static vigil_status_t vigil_vec3_distance(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double dx = vigil_vec_get_field(vm, base, 0U) - vigil_vec_get_field(vm, base + 1U, 0U);
    double dy = vigil_vec_get_field(vm, base, 1U) - vigil_vec_get_field(vm, base + 1U, 1U);
    double dz = vigil_vec_get_field(vm, base, 2U) - vigil_vec_get_field(vm, base + 1U, 2U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(dx * dx + dy * dy + dz * dz), error);
}

static vigil_status_t vigil_vec3_lengthsqr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, x * x + y * y + z * z, error);
}

static vigil_status_t vigil_vec3_negate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, -x, -y, -z, ci, error);
}

static vigil_status_t vigil_vec3_vlerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    vigil_value_t tv = vigil_vm_stack_get(vm, base + 2U);
    double t = vigil_nanbox_decode_double(tv);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, z1 + (z2 - z1) * t, ci, error);
}

static vigil_status_t vigil_vec3_reflect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double vx = vigil_vec_get_field(vm, base, 0U);
    double vy = vigil_vec_get_field(vm, base, 1U);
    double vz = vigil_vec_get_field(vm, base, 2U);
    double nx = vigil_vec_get_field(vm, base + 1U, 0U);
    double ny = vigil_vec_get_field(vm, base + 1U, 1U);
    double nz = vigil_vec_get_field(vm, base + 1U, 2U);
    double d2 = 2.0 * (vx * nx + vy * ny + vz * nz);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, vx - d2 * nx, vy - d2 * ny, vz - d2 * nz, ci, error);
}

static vigil_status_t vigil_vec3_angle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    double dot = x1 * x2 + y1 * y2 + z1 * z2;
    double len1 = sqrt(x1 * x1 + y1 * y1 + z1 * z1);
    double len2 = sqrt(x2 * x2 + y2 * y2 + z2 * z2);
    double denom = len1 * len2;
    double cosA;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (denom == 0.0)
        return vigil_math_push_f64(vm, 0.0, error);
    cosA = dot / denom;
    if (cosA < -1.0)
        cosA = -1.0;
    if (cosA > 1.0)
        cosA = 1.0;
    return vigil_math_push_f64(vm, acos(cosA), error);
}

static const vigil_native_class_field_t vigil_vec3_fields[] = {
    VIGIL_PFIELD("x", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("y", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("z", 1U, VIGIL_TYPE_F64),
};

/* Vec3 transform by Mat4: result = M * [x,y,z,1], perspective divide */
static vigil_status_t vigil_vec3_transform(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_object_t *arr = vigil_mat4_get_data(vm, base + 1U);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(arr, i);
    double rx = m[0] * x + m[4] * y + m[8] * z + m[12];
    double ry = m[1] * x + m[5] * y + m[9] * z + m[13];
    double rz = m[2] * x + m[6] * y + m[10] * z + m[14];
    double rw = m[3] * x + m[7] * y + m[11] * z + m[15];
    vigil_vm_stack_pop_n(vm, arg_count);
    if (rw != 0.0 && rw != 1.0)
    {
        rx /= rw;
        ry /= rw;
        rz /= rw;
    }
    return vigil_vec3_push_new(vm, rx, ry, rz, ci, error);
}

/* Vec3 rotate by Quaternion: v' = q * v * q^-1 */
static vigil_status_t vigil_vec3_rotate_by_quat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double vx = vigil_vec_get_field(vm, base, 0U);
    double vy = vigil_vec_get_field(vm, base, 1U);
    double vz = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    double qx = vigil_vec_get_field(vm, base + 1U, 0U);
    double qy = vigil_vec_get_field(vm, base + 1U, 1U);
    double qz = vigil_vec_get_field(vm, base + 1U, 2U);
    double qw = vigil_vec_get_field(vm, base + 1U, 3U);
    /* v' = v + 2*w*(u x v) + 2*(u x (u x v)) where u=(qx,qy,qz), w=qw */
    double cx1 = qy * vz - qz * vy, cy1 = qz * vx - qx * vz, cz1 = qx * vy - qy * vx;
    double cx2 = qy * cz1 - qz * cy1, cy2 = qz * cx1 - qx * cz1, cz2 = qx * cy1 - qy * cx1;
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, vx + 2.0 * (qw * cx1 + cx2), vy + 2.0 * (qw * cy1 + cy2),
                               vz + 2.0 * (qw * cz1 + cz2), ci, error);
}

/* Vec3 unproject: screen coords -> world coords.
 * Args: projection matrix, view matrix. */
static vigil_status_t vigil_vec3_unproject(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double sx = vigil_vec_get_field(vm, base, 0U);
    double sy = vigil_vec_get_field(vm, base, 1U);
    double sz = vigil_vec_get_field(vm, base, 2U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_object_t *parr = vigil_mat4_get_data(vm, base + 1U);
    vigil_object_t *varr = vigil_mat4_get_data(vm, base + 2U);
    double p[16], v[16], vp[16], inv[16];
    size_t i;
    int c, r, k;
    for (i = 0; i < 16; i++)
    {
        p[i] = vigil_mat4_read(parr, i);
        v[i] = vigil_mat4_read(varr, i);
    }
    /* vp = v * p */
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
        {
            double sum = 0;
            for (k = 0; k < 4; k++)
                sum += v[k * 4 + r] * p[c * 4 + k];
            vp[c * 4 + r] = sum;
        }
    /* invert vp (Gauss-Jordan) */
    {
        double aug[4][8];
        int row, col, piv;
        for (row = 0; row < 4; row++)
            for (col = 0; col < 4; col++)
            {
                aug[row][col] = vp[col * 4 + row];
                aug[row][col + 4] = (row == col) ? 1.0 : 0.0;
            }
        for (piv = 0; piv < 4; piv++)
        {
            int best = piv;
            double bv = fabs(aug[piv][piv]);
            for (row = piv + 1; row < 4; row++)
                if (fabs(aug[row][piv]) > bv)
                {
                    best = row;
                    bv = fabs(aug[row][piv]);
                }
            if (best != piv)
                for (col = 0; col < 8; col++)
                {
                    double t = aug[piv][col];
                    aug[piv][col] = aug[best][col];
                    aug[best][col] = t;
                }
            if (aug[piv][piv] == 0.0)
                break;
            double d = aug[piv][piv];
            for (col = 0; col < 8; col++)
                aug[piv][col] /= d;
            for (row = 0; row < 4; row++)
                if (row != piv)
                {
                    double f = aug[row][piv];
                    for (col = 0; col < 8; col++)
                        aug[row][col] -= f * aug[piv][col];
                }
        }
        for (row = 0; row < 4; row++)
            for (col = 0; col < 4; col++)
                inv[col * 4 + row] = aug[row][col + 4];
    }
    /* Normalize screen coords to [-1,1] and transform */
    double nx = sx * 2.0 - 1.0, ny = sy * 2.0 - 1.0, nz = sz * 2.0 - 1.0;
    double rx = inv[0] * nx + inv[4] * ny + inv[8] * nz + inv[12];
    double ry = inv[1] * nx + inv[5] * ny + inv[9] * nz + inv[13];
    double rz = inv[2] * nx + inv[6] * ny + inv[10] * nz + inv[14];
    double rw = inv[3] * nx + inv[7] * ny + inv[11] * nz + inv[15];
    vigil_vm_stack_pop_n(vm, arg_count);
    if (rw != 0.0)
    {
        rx /= rw;
        ry /= rw;
        rz /= rw;
    }
    return vigil_vec3_push_new(vm, rx, ry, rz, ci, error);
}

static const int vigil_vec_obj_obj_params[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT};

/* Vec3.zero() */
static vigil_status_t vigil_vec3_zero(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, 0.0, 0.0, 0.0, ci, error);
}

/* Vec3.one() */
static vigil_status_t vigil_vec3_one(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec3_push_new(vm, 1.0, 1.0, 1.0, ci, error);
}

static const vigil_native_class_method_t vigil_vec3_methods[] = {
    VIGIL_STATIC("zero", 4U, vigil_vec3_zero, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("one", 3U, vigil_vec3_one, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("length", 6U, vigil_vec3_length, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("lengthSqr", 9U, vigil_vec3_lengthsqr, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("dot", 3U, vigil_vec3_dot, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("distance", 8U, vigil_vec3_distance, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("angle", 5U, vigil_vec3_angle, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("cross", 5U, vigil_vec3_cross, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("normalize", 9U, vigil_vec3_vnormalize, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("negate", 6U, vigil_vec3_negate, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("add", 3U, vigil_vec3_add, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("sub", 3U, vigil_vec3_sub, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("scale", 5U, vigil_vec3_scale, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("lerp", 4U, vigil_vec3_vlerp, 2U, vigil_vec_obj_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("reflect", 7U, vigil_vec3_reflect, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("transform", 9U, vigil_vec3_transform, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("rotateByQuaternion", 18U, vigil_vec3_rotate_by_quat, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U,
                 NULL),
    VIGIL_METHOD("unproject", 9U, vigil_vec3_unproject, 2U, vigil_vec_obj_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
};

/* ── Vec4 class ──────────────────────────────────────────────────── */

static vigil_status_t vigil_vec4_push_new(vigil_vm_t *vm, double x, double y, double z, double w, size_t class_index,
                                          vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t fields[4];
    vigil_object_t *inst;
    vigil_value_t result;
    vigil_status_t s;
    fields[0] = vigil_nanbox_encode_double(x);
    fields[1] = vigil_nanbox_encode_double(y);
    fields[2] = vigil_nanbox_encode_double(z);
    fields[3] = vigil_nanbox_encode_double(w);
    s = vigil_instance_object_new(rt, class_index, fields, 4U, &inst, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&result, &inst);
    s = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return s;
}

static vigil_status_t vigil_vec4_length(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(x * x + y * y + z * z + w * w), error);
}

static vigil_status_t vigil_vec4_lengthsqr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, x * x + y * y + z * z + w * w, error);
}

static vigil_status_t vigil_vec4_dot(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double r = vigil_vec_get_field(vm, base, 0U) * vigil_vec_get_field(vm, base + 1U, 0U) +
               vigil_vec_get_field(vm, base, 1U) * vigil_vec_get_field(vm, base + 1U, 1U) +
               vigil_vec_get_field(vm, base, 2U) * vigil_vec_get_field(vm, base + 1U, 2U) +
               vigil_vec_get_field(vm, base, 3U) * vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, r, error);
}

static vigil_status_t vigil_vec4_distance(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double dx = vigil_vec_get_field(vm, base, 0U) - vigil_vec_get_field(vm, base + 1U, 0U);
    double dy = vigil_vec_get_field(vm, base, 1U) - vigil_vec_get_field(vm, base + 1U, 1U);
    double dz = vigil_vec_get_field(vm, base, 2U) - vigil_vec_get_field(vm, base + 1U, 2U);
    double dw = vigil_vec_get_field(vm, base, 3U) - vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(dx * dx + dy * dy + dz * dz + dw * dw), error);
}

static vigil_status_t vigil_vec4_vnormalize(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    double len = sqrt(x * x + y * y + z * z + w * w);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0)
        return vigil_vec4_push_new(vm, 0, 0, 0, 0, ci, error);
    return vigil_vec4_push_new(vm, x / len, y / len, z / len, w / len, ci, error);
}

static vigil_status_t vigil_vec4_negate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_vec_self_class(vm, base);
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, -x, -y, -z, -w, ci, error);
}

static vigil_status_t vigil_vec4_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_vec_self_class(vm, base);
    double x = vigil_vec_get_field(vm, base, 0U) + vigil_vec_get_field(vm, base + 1U, 0U);
    double y = vigil_vec_get_field(vm, base, 1U) + vigil_vec_get_field(vm, base + 1U, 1U);
    double z = vigil_vec_get_field(vm, base, 2U) + vigil_vec_get_field(vm, base + 1U, 2U);
    double w = vigil_vec_get_field(vm, base, 3U) + vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, x, y, z, w, ci, error);
}

static vigil_status_t vigil_vec4_sub(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_vec_self_class(vm, base);
    double x = vigil_vec_get_field(vm, base, 0U) - vigil_vec_get_field(vm, base + 1U, 0U);
    double y = vigil_vec_get_field(vm, base, 1U) - vigil_vec_get_field(vm, base + 1U, 1U);
    double z = vigil_vec_get_field(vm, base, 2U) - vigil_vec_get_field(vm, base + 1U, 2U);
    double w = vigil_vec_get_field(vm, base, 3U) - vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, x, y, z, w, ci, error);
}

static vigil_status_t vigil_vec4_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_vec_self_class(vm, base);
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    vigil_value_t sv = vigil_vm_stack_get(vm, base + 1U);
    double s = vigil_nanbox_decode_double(sv);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, x * s, y * s, z * s, w * s, ci, error);
}

static vigil_status_t vigil_vec4_vlerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_vec_self_class(vm, base);
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double w1 = vigil_vec_get_field(vm, base, 3U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    double w2 = vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_value_t tv = vigil_vm_stack_get(vm, base + 2U);
    double t = vigil_nanbox_decode_double(tv);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, z1 + (z2 - z1) * t, w1 + (w2 - w1) * t, ci,
                               error);
}

static const vigil_native_class_field_t vigil_vec4_fields[] = {
    VIGIL_PFIELD("x", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("y", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("z", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("w", 1U, VIGIL_TYPE_F64),
};

/* Vec4.zero() */
static vigil_status_t vigil_vec4_zero(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, 0.0, 0.0, 0.0, 0.0, ci, error);
}

/* Vec4.one() */
static vigil_status_t vigil_vec4_one(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_vec4_push_new(vm, 1.0, 1.0, 1.0, 1.0, ci, error);
}

static const vigil_native_class_method_t vigil_vec4_methods[] = {
    VIGIL_STATIC("zero", 4U, vigil_vec4_zero, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("one", 3U, vigil_vec4_one, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("length", 6U, vigil_vec4_length, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("lengthSqr", 9U, vigil_vec4_lengthsqr, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("dot", 3U, vigil_vec4_dot, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("distance", 8U, vigil_vec4_distance, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("normalize", 9U, vigil_vec4_vnormalize, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("negate", 6U, vigil_vec4_negate, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("add", 3U, vigil_vec4_add, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("sub", 3U, vigil_vec4_sub, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("scale", 5U, vigil_vec4_scale, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("lerp", 4U, vigil_vec4_vlerp, 2U, vigil_vec_obj_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
};

/* ── Quaternion class ─────────────────────────────────────────────── */

/*
 * Quaternion stores (x, y, z, w) where w is the scalar part.
 * Convention: q = w + xi + yj + zk.
 * Identity quaternion: (0, 0, 0, 1).
 */

/* Reuse vigil_vec4_push_new for quaternion — same 4-field layout. */
#define vigil_quat_push_new vigil_vec4_push_new

static vigil_status_t vigil_quat_length(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, sqrt(x * x + y * y + z * z + w * w), error);
}

static vigil_status_t vigil_quat_vnormalize(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    double len = sqrt(x * x + y * y + z * z + w * w);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0)
        return vigil_quat_push_new(vm, 0, 0, 0, 1, ci, error);
    return vigil_quat_push_new(vm, x / len, y / len, z / len, w / len, ci, error);
}

static vigil_status_t vigil_quat_conjugate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_quat_push_new(vm, -x, -y, -z, w, ci, error);
}

static vigil_status_t vigil_quat_inverse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    double lsq = x * x + y * y + z * z + w * w;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (lsq == 0.0)
        return vigil_quat_push_new(vm, 0, 0, 0, 1, ci, error);
    return vigil_quat_push_new(vm, -x / lsq, -y / lsq, -z / lsq, w / lsq, ci, error);
}

/* Hamilton product: q1 * q2 */
static vigil_status_t vigil_quat_multiply(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double w1 = vigil_vec_get_field(vm, base, 3U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    double w2 = vigil_vec_get_field(vm, base + 1U, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_quat_push_new(vm, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                               w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, ci, error);
}

static vigil_status_t vigil_quat_dot(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double r = vigil_vec_get_field(vm, base, 0U) * vigil_vec_get_field(vm, base + 1U, 0U) +
               vigil_vec_get_field(vm, base, 1U) * vigil_vec_get_field(vm, base + 1U, 1U) +
               vigil_vec_get_field(vm, base, 2U) * vigil_vec_get_field(vm, base + 1U, 2U) +
               vigil_vec_get_field(vm, base, 3U) * vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, r, error);
}

/* Spherical linear interpolation. */
static vigil_status_t vigil_quat_slerp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x1 = vigil_vec_get_field(vm, base, 0U);
    double y1 = vigil_vec_get_field(vm, base, 1U);
    double z1 = vigil_vec_get_field(vm, base, 2U);
    double w1 = vigil_vec_get_field(vm, base, 3U);
    double x2 = vigil_vec_get_field(vm, base + 1U, 0U);
    double y2 = vigil_vec_get_field(vm, base + 1U, 1U);
    double z2 = vigil_vec_get_field(vm, base + 1U, 2U);
    double w2 = vigil_vec_get_field(vm, base + 1U, 3U);
    vigil_value_t tv = vigil_vm_stack_get(vm, base + 2U);
    double t = vigil_nanbox_decode_double(tv);
    size_t ci = vigil_vec_self_class(vm, base);
    double cosHalf = x1 * x2 + y1 * y2 + z1 * z2 + w1 * w2;
    double s1, s2, halfAngle, sinHalf;
    vigil_vm_stack_pop_n(vm, arg_count);
    /* Take shortest path. */
    if (cosHalf < 0.0)
    {
        x2 = -x2;
        y2 = -y2;
        z2 = -z2;
        w2 = -w2;
        cosHalf = -cosHalf;
    }
    if (cosHalf > 0.9995)
    {
        /* Nearly identical — use linear interpolation. */
        s1 = 1.0 - t;
        s2 = t;
    }
    else
    {
        halfAngle = acos(cosHalf);
        sinHalf = sin(halfAngle);
        s1 = sin((1.0 - t) * halfAngle) / sinHalf;
        s2 = sin(t * halfAngle) / sinHalf;
    }
    return vigil_quat_push_new(vm, s1 * x1 + s2 * x2, s1 * y1 + s2 * y2, s1 * z1 + s2 * z2, s1 * w1 + s2 * w2, ci,
                               error);
}

/* Create quaternion from axis (Vec3-like, but passed as Quat fields) and angle. */
static vigil_status_t vigil_quat_from_axis_angle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Static: stack = [class_index, axis_vec3, angle_f64]. */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double ax = vigil_vec_get_field(vm, base + 1U, 0U);
    double ay = vigil_vec_get_field(vm, base + 1U, 1U);
    double az = vigil_vec_get_field(vm, base + 1U, 2U);
    vigil_value_t av = vigil_vm_stack_get(vm, base + 2U);
    double angle = vigil_nanbox_decode_double(av);
    double half = angle * 0.5;
    double s = sin(half);
    double len = sqrt(ax * ax + ay * ay + az * az);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (len != 0.0)
    {
        ax /= len;
        ay /= len;
        az /= len;
    }
    return vigil_quat_push_new(vm, ax * s, ay * s, az * s, cos(half), ci, error);
}

/* Convert to Euler angles (pitch, yaw, roll) in radians.
 * Returns a Vec3 — but we can't reference Vec3's class_index here,
 * so we return the angles as x, y, z of a new Quaternion (w=0).
 * The user reads .x (pitch), .y (yaw), .z (roll). */
static vigil_status_t vigil_quat_to_euler(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = vigil_vec_get_field(vm, base, 0U);
    double y = vigil_vec_get_field(vm, base, 1U);
    double z = vigil_vec_get_field(vm, base, 2U);
    double w = vigil_vec_get_field(vm, base, 3U);
    size_t ci = vigil_vec_self_class(vm, base);
    double sinp, pitch, yaw, roll;
    vigil_vm_stack_pop_n(vm, arg_count);
    /* pitch (x-axis rotation) */
    sinp = 2.0 * (w * x - y * z);
    if (sinp >= 1.0)
        pitch = 3.14159265358979323846 * 0.5;
    else if (sinp <= -1.0)
        pitch = -3.14159265358979323846 * 0.5;
    else
        pitch = asin(sinp);
    /* yaw (y-axis rotation) */
    yaw = atan2(2.0 * (w * y + x * z), 1.0 - 2.0 * (x * x + y * y));
    /* roll (z-axis rotation) */
    roll = atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (x * x + z * z));
    return vigil_quat_push_new(vm, pitch, yaw, roll, 0.0, ci, error);
}

static const vigil_native_class_field_t vigil_quat_fields[] = {
    VIGIL_PFIELD("x", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("y", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("z", 1U, VIGIL_TYPE_F64),
    VIGIL_PFIELD("w", 1U, VIGIL_TYPE_F64),
};

/* Quaternion.fromEuler(pitch, yaw, roll) — static factory */
static vigil_status_t vigil_quat_from_euler(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double pitch = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double yaw = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 2U));
    double roll = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 3U));
    double hp = pitch * 0.5, hy = yaw * 0.5, hr = roll * 0.5;
    double sp = sin(hp), cp = cos(hp);
    double sy = sin(hy), cy = cos(hy);
    double sr = sin(hr), cr = cos(hr);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_quat_push_new(vm, sp * cy * cr - cp * sy * sr, cp * sy * cr + sp * cy * sr,
                               cp * cy * sr - sp * sy * cr, cp * cy * cr + sp * sy * sr, ci, error);
}

static const int vigil_quat_f64x3_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};

/* Quaternion.toMat4() — returns Mat4 */
static vigil_status_t vigil_quat_to_mat4(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double qx = vigil_vec_get_field(vm, base, 0U);
    double qy = vigil_vec_get_field(vm, base, 1U);
    double qz = vigil_vec_get_field(vm, base, 2U);
    double qw = vigil_vec_get_field(vm, base, 3U);
    /* Need Mat4 class index — find it by name */
    vigil_value_t self_val = vigil_vm_stack_get(vm, base);
    vigil_object_t *inst = (vigil_object_t *)vigil_nanbox_decode_ptr(self_val);
    size_t quat_ci = vigil_instance_object_class_index(inst);
    /* Mat4 is registered after Quaternion, so it's quat_ci + 1.
     * This relies on registration order in vigil_math_classes[]. */
    size_t mat4_ci = quat_ci + 1U;
    double xx = qx * qx, yy = qy * qy, zz = qz * qz;
    double xy = qx * qy, xz = qx * qz, yz = qy * qz;
    double wx = qw * qx, wy = qw * qy, wz = qw * qz;
    double m[16] = {1 - 2 * (yy + zz),
                    2 * (xy + wz),
                    2 * (xz - wy),
                    0,
                    2 * (xy - wz),
                    1 - 2 * (xx + zz),
                    2 * (yz + wx),
                    0,
                    2 * (xz + wy),
                    2 * (yz - wx),
                    1 - 2 * (xx + yy),
                    0,
                    0,
                    0,
                    0,
                    1};
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, mat4_ci, error);
}

static const vigil_native_class_method_t vigil_quat_methods[] = {
    VIGIL_METHOD("length", 6U, vigil_quat_length, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("dot", 3U, vigil_quat_dot, 1U, vigil_vec_obj_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("normalize", 9U, vigil_quat_vnormalize, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("conjugate", 9U, vigil_quat_conjugate, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("inverse", 7U, vigil_quat_inverse, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("multiply", 8U, vigil_quat_multiply, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("slerp", 5U, vigil_quat_slerp, 2U, vigil_vec_obj_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("fromAxisAngle", 13U, vigil_quat_from_axis_angle, 2U, vigil_vec_obj_f64_params, VIGIL_TYPE_OBJECT, 1U,
                 NULL),
    VIGIL_STATIC("fromEuler", 9U, vigil_quat_from_euler, 3U, vigil_quat_f64x3_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("toEuler", 7U, vigil_quat_to_euler, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD_RET("toMat4", 6U, vigil_quat_to_mat4, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, "Mat4", 4U),
};

/* ── Mat4 class ──────────────────────────────────────────────────── */

/*
 * Mat4 stores a single field: data (array<f64>, 16 elements).
 * Column-major layout matching OpenGL/raylib convention:
 *   [m0  m4  m8  m12]
 *   [m1  m5  m9  m13]
 *   [m2  m6  m10 m14]
 *   [m3  m7  m11 m15]
 * Index = col * 4 + row.
 */

/* Helper: get the data array object from a Mat4 instance at stack slot.
 * The returned pointer is borrowed — the instance on the stack keeps it alive.
 */
static vigil_object_t *vigil_mat4_get_data(vigil_vm_t *vm, size_t slot)
{
    vigil_value_t self_val = vigil_vm_stack_get(vm, slot);
    vigil_object_t *inst = (vigil_object_t *)vigil_nanbox_decode_ptr(self_val);
    vigil_value_t field;
    vigil_object_t *arr;
    vigil_instance_object_get_field(inst, 0U, &field);
    arr = (vigil_object_t *)vigil_nanbox_decode_ptr(field);
    vigil_value_release(&field);
    return arr;
}

/* Helper: read f64 from mat4 data array at index. */
static double vigil_mat4_read(vigil_object_t *arr, size_t idx)
{
    vigil_value_t v;
    vigil_array_object_get(arr, idx, &v);
    return vigil_nanbox_decode_double(v);
}

/* Helper: push a new Mat4 with 16 doubles. */
static vigil_status_t vigil_mat4_push_new(vigil_vm_t *vm, const double m[16], size_t class_index, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t items[16];
    vigil_object_t *arr;
    vigil_value_t arr_val;
    vigil_value_t inst_fields[1];
    vigil_object_t *inst;
    vigil_value_t result;
    vigil_status_t s;
    size_t i;
    for (i = 0; i < 16; i++)
        items[i] = vigil_nanbox_encode_double(m[i]);
    s = vigil_array_object_new(rt, items, 16U, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&arr_val, &arr);
    inst_fields[0] = arr_val;
    s = vigil_instance_object_new(rt, class_index, inst_fields, 1U, &inst, error);
    vigil_value_release(&arr_val);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&result, &inst);
    s = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return s;
}

/* identity(): static factory, returns identity matrix */
static vigil_status_t vigil_mat4_identity(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* get(row, col) -> f64 */
static vigil_status_t vigil_mat4_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *arr = vigil_mat4_get_data(vm, base);
    vigil_value_t rv = vigil_vm_stack_get(vm, base + 1U);
    vigil_value_t cv = vigil_vm_stack_get(vm, base + 2U);
    int row = (int)vigil_nanbox_decode_i32(rv);
    int col = (int)vigil_nanbox_decode_i32(cv);
    double val = vigil_mat4_read(arr, (size_t)(col * 4 + row));
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, val, error);
}

/* set(row, col, val) -> Mat4 (returns new matrix) */
static vigil_status_t vigil_mat4_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *arr = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_value_t rv = vigil_vm_stack_get(vm, base + 1U);
    vigil_value_t cv = vigil_vm_stack_get(vm, base + 2U);
    vigil_value_t vv = vigil_vm_stack_get(vm, base + 3U);
    int row = (int)vigil_nanbox_decode_i32(rv);
    int col = (int)vigil_nanbox_decode_i32(cv);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(arr, i);
    m[col * 4 + row] = vigil_nanbox_decode_double(vv);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* multiply(Mat4) -> Mat4 */
static vigil_status_t vigil_mat4_multiply(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    vigil_object_t *b = vigil_mat4_get_data(vm, base + 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    double m[16];
    int c, r, k;
    for (c = 0; c < 4; c++)
    {
        for (r = 0; r < 4; r++)
        {
            double sum = 0.0;
            for (k = 0; k < 4; k++)
            {
                sum += vigil_mat4_read(a, k * 4 + r) * vigil_mat4_read(b, c * 4 + k);
            }
            m[c * 4 + r] = sum;
        }
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* transpose() -> Mat4 */
static vigil_status_t vigil_mat4_transpose(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double m[16];
    int c, r;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            m[c * 4 + r] = vigil_mat4_read(a, r * 4 + c);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* determinant() -> f64 */
static vigil_status_t vigil_mat4_determinant(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    double m[16];
    double det;
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    det = m[0] * (m[5] * (m[10] * m[15] - m[11] * m[14]) - m[9] * (m[6] * m[15] - m[7] * m[14]) +
                  m[13] * (m[6] * m[11] - m[7] * m[10])) -
          m[4] * (m[1] * (m[10] * m[15] - m[11] * m[14]) - m[9] * (m[2] * m[15] - m[3] * m[14]) +
                  m[13] * (m[2] * m[11] - m[3] * m[10])) +
          m[8] * (m[1] * (m[6] * m[15] - m[7] * m[14]) - m[5] * (m[2] * m[15] - m[3] * m[14]) +
                  m[13] * (m[2] * m[7] - m[3] * m[6])) -
          m[12] * (m[1] * (m[6] * m[11] - m[7] * m[10]) - m[5] * (m[2] * m[11] - m[3] * m[10]) +
                   m[9] * (m[2] * m[7] - m[3] * m[6]));
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, det, error);
}

/* add(Mat4) -> Mat4 */
static vigil_status_t vigil_mat4_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    vigil_object_t *b = vigil_mat4_get_data(vm, base + 1U);
    size_t ci = vigil_vec_self_class(vm, base);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i) + vigil_mat4_read(b, i);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* scale(f64) -> Mat4 (scalar multiply) */
static vigil_status_t vigil_mat4_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    vigil_value_t sv = vigil_vm_stack_get(vm, base + 1U);
    double s = vigil_nanbox_decode_double(sv);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i) * s;
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* trace() -> f64 */
static vigil_status_t vigil_mat4_trace(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    double tr = vigil_mat4_read(a, 0) + vigil_mat4_read(a, 5) + vigil_mat4_read(a, 10) + vigil_mat4_read(a, 15);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_math_push_f64(vm, tr, error);
}

/* invert() -> Mat4 (Gauss-Jordan elimination) */
static vigil_status_t vigil_mat4_invert(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double aug[4][8];
    int row, col, piv, best;
    double bv, d, f, t;
    for (row = 0; row < 4; row++)
        for (col = 0; col < 4; col++)
        {
            aug[row][col] = vigil_mat4_read(a, (size_t)(col * 4 + row));
            aug[row][col + 4] = (row == col) ? 1.0 : 0.0;
        }
    for (piv = 0; piv < 4; piv++)
    {
        best = piv;
        bv = fabs(aug[piv][piv]);
        for (row = piv + 1; row < 4; row++)
            if (fabs(aug[row][piv]) > bv)
            {
                best = row;
                bv = fabs(aug[row][piv]);
            }
        if (best != piv)
            for (col = 0; col < 8; col++)
            {
                t = aug[piv][col];
                aug[piv][col] = aug[best][col];
                aug[best][col] = t;
            }
        if (aug[piv][piv] == 0.0)
            break;
        d = aug[piv][piv];
        for (col = 0; col < 8; col++)
            aug[piv][col] /= d;
        for (row = 0; row < 4; row++)
            if (row != piv)
            {
                f = aug[row][piv];
                for (col = 0; col < 8; col++)
                    aug[row][col] -= f * aug[piv][col];
            }
    }
    {
        double m[16];
        for (row = 0; row < 4; row++)
            for (col = 0; col < 4; col++)
                m[col * 4 + row] = aug[row][col + 4];
        vigil_vm_stack_pop_n(vm, arg_count);
        return vigil_mat4_push_new(vm, m, ci, error);
    }
}

/* translate(Vec3) -> Mat4: self * translation matrix */
static vigil_status_t vigil_mat4_translate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double tx = vigil_vec_get_field(vm, base + 1U, 0U);
    double ty = vigil_vec_get_field(vm, base + 1U, 1U);
    double tz = vigil_vec_get_field(vm, base + 1U, 2U);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    /* m = m * T where T is identity with [12,13,14] = tx,ty,tz */
    m[12] += m[0] * tx + m[4] * ty + m[8] * tz;
    m[13] += m[1] * tx + m[5] * ty + m[9] * tz;
    m[14] += m[2] * tx + m[6] * ty + m[10] * tz;
    m[15] += m[3] * tx + m[7] * ty + m[11] * tz;
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* scaleV(Vec3) -> Mat4: self * scale matrix */
static vigil_status_t vigil_mat4_scale_vec(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double sx = vigil_vec_get_field(vm, base + 1U, 0U);
    double sy = vigil_vec_get_field(vm, base + 1U, 1U);
    double sz = vigil_vec_get_field(vm, base + 1U, 2U);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    m[0] *= sx;
    m[1] *= sx;
    m[2] *= sx;
    m[3] *= sx;
    m[4] *= sy;
    m[5] *= sy;
    m[6] *= sy;
    m[7] *= sy;
    m[8] *= sz;
    m[9] *= sz;
    m[10] *= sz;
    m[11] *= sz;
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* rotateX(angle) -> Mat4 */
static vigil_status_t vigil_mat4_rotate_x(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double angle = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double c = cos(angle), s = sin(angle);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    /* Multiply by Rx on the right */
    double t;
    for (i = 0; i < 4; i++)
    {
        t = m[4 + i];
        m[4 + i] = t * c + m[8 + i] * s;
        m[8 + i] = -t * s + m[8 + i] * c;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* rotateY(angle) -> Mat4 */
static vigil_status_t vigil_mat4_rotate_y(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double angle = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double c = cos(angle), s = sin(angle);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    double t;
    for (i = 0; i < 4; i++)
    {
        t = m[i];
        m[i] = t * c - m[8 + i] * s;
        m[8 + i] = t * s + m[8 + i] * c;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* rotateZ(angle) -> Mat4 */
static vigil_status_t vigil_mat4_rotate_z(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *a = vigil_mat4_get_data(vm, base);
    size_t ci = vigil_vec_self_class(vm, base);
    double angle = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double c = cos(angle), s = sin(angle);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++)
        m[i] = vigil_mat4_read(a, i);
    double t;
    for (i = 0; i < 4; i++)
    {
        t = m[i];
        m[i] = t * c + m[4 + i] * s;
        m[4 + i] = -t * s + m[4 + i] * c;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* Mat4.lookAt(eye, target, up) -> Mat4 (static) */
static vigil_status_t vigil_mat4_look_at(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double ex = vigil_vec_get_field(vm, base + 1U, 0U);
    double ey = vigil_vec_get_field(vm, base + 1U, 1U);
    double ez = vigil_vec_get_field(vm, base + 1U, 2U);
    double tx = vigil_vec_get_field(vm, base + 2U, 0U);
    double ty = vigil_vec_get_field(vm, base + 2U, 1U);
    double tz = vigil_vec_get_field(vm, base + 2U, 2U);
    double ux = vigil_vec_get_field(vm, base + 3U, 0U);
    double uy = vigil_vec_get_field(vm, base + 3U, 1U);
    double uz = vigil_vec_get_field(vm, base + 3U, 2U);
    /* f = normalize(target - eye) */
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    double fl = sqrt(fx * fx + fy * fy + fz * fz);
    if (fl != 0.0)
    {
        fx /= fl;
        fy /= fl;
        fz /= fl;
    }
    /* s = normalize(f x up) */
    double sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    double sl = sqrt(sx * sx + sy * sy + sz * sz);
    if (sl != 0.0)
    {
        sx /= sl;
        sy /= sl;
        sz /= sl;
    }
    /* u = s x f */
    double uux = sy * fz - sz * fy, uuy = sz * fx - sx * fz, uuz = sx * fy - sy * fx;
    double m[16] = {sx,
                    uux,
                    -fx,
                    0,
                    sy,
                    uuy,
                    -fy,
                    0,
                    sz,
                    uuz,
                    -fz,
                    0,
                    -(sx * ex + sy * ey + sz * ez),
                    -(uux * ex + uuy * ey + uuz * ez),
                    fx * ex + fy * ey + fz * ez,
                    1};
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

static const int vigil_mat4_obj3_params[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT};
static const int vigil_mat4_f64x4_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int vigil_mat4_f64x6_params[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                              VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};

/* Mat4.perspective(fovY, aspect, near, far) -> Mat4 (static) */
static vigil_status_t vigil_mat4_perspective(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double fovy = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double aspect = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 2U));
    double near = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 3U));
    double far = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 4U));
    double top = near * tan(fovy * 0.5);
    double right = top * aspect;
    double m[16] = {0};
    m[0] = near / right;
    m[5] = near / top;
    m[10] = -(far + near) / (far - near);
    m[11] = -1.0;
    m[14] = -(2.0 * far * near) / (far - near);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* Mat4.ortho(left, right, bottom, top, near, far) -> Mat4 (static) */
static vigil_status_t vigil_mat4_ortho(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double l = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double r = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 2U));
    double b = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 3U));
    double t = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 4U));
    double n = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 5U));
    double f = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 6U));
    double m[16] = {0};
    m[0] = 2.0 / (r - l);
    m[5] = 2.0 / (t - b);
    m[10] = -2.0 / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0;
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

/* Mat4.frustum(left, right, bottom, top, near, far) -> Mat4 (static) */
static vigil_status_t vigil_mat4_frustum(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = vigil_static_class_index(vm, base);
    double l = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 1U));
    double r = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 2U));
    double b = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 3U));
    double t = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 4U));
    double n = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 5U));
    double f = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 6U));
    double m[16] = {0};
    m[0] = 2.0 * n / (r - l);
    m[5] = 2.0 * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0;
    m[14] = -(2.0 * f * n) / (f - n);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_mat4_push_new(vm, m, ci, error);
}

static const int vigil_mat4_i32i32_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int vigil_mat4_i32i32f64_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_F64};

static const vigil_native_class_field_t vigil_mat4_fields[] = {
    {"data", 4U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_F64, NULL, NULL},
};

static const vigil_native_class_method_t vigil_mat4_methods[] = {
    VIGIL_STATIC("identity", 8U, vigil_mat4_identity, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("lookAt", 6U, vigil_mat4_look_at, 3U, vigil_mat4_obj3_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("perspective", 11U, vigil_mat4_perspective, 4U, vigil_mat4_f64x4_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("ortho", 5U, vigil_mat4_ortho, 6U, vigil_mat4_f64x6_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_STATIC("frustum", 7U, vigil_mat4_frustum, 6U, vigil_mat4_f64x6_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("get", 3U, vigil_mat4_get, 2U, vigil_mat4_i32i32_params, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("set", 3U, vigil_mat4_set, 3U, vigil_mat4_i32i32f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("multiply", 8U, vigil_mat4_multiply, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("transpose", 9U, vigil_mat4_transpose, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("determinant", 11U, vigil_mat4_determinant, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("trace", 5U, vigil_mat4_trace, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    VIGIL_METHOD("invert", 6U, vigil_mat4_invert, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("add", 3U, vigil_mat4_add, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("scale", 5U, vigil_mat4_scale, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("scaleV", 6U, vigil_mat4_scale_vec, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("translate", 9U, vigil_mat4_translate, 1U, vigil_vec_obj_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("rotateX", 7U, vigil_mat4_rotate_x, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("rotateY", 7U, vigil_mat4_rotate_y, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
    VIGIL_METHOD("rotateZ", 7U, vigil_mat4_rotate_z, 1U, vigil_vec_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL),
};

static const vigil_native_class_t vigil_math_classes[] = {
    {"Vec2", 4U, vigil_vec2_fields, 2U, vigil_vec2_methods, 15U, NULL, NULL},
    {"Vec3", 4U, vigil_vec3_fields, 3U, vigil_vec3_methods, 18U, NULL, NULL},
    {"Vec4", 4U, vigil_vec4_fields, 4U, vigil_vec4_methods, 12U, NULL, NULL},
    {"Quaternion", 10U, vigil_quat_fields, 4U, vigil_quat_methods, 11U, NULL, NULL},
    {"Mat4", 4U, vigil_mat4_fields, 1U, vigil_mat4_methods, 19U, NULL, NULL},
};

#define VIGIL_MATH_CLASS_COUNT (sizeof(vigil_math_classes) / sizeof(vigil_math_classes[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_math = {
    "math", 4U, vigil_math_functions, VIGIL_MATH_FUNCTION_COUNT, vigil_math_classes, VIGIL_MATH_CLASS_COUNT, NULL};
