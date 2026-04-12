#include <string.h>

#include "internal/vigil_binding.h"
#include "internal/vigil_internal.h"

static int vigil_binding_names_equal(const char *left, size_t left_length, const char *right, size_t right_length)
{
    return left != NULL && right != NULL && left_length == right_length && memcmp(left, right, left_length) == 0;
}

vigil_binding_type_t vigil_binding_type_invalid(void)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_INVALID;
    type.object_kind = VIGIL_BINDING_OBJECT_NONE;
    type.object_index = VIGIL_BINDING_INVALID_CLASS_INDEX;
    return type;
}

vigil_binding_type_t vigil_binding_type_primitive(vigil_type_kind_t kind)
{
    vigil_binding_type_t type;

    type.kind = kind;
    type.object_kind = VIGIL_BINDING_OBJECT_NONE;
    type.object_index = VIGIL_BINDING_INVALID_CLASS_INDEX;
    return type;
}

vigil_binding_type_t vigil_binding_type_class(size_t class_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_OBJECT;
    type.object_kind = VIGIL_BINDING_OBJECT_CLASS;
    type.object_index = class_index;
    return type;
}

vigil_binding_type_t vigil_binding_type_interface(size_t interface_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_OBJECT;
    type.object_kind = VIGIL_BINDING_OBJECT_INTERFACE;
    type.object_index = interface_index;
    return type;
}

vigil_binding_type_t vigil_binding_type_enum(size_t enum_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_I32;
    type.object_kind = VIGIL_BINDING_OBJECT_ENUM;
    type.object_index = enum_index;
    return type;
}

vigil_binding_type_t vigil_binding_type_array(size_t array_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_OBJECT;
    type.object_kind = VIGIL_BINDING_OBJECT_ARRAY;
    type.object_index = array_index;
    return type;
}

vigil_binding_type_t vigil_binding_type_map(size_t map_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_OBJECT;
    type.object_kind = VIGIL_BINDING_OBJECT_MAP;
    type.object_index = map_index;
    return type;
}

vigil_binding_type_t vigil_binding_type_function(size_t function_type_index)
{
    vigil_binding_type_t type;

    type.kind = VIGIL_TYPE_OBJECT;
    type.object_kind = VIGIL_BINDING_OBJECT_FUNCTION;
    type.object_index = function_type_index;
    return type;
}

int vigil_binding_type_is_valid(vigil_binding_type_t type)
{
    if (type.object_kind == VIGIL_BINDING_OBJECT_ENUM)
    {
        return type.kind == VIGIL_TYPE_I32 && type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
    }

    if (type.kind == VIGIL_TYPE_OBJECT)
    {
        return type.object_kind != VIGIL_BINDING_OBJECT_NONE && type.object_kind != VIGIL_BINDING_OBJECT_ENUM &&
               type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
    }

    return type.object_kind == VIGIL_BINDING_OBJECT_NONE && vigil_type_kind_is_valid(type.kind);
}

int vigil_binding_type_equal(vigil_binding_type_t left, vigil_binding_type_t right)
{
    if (!vigil_binding_type_is_valid(left) || !vigil_binding_type_is_valid(right))
    {
        return 0;
    }

    return left.kind == right.kind && left.object_kind == right.object_kind && left.object_index == right.object_index;
}

static vigil_status_t vigil_binding_grow_function_params(vigil_runtime_t *runtime, vigil_binding_function_t *function,
                                                         size_t minimum_capacity, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= function->param_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = function->param_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*function->params))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "binding parameter table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = function->params;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(runtime, next_capacity * sizeof(*function->params), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(runtime, &memory, next_capacity * sizeof(*function->params), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_binding_function_param_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*function->params));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    function->params = (vigil_binding_function_param_t *)memory;
    function->param_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_binding_grow_function_returns(vigil_runtime_t *runtime, vigil_binding_function_t *function,
                                                          size_t minimum_capacity, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= function->return_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = function->return_capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*function->return_types))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "binding return type table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = function->return_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(runtime, next_capacity * sizeof(*function->return_types), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(runtime, &memory, next_capacity * sizeof(*function->return_types), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_binding_type_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*function->return_types));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    function->return_types = (vigil_binding_type_t *)memory;
    function->return_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_binding_grow_functions(vigil_binding_function_table_t *table, size_t minimum_capacity,
                                                   vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= table->capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = table->capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*table->functions))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "binding function table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = table->functions;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(table->runtime, next_capacity * sizeof(*table->functions), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(table->runtime, &memory, next_capacity * sizeof(*table->functions), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_binding_function_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*table->functions));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    table->functions = (vigil_binding_function_t *)memory;
    table->capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_binding_grow_locals(vigil_binding_scope_stack_t *stack, size_t minimum_capacity,
                                                vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= stack->local_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = stack->local_capacity;
    next_capacity = old_capacity == 0U ? 16U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*stack->locals))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "binding local table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = stack->locals;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(stack->runtime, next_capacity * sizeof(*stack->locals), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(stack->runtime, &memory, next_capacity * sizeof(*stack->locals), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_binding_local_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*stack->locals));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    stack->locals = (vigil_binding_local_t *)memory;
    stack->local_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_binding_function_init(vigil_binding_function_t *function)
{
    if (function == NULL)
    {
        return;
    }

    memset(function, 0, sizeof(*function));
    function->return_type = vigil_binding_type_invalid();
    function->owner_class_index = VIGIL_BINDING_INVALID_CLASS_INDEX;
    function->is_local = 0;
}

