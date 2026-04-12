#include <stdlib.h>
#include <string.h>

#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"

void vigil_statement_result_clear(vigil_statement_result_t *result)
{
    if (result == NULL)
    {
        return;
    }

    result->guaranteed_return = 0;
}

void vigil_statement_result_set_guaranteed_return(vigil_statement_result_t *result, int guaranteed_return)
{
    if (result == NULL)
    {
        return;
    }

    result->guaranteed_return = guaranteed_return;
}

void vigil_program_set_module_context(vigil_program_state_t *program, const vigil_source_file_t *source,
                                      const vigil_token_list_t *tokens)
{
    if (program == NULL)
    {
        return;
    }

    program->source = source;
    program->tokens = tokens;
}

vigil_status_t vigil_program_grow_modules(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->module_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->module_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->modules))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "module table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->modules;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->modules), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->modules),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_program_module_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->modules));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->modules = (vigil_program_module_t *)memory;
    program->module_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_program_import_default_alias(const char *path, size_t path_length, const char **out_alias,
                                        size_t *out_alias_length)
{
    size_t start;
    size_t end;

    if (out_alias != NULL)
    {
        *out_alias = path;
    }
    if (out_alias_length != NULL)
    {
        *out_alias_length = path_length;
    }
    if (path == NULL)
    {
        return;
    }

    start = path_length;
    while (start > 0U)
    {
        char current = path[start - 1U];

        if (current == '/' || current == '\\')
        {
            break;
        }
        start -= 1U;
    }

    end = path_length;
    if (end >= 6U && memcmp(path + end - 6U, ".vigil", 6U) == 0)
    {
        end -= 6U;
    }

    if (out_alias != NULL)
    {
        *out_alias = path + start;
    }
    if (out_alias_length != NULL)
    {
        *out_alias_length = end - start;
    }
}

