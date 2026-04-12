#include "internal/vigil_compiler_types.h"

int vigil_parser_type_is_primitive(vigil_parser_type_t type)
{
    return type.kind != VIGIL_TYPE_INVALID && type.kind != VIGIL_TYPE_OBJECT &&
           type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_class(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_OBJECT && type.object_kind == VIGIL_BINDING_OBJECT_CLASS &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_is_interface(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_OBJECT && type.object_kind == VIGIL_BINDING_OBJECT_INTERFACE &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_is_enum(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_I32 && type.object_kind == VIGIL_BINDING_OBJECT_ENUM &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_is_array(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_OBJECT && type.object_kind == VIGIL_BINDING_OBJECT_ARRAY &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_is_map(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_OBJECT && type.object_kind == VIGIL_BINDING_OBJECT_MAP &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_supports_map_key(vigil_parser_type_t type)
{
    return (type.kind == VIGIL_TYPE_STRING && type.object_kind == VIGIL_BINDING_OBJECT_NONE) ||
           (type.kind == VIGIL_TYPE_BOOL && type.object_kind == VIGIL_BINDING_OBJECT_NONE) ||
           ((type.kind == VIGIL_TYPE_I32 || type.kind == VIGIL_TYPE_I64 || type.kind == VIGIL_TYPE_U8 ||
             type.kind == VIGIL_TYPE_U32 || type.kind == VIGIL_TYPE_U64) &&
            type.object_kind == VIGIL_BINDING_OBJECT_NONE) ||
           (type.kind == VIGIL_TYPE_I32 && type.object_kind == VIGIL_BINDING_OBJECT_ENUM &&
            type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX);
}

int vigil_parser_type_is_function(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_OBJECT && type.object_kind == VIGIL_BINDING_OBJECT_FUNCTION &&
           type.object_index != VIGIL_BINDING_INVALID_CLASS_INDEX;
}

int vigil_parser_type_is_void(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_VOID && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_i32(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_I32 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_i64(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_I64 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_u8(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_U8 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_u32(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_U32 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_u64(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_U64 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_integer(vigil_parser_type_t type)
{
    return vigil_parser_type_is_i32(type) || vigil_parser_type_is_i64(type) || vigil_parser_type_is_u8(type) ||
           vigil_parser_type_is_u32(type) || vigil_parser_type_is_u64(type);
}

int vigil_parser_type_is_signed_integer(vigil_parser_type_t type)
{
    return vigil_parser_type_is_i32(type) || vigil_parser_type_is_i64(type);
}

int vigil_parser_type_is_unsigned_integer(vigil_parser_type_t type)
{
    return vigil_parser_type_is_u8(type) || vigil_parser_type_is_u32(type) || vigil_parser_type_is_u64(type);
}

int vigil_parser_type_is_f64(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_F64 && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_bool(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_BOOL && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_string(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_STRING && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

int vigil_parser_type_is_err(vigil_parser_type_t type)
{
    return type.kind == VIGIL_TYPE_ERR && type.object_kind == VIGIL_BINDING_OBJECT_NONE;
}

vigil_parser_type_t vigil_function_return_type_at(const vigil_function_decl_t *decl, size_t index)
{
    if (decl == NULL || index >= decl->return_count)
    {
        return vigil_binding_type_invalid();
    }

    if (decl->return_count == 1U || decl->return_types == NULL)
    {
        return decl->return_type;
    }

    return decl->return_types[index];
}

const vigil_parser_type_t *vigil_function_return_types(const vigil_function_decl_t *decl)
{
    if (decl == NULL || decl->return_count == 0U)
    {
        return NULL;
    }

    if (decl->return_count == 1U || decl->return_types == NULL)
    {
        return &decl->return_type;
    }

    return decl->return_types;
}

vigil_parser_type_t vigil_interface_method_return_type_at(const vigil_interface_method_t *method, size_t index)
{
    if (method == NULL || index >= method->return_count)
    {
        return vigil_binding_type_invalid();
    }

    if (method->return_count == 1U || method->return_types == NULL)
    {
        return method->return_type;
    }

    return method->return_types[index];
}

const vigil_parser_type_t *vigil_interface_method_return_types(const vigil_interface_method_t *method)
{
    if (method == NULL || method->return_count == 0U)
    {
        return NULL;
    }

    if (method->return_count == 1U || method->return_types == NULL)
    {
        return &method->return_type;
    }

    return method->return_types;
}

int vigil_parser_type_equal(vigil_parser_type_t left, vigil_parser_type_t right)
{
    return vigil_binding_type_equal(left, right);
}

int vigil_program_type_is_assignable(const vigil_program_state_t *program, vigil_parser_type_t target_type,
                                     vigil_parser_type_t source_type)
{
    if (!vigil_binding_type_is_valid(target_type) || !vigil_binding_type_is_valid(source_type))
    {
        return 0;
    }

    if (vigil_parser_type_is_interface(target_type) && vigil_parser_type_is_class(source_type))
    {
        if (program == NULL)
        {
            return 0;
        }

        return vigil_class_decl_implements_interface(&program->classes[source_type.object_index],
                                                     target_type.object_index);
    }

    if (vigil_parser_type_is_function(target_type))
    {
        const vigil_function_type_decl_t *target_decl;
        const vigil_function_type_decl_t *source_decl;
        size_t index;

        if (program == NULL || !vigil_parser_type_is_function(source_type))
        {
            return 0;
        }
        target_decl = vigil_program_function_type_decl(program, target_type);
        source_decl = vigil_program_function_type_decl(program, source_type);
        if (target_decl == NULL || source_decl == NULL)
        {
            return 0;
        }
        if (target_decl->is_any)
        {
            return 1;
        }
        if (source_decl->is_any)
        {
            return 0;
        }
        if (target_decl->param_count != source_decl->param_count ||
            target_decl->return_count != source_decl->return_count)
        {
            return 0;
        }
        for (index = 0U; index < target_decl->param_count; index += 1U)
        {
            if (!vigil_parser_type_equal(target_decl->param_types[index], source_decl->param_types[index]))
            {
                return 0;
            }
        }
        for (index = 0U; index < target_decl->return_count; index += 1U)
        {
            if (!vigil_parser_type_equal(target_decl->return_types[index], source_decl->return_types[index]))
            {
                return 0;
            }
        }
        return 1;
    }

    if (vigil_parser_type_is_class(target_type) || vigil_parser_type_is_class(source_type) ||
        vigil_parser_type_is_interface(target_type) || vigil_parser_type_is_interface(source_type) ||
        vigil_parser_type_is_enum(target_type) || vigil_parser_type_is_enum(source_type) ||
        vigil_parser_type_is_array(target_type) || vigil_parser_type_is_array(source_type) ||
        vigil_parser_type_is_function(target_type) || vigil_parser_type_is_function(source_type) ||
        vigil_parser_type_is_map(target_type) || vigil_parser_type_is_map(source_type))
    {
        return vigil_parser_type_equal(target_type, source_type);
    }

    return vigil_type_is_assignable(target_type.kind, source_type.kind);
}

int vigil_parser_type_supports_unary_operator(vigil_unary_operator_kind_t operator_kind,
                                              vigil_parser_type_t operand_type)
{
    if (!vigil_parser_type_is_primitive(operand_type))
    {
        return 0;
    }

    return vigil_type_supports_unary_operator(operator_kind, operand_type.kind);
}

int vigil_parser_type_supports_binary_operator(vigil_binary_operator_kind_t operator_kind,
                                               vigil_parser_type_t left_type, vigil_parser_type_t right_type)
{
    if (!vigil_binding_type_is_valid(left_type) || !vigil_binding_type_is_valid(right_type))
    {
        return 0;
    }

    if (operator_kind == VIGIL_BINARY_OPERATOR_EQUAL || operator_kind == VIGIL_BINARY_OPERATOR_NOT_EQUAL)
    {
        if (vigil_parser_type_is_function(left_type) && vigil_parser_type_is_function(right_type))
        {
            return 1;
        }

        return vigil_parser_type_equal(left_type, right_type);
    }

    if (!vigil_parser_type_is_primitive(left_type) || !vigil_parser_type_is_primitive(right_type))
    {
        return 0;
    }

    return vigil_type_supports_binary_operator(operator_kind, left_type.kind, right_type.kind);
}

const vigil_function_type_decl_t *vigil_program_function_type_decl(const vigil_program_state_t *program,
                                                                   vigil_parser_type_t type)
{
    if (program == NULL || !vigil_parser_type_is_function(type) || type.object_index >= program->function_type_count)
    {
        return NULL;
    }

    return &program->function_types[type.object_index];
}

int vigil_class_decl_implements_interface(const vigil_class_decl_t *decl, size_t interface_index)
{
    size_t i;

    if (decl == NULL)
    {
        return 0;
    }

    for (i = 0U; i < decl->implemented_interface_count; i += 1U)
    {
        if (decl->implemented_interfaces[i] == interface_index)
        {
            return 1;
        }
    }

    return 0;
}