void vigil_binding_function_free(vigil_runtime_t *runtime, vigil_binding_function_t *function)
{
    void *memory;

    if (runtime == NULL || function == NULL)
    {
        return;
    }

    if (function->object != NULL)
    {
        vigil_object_release(&function->object);
    }

    memory = function->params;
    vigil_runtime_free(runtime, &memory);
    memory = function->captures;
    vigil_runtime_free(runtime, &memory);
    memory = function->return_types;
    vigil_runtime_free(runtime, &memory);
    memset(function, 0, sizeof(*function));
}

vigil_status_t vigil_binding_function_add_param(vigil_runtime_t *runtime, vigil_binding_function_t *function,
                                                const vigil_binding_function_param_spec_t *spec, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_binding_function_param_t *param;
    size_t index;

    if (runtime == NULL || function == NULL || spec == NULL || spec->name == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding function parameter arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < function->param_count; index += 1U)
    {
        if (vigil_binding_names_equal(function->params[index].name, function->params[index].length, spec->name,
                                      spec->name_length))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "binding function parameter is already declared");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }

    status = vigil_binding_grow_function_params(runtime, function, function->param_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    param = &function->params[function->param_count];
    param->name = spec->name;
    param->length = spec->name_length;
    param->type = spec->type;
    param->span = spec->span;
    function->param_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_binding_function_add_return_type(vigil_runtime_t *runtime, vigil_binding_function_t *function,
                                                      vigil_binding_type_t type, vigil_error_t *error)
{
    vigil_status_t status;

    if (runtime == NULL || function == NULL || !vigil_binding_type_is_valid(type))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding function return type arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_binding_grow_function_returns(runtime, function, function->return_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    function->return_types[function->return_count] = type;
    function->return_count += 1U;
    function->return_type = function->return_types[0];
    return VIGIL_STATUS_OK;
}

void vigil_binding_function_table_init(vigil_binding_function_table_t *table, vigil_runtime_t *runtime)
{
    if (table == NULL)
    {
        return;
    }

    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
}

void vigil_binding_function_table_free(vigil_binding_function_table_t *table)
{
    size_t index;
    void *memory;

    if (table == NULL || table->runtime == NULL)
    {
        return;
    }

    for (index = 0U; index < table->count; index += 1U)
    {
        vigil_binding_function_free(table->runtime, &table->functions[index]);
    }

    memory = table->functions;
    vigil_runtime_free(table->runtime, &memory);
    memset(table, 0, sizeof(*table));
}

int vigil_binding_function_table_find(const vigil_binding_function_table_t *table, const char *name, size_t name_length,
                                      size_t *out_index, const vigil_binding_function_t **out_function)
{
    size_t index;

    if (table == NULL || name == NULL)
    {
        return 0;
    }

    for (index = 0U; index < table->count; index += 1U)
    {
        if (vigil_binding_names_equal(table->functions[index].name, table->functions[index].name_length, name,
                                      name_length))
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            if (out_function != NULL)
            {
                *out_function = &table->functions[index];
            }
            return 1;
        }
    }

    return 0;
}

const vigil_binding_function_t *vigil_binding_function_table_get(const vigil_binding_function_table_t *table,
                                                                 size_t index)
{
    if (table == NULL || index >= table->count)
    {
        return NULL;
    }

    return &table->functions[index];
}

vigil_binding_function_t *vigil_binding_function_table_get_mutable(vigil_binding_function_table_t *table, size_t index)
{
    if (table == NULL || index >= table->count)
    {
        return NULL;
    }

    return &table->functions[index];
}

vigil_status_t vigil_binding_function_table_append(vigil_binding_function_table_t *table,
                                                   vigil_binding_function_t *function, size_t *out_index,
                                                   vigil_error_t *error)
{
    vigil_status_t status;
    size_t index;

    if (table == NULL || function == NULL || function->name == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding function table append arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_binding_function_table_find(table, function->name, function->name_length, &index, NULL))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "binding function is already declared");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_binding_grow_functions(table, table->count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    table->functions[table->count] = *function;
    if (out_index != NULL)
    {
        *out_index = table->count;
    }
    table->count += 1U;
    memset(function, 0, sizeof(*function));
    return VIGIL_STATUS_OK;
}

void vigil_binding_scope_stack_init(vigil_binding_scope_stack_t *stack, vigil_runtime_t *runtime)
{
    if (stack == NULL)
    {
        return;
    }

    memset(stack, 0, sizeof(*stack));
    stack->runtime = runtime;
}

void vigil_binding_scope_stack_free(vigil_binding_scope_stack_t *stack)
{
    void *memory;

    if (stack == NULL || stack->runtime == NULL)
    {
        return;
    }

    memory = stack->locals;
    vigil_runtime_free(stack->runtime, &memory);
    memset(stack, 0, sizeof(*stack));
}

void vigil_binding_scope_stack_begin_scope(vigil_binding_scope_stack_t *stack)
{
    if (stack == NULL)
    {
        return;
    }

    stack->scope_depth += 1U;
}

void vigil_binding_scope_stack_end_scope(vigil_binding_scope_stack_t *stack, size_t *out_popped_count)
{
    size_t popped_count;

    if (out_popped_count != NULL)
    {
        *out_popped_count = 0U;
    }

    if (stack == NULL || stack->scope_depth == 0U)
    {
        return;
    }

    popped_count = 0U;
    stack->scope_depth -= 1U;
    while (stack->local_count > 0U && stack->locals[stack->local_count - 1U].depth > stack->scope_depth)
    {
        stack->local_count -= 1U;
        popped_count += 1U;
    }

    if (out_popped_count != NULL)
    {
        *out_popped_count = popped_count;
    }
}

size_t vigil_binding_scope_stack_depth(const vigil_binding_scope_stack_t *stack)
{
    if (stack == NULL)
    {
        return 0U;
    }

    return stack->scope_depth;
}

size_t vigil_binding_scope_stack_count(const vigil_binding_scope_stack_t *stack)
{
    if (stack == NULL)
    {
        return 0U;
    }

    return stack->local_count;
}

size_t vigil_binding_scope_stack_count_above_depth(const vigil_binding_scope_stack_t *stack, size_t target_depth)
{
    size_t count;
    size_t index;

    if (stack == NULL)
    {
        return 0U;
    }

    count = 0U;
    for (index = stack->local_count; index > 0U; index -= 1U)
    {
        if (stack->locals[index - 1U].depth <= target_depth)
        {
            break;
        }
        count += 1U;
    }

    return count;
}

vigil_status_t vigil_binding_scope_stack_declare_local(vigil_binding_scope_stack_t *stack,
                                                       const vigil_binding_local_spec_t *spec, size_t *out_index,
                                                       vigil_error_t *error)
{
    vigil_status_t status;
    size_t index;
    vigil_binding_local_t *local;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }

    if (stack == NULL || spec == NULL || spec->name == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding local declaration arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = stack->local_count; index > 0U; index -= 1U)
    {
        local = &stack->locals[index - 1U];
        if (local->depth < stack->scope_depth)
        {
            break;
        }

        if (vigil_binding_names_equal(local->name, local->length, spec->name, spec->name_length))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "binding local variable is already declared in this scope");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }

    status = vigil_binding_grow_locals(stack, stack->local_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    local = &stack->locals[stack->local_count];
    local->name = spec->name;
    local->length = spec->name_length;
    local->depth = stack->scope_depth;
    local->type = spec->type;
    local->is_const = spec->is_const != 0;
    local->is_param = spec->is_param != 0;
    if (out_index != NULL)
    {
        *out_index = stack->local_count;
    }
    stack->local_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_binding_scope_stack_declare_hidden_local(vigil_binding_scope_stack_t *stack,
                                                              vigil_binding_type_t type, int is_const,
                                                              size_t *out_index, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_binding_local_t *local;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }

    if (stack == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding hidden local declaration arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_binding_grow_locals(stack, stack->local_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    local = &stack->locals[stack->local_count];
    local->name = NULL;
    local->length = 0U;
    local->depth = stack->scope_depth;
    local->type = type;
    local->is_const = is_const != 0;
    if (out_index != NULL)
    {
        *out_index = stack->local_count;
    }
    stack->local_count += 1U;
    return VIGIL_STATUS_OK;
}

int vigil_binding_scope_stack_find_local(const vigil_binding_scope_stack_t *stack, const char *name, size_t name_length,
                                         size_t *out_index, vigil_binding_type_t *out_type)
{
    size_t index;

    if (stack == NULL || name == NULL)
    {
        return 0;
    }

    for (index = stack->local_count; index > 0U; index -= 1U)
    {
        if (vigil_binding_names_equal(stack->locals[index - 1U].name, stack->locals[index - 1U].length, name,
                                      name_length))
        {
            if (out_index != NULL)
            {
                *out_index = index - 1U;
            }
            if (out_type != NULL)
            {
                *out_type = stack->locals[index - 1U].type;
            }
            return 1;
        }
    }

    return 0;
}

const vigil_binding_local_t *vigil_binding_scope_stack_local_at(const vigil_binding_scope_stack_t *stack, size_t index)
{
    if (stack == NULL || index >= stack->local_count)
    {
        return NULL;
    }

    return &stack->locals[index];
}