void vigil_class_decl_free(vigil_program_state_t *program, vigil_class_decl_t *decl)
{
    void *memory;
    size_t i;

    if (program == NULL || decl == NULL)
    {
        return;
    }

    memory = decl->fields;
    vigil_runtime_free(program->registry->runtime, &memory);
    memory = decl->methods;
    vigil_runtime_free(program->registry->runtime, &memory);
    memory = decl->implemented_interfaces;
    vigil_runtime_free(program->registry->runtime, &memory);
    for (i = 0U; i < decl->interface_impl_count; i += 1U)
    {
        memory = decl->interface_impls[i].function_indices;
        vigil_runtime_free(program->registry->runtime, &memory);
    }
    memory = decl->interface_impls;
    vigil_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

vigil_status_t vigil_program_grow_classes(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->class_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->class_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->classes))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "class table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->classes;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->classes), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->classes),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_class_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->classes));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->classes = (vigil_class_decl_t *)memory;
    program->class_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_interface_decl_free(vigil_program_state_t *program, vigil_interface_decl_t *decl)
{
    void *memory;
    size_t i;

    if (program == NULL || decl == NULL)
    {
        return;
    }

    for (i = 0U; i < decl->method_count; i += 1U)
    {
        memory = decl->methods[i].param_types;
        vigil_runtime_free(program->registry->runtime, &memory);
        memory = decl->methods[i].return_types;
        vigil_runtime_free(program->registry->runtime, &memory);
    }
    memory = decl->methods;
    vigil_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

vigil_status_t vigil_program_grow_interfaces(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->interface_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->interface_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->interfaces))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "interface table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->interfaces;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->interfaces), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*program->interfaces), program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_interface_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->interfaces));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->interfaces = (vigil_interface_decl_t *)memory;
    program->interface_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_enum_decl_free(vigil_program_state_t *program, vigil_enum_decl_t *decl)
{
    void *memory;

    if (program == NULL || decl == NULL)
    {
        return;
    }

    memory = decl->members;
    vigil_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

vigil_status_t vigil_program_grow_enums(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->enum_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->enum_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->enums))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "enum table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->enums;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->enums), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->enums),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_enum_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->enums));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->enums = (vigil_enum_decl_t *)memory;
    program->enum_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_grow_array_types(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->array_type_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->array_type_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->array_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "array type table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->array_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->array_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*program->array_types), program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_array_type_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->array_types));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->array_types = (vigil_array_type_decl_t *)memory;
    program->array_type_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

int vigil_program_find_array_type(const vigil_program_state_t *program, vigil_parser_type_t element_type,
                                  size_t *out_index)
{
    size_t index;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }
    if (program == NULL)
    {
        return 0;
    }

    for (index = 0U; index < program->array_type_count; ++index)
    {
        if (vigil_parser_type_equal(program->array_types[index].element_type, element_type))
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

vigil_status_t vigil_program_intern_array_type(vigil_program_state_t *program, vigil_parser_type_t element_type,
                                               vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    size_t index;

    if (out_type != NULL)
    {
        *out_type = vigil_binding_type_invalid();
    }
    if (program == NULL || !vigil_binding_type_is_valid(element_type) || out_type == NULL)
    {
        vigil_error_set_literal(program == NULL ? NULL : program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "array type arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_program_find_array_type(program, element_type, &index))
    {
        *out_type = vigil_binding_type_array(index);
        return VIGIL_STATUS_OK;
    }

    status = vigil_program_grow_array_types(program, program->array_type_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    index = program->array_type_count;
    program->array_types[index].element_type = element_type;
    program->array_type_count += 1U;
    *out_type = vigil_binding_type_array(index);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_grow_map_types(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->map_type_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->map_type_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->map_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "map type table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->map_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->map_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->map_types),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_map_type_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->map_types));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->map_types = (vigil_map_type_decl_t *)memory;
    program->map_type_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

int vigil_program_find_map_type(const vigil_program_state_t *program, vigil_parser_type_t key_type,
                                vigil_parser_type_t value_type, size_t *out_index)
{
    size_t index;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }
    if (program == NULL)
    {
        return 0;
    }

    for (index = 0U; index < program->map_type_count; ++index)
    {
        if (vigil_parser_type_equal(program->map_types[index].key_type, key_type) &&
            vigil_parser_type_equal(program->map_types[index].value_type, value_type))
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

vigil_status_t vigil_program_intern_map_type(vigil_program_state_t *program, vigil_parser_type_t key_type,
                                             vigil_parser_type_t value_type, vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    size_t index;

    if (out_type != NULL)
    {
        *out_type = vigil_binding_type_invalid();
    }
    if (program == NULL || !vigil_binding_type_is_valid(key_type) || !vigil_binding_type_is_valid(value_type) ||
        out_type == NULL)
    {
        vigil_error_set_literal(program == NULL ? NULL : program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "map type arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_program_find_map_type(program, key_type, value_type, &index))
    {
        *out_type = vigil_binding_type_map(index);
        return VIGIL_STATUS_OK;
    }

    status = vigil_program_grow_map_types(program, program->map_type_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    index = program->map_type_count;
    program->map_types[index].key_type = key_type;
    program->map_types[index].value_type = value_type;
    program->map_type_count += 1U;
    *out_type = vigil_binding_type_map(index);
    return VIGIL_STATUS_OK;
}

void vigil_function_type_decl_free(vigil_program_state_t *program, vigil_function_type_decl_t *decl)
{
    void *memory;

    if (program == NULL || decl == NULL)
    {
        return;
    }

    memory = decl->return_types;
    vigil_runtime_free(program->registry->runtime, &memory);
    memory = decl->param_types;
    vigil_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

vigil_status_t vigil_function_type_decl_grow_params(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                    size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->param_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->param_capacity;
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
    if (next_capacity > SIZE_MAX / sizeof(*decl->param_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "function type parameter table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->param_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->param_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->param_types),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_parser_type_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->param_types));
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->param_types = (vigil_parser_type_t *)memory;
    decl->param_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_function_type_decl_add_param(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                  vigil_parser_type_t type)
{
    vigil_status_t status;

    status = vigil_function_type_decl_grow_params(program, decl, decl->param_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->param_types[decl->param_count] = type;
    decl->param_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_function_type_decl_grow_returns(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                     size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->return_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->return_capacity;
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
    if (next_capacity > SIZE_MAX / sizeof(*decl->return_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "function type return table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->return_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->return_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->return_types),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_parser_type_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->return_types));
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->return_types = (vigil_parser_type_t *)memory;
    decl->return_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_function_type_decl_add_return(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                   vigil_parser_type_t type)
{
    vigil_status_t status;

    status = vigil_function_type_decl_grow_returns(program, decl, decl->return_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->return_types[decl->return_count] = type;
    decl->return_count += 1U;
    decl->return_type = decl->return_types[0];
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_grow_function_types(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->function_type_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->function_type_capacity;
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
    if (next_capacity > SIZE_MAX / sizeof(*program->function_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "function type table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->function_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->function_types),
                                     &memory, program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*program->function_types), program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_function_type_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->function_types));
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->function_types = (vigil_function_type_decl_t *)memory;
    program->function_type_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

int vigil_function_type_decl_matches(const vigil_function_type_decl_t *left, const vigil_function_type_decl_t *right)
{
    size_t index;

    if (left == NULL || right == NULL)
    {
        return 0;
    }
    if (left->is_any || right->is_any)
    {
        return left->is_any == right->is_any;
    }
    if (left->param_count != right->param_count || left->return_count != right->return_count)
    {
        return 0;
    }

    for (index = 0U; index < left->param_count; index += 1U)
    {
        if (!vigil_parser_type_equal(left->param_types[index], right->param_types[index]))
        {
            return 0;
        }
    }
    for (index = 0U; index < left->return_count; index += 1U)
    {
        if (!vigil_parser_type_equal(left->return_types[index], right->return_types[index]))
        {
            return 0;
        }
    }

    return 1;
}

int vigil_program_find_function_type(const vigil_program_state_t *program, const vigil_function_type_decl_t *needle,
                                     size_t *out_index)
{
    size_t index;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }
    if (program == NULL || needle == NULL)
    {
        return 0;
    }

    for (index = 0U; index < program->function_type_count; index += 1U)
    {
        if (vigil_function_type_decl_matches(&program->function_types[index], needle))
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

vigil_status_t vigil_program_intern_function_type(vigil_program_state_t *program,
                                                  const vigil_function_type_decl_t *decl, vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    size_t index;

    if (out_type != NULL)
    {
        *out_type = vigil_binding_type_invalid();
    }
    if (program == NULL || decl == NULL || out_type == NULL)
    {
        vigil_error_set_literal(program == NULL ? NULL : program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "function type arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_program_find_function_type(program, decl, &index))
    {
        *out_type = vigil_binding_type_function(index);
        return VIGIL_STATUS_OK;
    }

    status = vigil_program_grow_function_types(program, program->function_type_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    index = program->function_type_count;
    program->function_types[index].is_any = decl->is_any;
    program->function_types[index].return_type = decl->return_type;
    if (decl->param_count != 0U)
    {
        status = vigil_function_type_decl_grow_params(program, &program->function_types[index], decl->param_count);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_function_type_decl_free(program, &program->function_types[index]);
            return status;
        }
        memcpy(program->function_types[index].param_types, decl->param_types,
               decl->param_count * sizeof(*decl->param_types));
        program->function_types[index].param_count = decl->param_count;
    }
    if (decl->return_count != 0U)
    {
        status = vigil_function_type_decl_grow_returns(program, &program->function_types[index], decl->return_count);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_function_type_decl_free(program, &program->function_types[index]);
            return status;
        }
        memcpy(program->function_types[index].return_types, decl->return_types,
               decl->return_count * sizeof(*decl->return_types));
        program->function_types[index].return_count = decl->return_count;
        program->function_types[index].return_type = decl->return_type;
    }
    program->function_type_count += 1U;
    *out_type = vigil_binding_type_function(index);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_intern_function_type_from_decl(vigil_program_state_t *program,
                                                            const vigil_function_decl_t *decl,
                                                            vigil_parser_type_t *out_type)
{
    vigil_function_type_decl_t function_type;
    vigil_status_t status;
    size_t index;

    memset(&function_type, 0, sizeof(function_type));
    for (index = 0U; index < decl->param_count; index += 1U)
    {
        status = vigil_function_type_decl_add_param(program, &function_type, decl->params[index].type);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_function_type_decl_free(program, &function_type);
            return status;
        }
    }
    for (index = 0U; index < decl->return_count; index += 1U)
    {
        status =
            vigil_function_type_decl_add_return(program, &function_type, vigil_function_return_type_at(decl, index));
        if (status != VIGIL_STATUS_OK)
        {
            vigil_function_type_decl_free(program, &function_type);
            return status;
        }
    }

    status = vigil_program_intern_function_type(program, &function_type, out_type);
    vigil_function_type_decl_free(program, &function_type);
    return status;
}

vigil_status_t vigil_class_decl_grow_fields(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                            size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->field_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->field_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*decl->fields))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "class field table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->fields;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->fields), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->fields),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_class_field_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->fields));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->fields = (vigil_class_field_t *)memory;
    decl->field_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_class_decl_grow_methods(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                             size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->method_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->method_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*decl->methods))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "class method table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->methods;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->methods), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->methods),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_class_method_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->methods));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->methods = (vigil_class_method_t *)memory;
    decl->method_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_class_decl_grow_implemented_interfaces(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                                            size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->implemented_interface_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->implemented_interface_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*decl->implemented_interfaces))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "class interface list allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->implemented_interfaces;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->implemented_interfaces),
                                     &memory, program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*decl->implemented_interfaces), program->error);
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->implemented_interfaces = (size_t *)memory;
    decl->implemented_interface_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_class_decl_grow_interface_impls(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                                     size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->interface_impl_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->interface_impl_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*decl->interface_impls))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "class interface implementation table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->interface_impls;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->interface_impls),
                                     &memory, program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*decl->interface_impls), program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_class_interface_impl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->interface_impls));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->interface_impls = (vigil_class_interface_impl_t *)memory;
    decl->interface_impl_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_interface_decl_grow_methods(vigil_program_state_t *program, vigil_interface_decl_t *decl,
                                                 size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->method_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->method_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*decl->methods))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "interface method table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->methods;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->methods), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->methods),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_interface_method_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->methods));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->methods = (vigil_interface_method_t *)memory;
    decl->method_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_grow_constants(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->constant_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->constant_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->constants))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "global constant table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->constants;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->constants), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->constants),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_global_constant_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->constants));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->constants = (vigil_global_constant_t *)memory;
    program->constant_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_grow_globals(vigil_program_state_t *program, size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->global_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->global_capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->globals))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "global variable table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->globals;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->globals), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*program->globals),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_global_variable_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->globals));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->globals = (vigil_global_variable_t *)memory;
    program->global_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_program_trim_text_range(const char *text, size_t start, size_t end, size_t *out_start, size_t *out_end)
{
    while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r'))
    {
        start += 1U;
    }
    while (end > start &&
           (text[end - 1U] == ' ' || text[end - 1U] == '\t' || text[end - 1U] == '\n' || text[end - 1U] == '\r'))
    {
        end -= 1U;
    }
    if (out_start != NULL)
    {
        *out_start = start;
    }
    if (out_end != NULL)
    {
        *out_end = end;
    }
}

vigil_status_t vigil_program_grow_functions(vigil_program_state_t *program, size_t minimum_capacity)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= program->functions.capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = program->functions.capacity;
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

    if (next_capacity > SIZE_MAX / sizeof(*program->functions.functions))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "function table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->functions.functions;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*program->functions.functions),
                                     &memory, program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*program->functions.functions), program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_function_decl_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*program->functions.functions));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    program->functions.functions = (vigil_function_decl_t *)memory;
    program->functions.capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_program_free(vigil_program_state_t *program)
{
    size_t i;
    void *memory;

    if (program == NULL)
    {
        return;
    }

    for (i = 0U; i < program->module_count; i += 1U)
    {
        size_t import_index;

        for (import_index = 0U; import_index < program->modules[i].import_count; import_index += 1U)
        {
            memory = program->modules[i].imports[import_index].owned_alias;
            vigil_runtime_free(program->registry->runtime, &memory);
        }
        memory = program->modules[i].imports;
        vigil_runtime_free(program->registry->runtime, &memory);
        if (program->modules[i].tokens != NULL)
        {
            memory = program->modules[i].tokens;
            vigil_token_list_free(program->modules[i].tokens);
            vigil_runtime_free(program->registry->runtime, &memory);
        }
    }
    memory = program->modules;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->modules = NULL;
    program->module_count = 0U;
    program->module_capacity = 0U;
    for (i = 0U; i < program->class_count; i += 1U)
    {
        vigil_class_decl_free(program, &program->classes[i]);
    }
    memory = program->classes;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->classes = NULL;
    program->class_count = 0U;
    program->class_capacity = 0U;
    for (i = 0U; i < program->interface_count; i += 1U)
    {
        vigil_interface_decl_free(program, &program->interfaces[i]);
    }
    memory = program->interfaces;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->interfaces = NULL;
    program->interface_count = 0U;
    program->interface_capacity = 0U;
    for (i = 0U; i < program->enum_count; i += 1U)
    {
        vigil_enum_decl_free(program, &program->enums[i]);
    }
    memory = program->enums;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->enums = NULL;
    program->enum_count = 0U;
    program->enum_capacity = 0U;
    memory = program->array_types;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->array_types = NULL;
    program->array_type_count = 0U;
    program->array_type_capacity = 0U;
    memory = program->map_types;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->map_types = NULL;
    program->map_type_count = 0U;
    program->map_type_capacity = 0U;
    for (i = 0U; i < program->function_type_count; i += 1U)
    {
        vigil_function_type_decl_free(program, &program->function_types[i]);
    }
    memory = program->function_types;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->function_types = NULL;
    program->function_type_count = 0U;
    program->function_type_capacity = 0U;
    for (i = 0U; i < program->constant_count; i += 1U)
    {
        vigil_value_release(&program->constants[i].value);
    }
    memory = program->constants;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->constants = NULL;
    program->constant_count = 0U;
    program->constant_capacity = 0U;
    memory = program->globals;
    vigil_runtime_free(program->registry->runtime, &memory);
    program->globals = NULL;
    program->global_count = 0U;
    program->global_capacity = 0U;
    {
        void *efn = program->extern_fns;
        vigil_runtime_free(program->registry->runtime, &efn);
    }
    program->extern_fns = NULL;
    program->extern_fn_count = 0U;
    program->extern_fn_capacity = 0U;
    vigil_binding_function_table_free(&program->functions);
}
