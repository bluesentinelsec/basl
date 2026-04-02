/*
 * stdlib/xml.c — VIGIL xml stdlib module.
 *
 * Exposes xml.parse(text) -> (xml.Document, err) and xml.Element
 * navigation to VIGIL programs.
 */

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_xml.h"
#include "vigil/native_module.h"
#include "vigil/value.h"
#include "vigil/vm.h"

/* ── Field indexes ───────────────────────────────────────────────── */

enum
{
    DOC_FIELD_PTR = 0, /* opaque i64 holding vigil_xml_document_t* */
    DOC_FIELD_COUNT = 1
};

enum
{
    ELEM_FIELD_PTR = 0, /* opaque i64 holding vigil_xml_element_t* */
    ELEM_FIELD_COUNT = 1
};

/* ── Error helpers ────────────────────────────────────────────────── */

static vigil_status_t xml_push_err(vigil_vm_t *vm, const char *message, size_t message_len, vigil_error_t *error)
{
    vigil_object_t *err_obj = NULL;
    vigil_value_t val;
    vigil_status_t status;
    (void)message_len;
    status = vigil_error_object_new_cstr(vigil_vm_runtime(vm), message != NULL ? message : "", 10, &err_obj, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &err_obj);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

static vigil_status_t xml_push_ok_err(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_xml_document_t *xml_get_doc_ptr(vigil_vm_t *vm, size_t base)
{
    vigil_value_t self_val = vigil_vm_stack_get(vm, base);
    vigil_object_t *self = vigil_value_as_object(&self_val);
    vigil_value_t ptr_val;
    if (self == NULL || !vigil_instance_object_get_field(self, DOC_FIELD_PTR, &ptr_val))
        return NULL;
    int64_t raw = vigil_value_as_int(&ptr_val);
    vigil_value_release(&ptr_val);
    return (vigil_xml_document_t *)(uintptr_t)raw;
}

static const vigil_xml_element_t *xml_get_elem_ptr(vigil_vm_t *vm, size_t base)
{
    vigil_value_t self_val = vigil_vm_stack_get(vm, base);
    vigil_object_t *self = vigil_value_as_object(&self_val);
    vigil_value_t ptr_val;
    if (self == NULL || !vigil_instance_object_get_field(self, ELEM_FIELD_PTR, &ptr_val))
        return NULL;
    int64_t raw = vigil_value_as_int(&ptr_val);
    vigil_value_release(&ptr_val);
    return (const vigil_xml_element_t *)(uintptr_t)raw;
}

static vigil_status_t xml_push_string(vigil_vm_t *vm, const char *text, vigil_error_t *error)
{
    vigil_object_t *str = NULL;
    vigil_value_t val;
    vigil_status_t status;
    if (text == NULL)
        text = "";
    status = vigil_string_object_new(vigil_vm_runtime(vm), text, strlen(text), &str, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &str);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

static vigil_status_t xml_wrap_element(vigil_vm_t *vm, size_t class_index, const vigil_xml_element_t *el,
                                       vigil_error_t *error)
{
    vigil_value_t fields[ELEM_FIELD_COUNT];
    vigil_object_t *instance = NULL;
    vigil_value_t val;
    vigil_status_t status;

    vigil_value_init_int(&fields[ELEM_FIELD_PTR], (int64_t)(uintptr_t)el);
    status = vigil_instance_object_new(vigil_vm_runtime(vm), class_index, fields, ELEM_FIELD_COUNT, &instance, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &instance);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

/* ── Document methods ────────────────────────────────────────────── */

static vigil_status_t xml_doc_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index;
    vigil_value_t text_val;
    vigil_object_t *text_obj;
    const char *text;
    size_t text_len;
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t xml_err;
    vigil_status_t status;
    vigil_value_t fields[DOC_FIELD_COUNT];
    vigil_object_t *instance = NULL;
    vigil_value_t result;

    class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    text_val = vigil_vm_stack_get(vm, base + 1U);
    text_obj = vigil_value_as_object(&text_val);
    if (text_obj == NULL || vigil_object_type(text_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        status = xml_push_string(vm, "", error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return xml_push_err(vm, "xml.parse() requires a string argument", 38U, error);
    }
    text = vigil_string_object_c_str(text_obj);
    text_len = vigil_string_object_length(text_obj);

    status = vigil_xml_parse(text, text_len, &doc, &xml_err);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        status = xml_push_string(vm, "", error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return xml_push_err(vm, xml_err.message, strlen(xml_err.message), error);
    }

    vigil_value_init_int(&fields[DOC_FIELD_PTR], (int64_t)(uintptr_t)doc);
    status = vigil_instance_object_new(vigil_vm_runtime(vm), class_index, fields, DOC_FIELD_COUNT, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_xml_document_free(doc);
        return status;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_init_object(&result, &instance);
    status = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    return xml_push_ok_err(vm, error);
}

static vigil_status_t xml_doc_root(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_xml_document_t *doc;
    size_t self_class;
    size_t elem_class;
    vigil_value_t self_val;
    vigil_object_t *self_obj;

    doc = xml_get_doc_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    self_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - arg_count);
    self_obj = vigil_value_as_object(&self_val);
    self_class = self_obj != NULL ? vigil_instance_object_class_index(self_obj) : 0U;
    /* Element class is the next class after Document in the module */
    elem_class = self_class + 1U;

    vigil_vm_stack_pop_n(vm, arg_count);
    if (doc == NULL || doc->root == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "document has no root element");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return xml_wrap_element(vm, elem_class, doc->root, error);
}

static vigil_status_t xml_doc_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_xml_document_t *doc;
    doc = xml_get_doc_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return xml_push_string(vm, doc != NULL ? doc->version : NULL, error);
}

static vigil_status_t xml_doc_encoding(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_xml_document_t *doc;
    doc = xml_get_doc_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return xml_push_string(vm, doc != NULL ? doc->encoding : NULL, error);
}

static vigil_status_t xml_doc_to_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_xml_document_t *doc;
    char *output;
    size_t output_len;
    vigil_status_t status;

    doc = xml_get_doc_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    output = vigil_xml_document_to_string(doc, &output_len);
    if (output == NULL)
        return xml_push_string(vm, "", error);
    status = xml_push_string(vm, output, error);
    free(output);
    return status;
}

/* ── Element methods ─────────────────────────────────────────────── */

static vigil_status_t xml_elem_tag(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const vigil_xml_element_t *el;
    el = xml_get_elem_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return xml_push_string(vm, el != NULL ? el->tag : NULL, error);
}

static vigil_status_t xml_elem_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const vigil_xml_element_t *el;
    el = xml_get_elem_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return xml_push_string(vm, el != NULL ? el->text : NULL, error);
}

static vigil_status_t xml_elem_all_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const vigil_xml_element_t *el;
    char *all;
    size_t len;
    vigil_status_t status;

    el = xml_get_elem_ptr(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    all = vigil_xml_element_all_text(el, &len);
    status = xml_push_string(vm, all, error);
    free(all);
    return status;
}

static vigil_status_t xml_elem_attr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_xml_element_t *el;
    vigil_value_t name_val;
    vigil_object_t *name_obj;
    const char *name;
    const char *value;
    vigil_status_t status;
    vigil_value_t bool_val;

    el = xml_get_elem_ptr(vm, base);
    name_val = vigil_vm_stack_get(vm, base + 1U);
    name_obj = vigil_value_as_object(&name_val);
    if (name_obj == NULL || vigil_object_type(name_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "attr() requires a string name");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    name = vigil_string_object_c_str(name_obj);
    value = vigil_xml_element_attr(el, name);

    vigil_vm_stack_pop_n(vm, arg_count);
    status = xml_push_string(vm, value != NULL ? value : "", error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_bool(&bool_val, value != NULL);
    status = vigil_vm_stack_push(vm, &bool_val, error);
    vigil_value_release(&bool_val);
    return status;
}

static vigil_status_t xml_elem_children(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_xml_element_t *el;
    size_t elem_class;
    vigil_object_t *arr = NULL;
    vigil_value_t arr_val;
    vigil_status_t status;
    size_t i;
    vigil_value_t self_val;
    vigil_object_t *self_obj;

    el = xml_get_elem_ptr(vm, base);
    self_val = vigil_vm_stack_get(vm, base);
    self_obj = vigil_value_as_object(&self_val);
    elem_class = self_obj != NULL ? vigil_instance_object_class_index(self_obj) : 0U;

    status = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0U, &arr, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (el != NULL)
    {
        for (i = 0U; i < el->child_count; i++)
        {
            vigil_value_t fields[ELEM_FIELD_COUNT];
            vigil_object_t *inst = NULL;
            vigil_value_t inst_val;

            vigil_value_init_int(&fields[ELEM_FIELD_PTR], (int64_t)(uintptr_t)el->children[i]);
            status =
                vigil_instance_object_new(vigil_vm_runtime(vm), elem_class, fields, ELEM_FIELD_COUNT, &inst, error);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return status;
            }
            vigil_value_init_object(&inst_val, &inst);
            status = vigil_array_object_append(arr, &inst_val, error);
            vigil_value_release(&inst_val);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return status;
            }
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_init_object(&arr_val, &arr);
    status = vigil_vm_stack_push(vm, &arr_val, error);
    vigil_value_release(&arr_val);
    return status;
}

static vigil_status_t xml_elem_children_by_tag(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_xml_element_t *el;
    vigil_value_t tag_val;
    vigil_object_t *tag_obj;
    const char *tag;
    size_t elem_class;
    vigil_object_t *arr = NULL;
    vigil_value_t arr_val;
    vigil_status_t status;
    size_t i;
    vigil_value_t self_val;
    vigil_object_t *self_obj;

    el = xml_get_elem_ptr(vm, base);
    self_val = vigil_vm_stack_get(vm, base);
    self_obj = vigil_value_as_object(&self_val);
    elem_class = self_obj != NULL ? vigil_instance_object_class_index(self_obj) : 0U;

    tag_val = vigil_vm_stack_get(vm, base + 1U);
    tag_obj = vigil_value_as_object(&tag_val);
    if (tag_obj == NULL || vigil_object_type(tag_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "children_by_tag() requires a string tag");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    tag = vigil_string_object_c_str(tag_obj);

    status = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0U, &arr, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (el != NULL)
    {
        for (i = 0U; i < el->child_count; i++)
        {
            vigil_value_t fields[ELEM_FIELD_COUNT];
            vigil_object_t *inst = NULL;
            vigil_value_t inst_val;

            if (strcmp(el->children[i]->tag, tag) != 0)
                continue;
            vigil_value_init_int(&fields[ELEM_FIELD_PTR], (int64_t)(uintptr_t)el->children[i]);
            status =
                vigil_instance_object_new(vigil_vm_runtime(vm), elem_class, fields, ELEM_FIELD_COUNT, &inst, error);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return status;
            }
            vigil_value_init_object(&inst_val, &inst);
            status = vigil_array_object_append(arr, &inst_val, error);
            vigil_value_release(&inst_val);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return status;
            }
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_init_object(&arr_val, &arr);
    status = vigil_vm_stack_push(vm, &arr_val, error);
    vigil_value_release(&arr_val);
    return status;
}

static vigil_status_t xml_elem_child(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_xml_element_t *el;
    vigil_value_t tag_val;
    vigil_object_t *tag_obj;
    const char *tag;
    const vigil_xml_element_t *found;
    size_t elem_class;
    vigil_status_t status;
    vigil_value_t self_val;
    vigil_object_t *self_obj;

    el = xml_get_elem_ptr(vm, base);
    self_val = vigil_vm_stack_get(vm, base);
    self_obj = vigil_value_as_object(&self_val);
    elem_class = self_obj != NULL ? vigil_instance_object_class_index(self_obj) : 0U;

    tag_val = vigil_vm_stack_get(vm, base + 1U);
    tag_obj = vigil_value_as_object(&tag_val);
    if (tag_obj == NULL || vigil_object_type(tag_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "child() requires a string tag");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    tag = vigil_string_object_c_str(tag_obj);
    found = vigil_xml_element_child(el, tag);

    vigil_vm_stack_pop_n(vm, arg_count);
    if (found == NULL)
    {
        /* Push a placeholder element and an error */
        status = xml_wrap_element(vm, elem_class, el, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return xml_push_err(vm, "child not found", 15U, error);
    }
    status = xml_wrap_element(vm, elem_class, found, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return xml_push_ok_err(vm, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int obj_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int str_bool_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_BOOL};

static const char *xml_text_param_names[] = {"text"};
static const char *xml_name_param_names[] = {"name"};
static const char *xml_tag_param_names[] = {"tag"};

static const vigil_native_symbol_doc_t xml_module_doc = {"XML parser.", "Parse and navigate XML documents.", NULL};
static const vigil_native_symbol_doc_t xml_doc_doc = {"XML document.", "Represents a parsed XML document.", NULL};
static const vigil_native_symbol_doc_t xml_elem_doc = {"XML element.", "Represents an element in the XML tree.", NULL};
static const vigil_native_symbol_doc_t xml_parse_doc = {"Parse XML text.",
                                                        "Parses a string as XML and returns a Document.", NULL};
static const vigil_native_symbol_doc_t xml_root_doc = {"Get root element.", "Returns the document root element.", NULL};
static const vigil_native_symbol_doc_t xml_version_doc = {"Get XML version.",
                                                          "Returns the version from the XML declaration.", NULL};
static const vigil_native_symbol_doc_t xml_encoding_doc = {"Get encoding.",
                                                           "Returns the encoding from the XML declaration.", NULL};
static const vigil_native_symbol_doc_t xml_to_string_doc = {"Serialize to XML.",
                                                            "Returns the document as an XML string.", NULL};
static const vigil_native_symbol_doc_t xml_tag_doc = {"Get tag name.", "Returns the element tag name.", NULL};
static const vigil_native_symbol_doc_t xml_text_doc = {"Get text content.", "Returns the direct text content.", NULL};
static const vigil_native_symbol_doc_t xml_all_text_doc = {"Get all text.", "Returns recursive text content.", NULL};
static const vigil_native_symbol_doc_t xml_attr_doc = {"Get attribute.",
                                                       "Returns the attribute value and whether it exists.", NULL};
static const vigil_native_symbol_doc_t xml_children_doc = {"Get children.", "Returns all child elements.", NULL};
static const vigil_native_symbol_doc_t xml_children_by_tag_doc = {"Get children by tag.",
                                                                  "Returns child elements matching the tag.", NULL};
static const vigil_native_symbol_doc_t xml_child_doc = {"Get first child by tag.",
                                                        "Returns the first child with the given tag.", NULL};

static const vigil_native_symbol_doc_t xml_doc_ptr_doc = {"Internal pointer.", "Opaque document handle.", NULL};
static const vigil_native_symbol_doc_t xml_elem_ptr_doc = {"Internal pointer.", "Opaque element handle.", NULL};

static const vigil_native_class_field_t xml_doc_fields[] = {
    {"_ptr", 4U, VIGIL_TYPE_I64, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &xml_doc_ptr_doc},
};

static const vigil_native_class_field_t xml_elem_fields[] = {
    {"_ptr", 4U, VIGIL_TYPE_I64, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &xml_elem_ptr_doc},
};

static const vigil_native_class_method_t xml_doc_methods[] = {
    {"parse", 5U, xml_doc_parse, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Document", 8U, 0,
     xml_text_param_names, NULL, "xml.Document", &xml_parse_doc},
    {"root", 4U, xml_doc_root, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Element", 7U, 0, NULL, NULL, "xml.Element",
     &xml_root_doc},
    {"version", 7U, xml_doc_version, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &xml_version_doc},
    {"encoding", 8U, xml_doc_encoding, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &xml_encoding_doc},
    {"to_string", 9U, xml_doc_to_string, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &xml_to_string_doc},
};

static const vigil_native_class_method_t xml_elem_methods[] = {
    {"tag", 3U, xml_elem_tag, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &xml_tag_doc},
    {"text", 4U, xml_elem_text, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &xml_text_doc},
    {"all_text", 8U, xml_elem_all_text, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &xml_all_text_doc},
    {"attr", 4U, xml_elem_attr, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_bool_returns, 0, NULL, 0U, 0,
     xml_name_param_names, NULL, NULL, &xml_attr_doc},
    {"children", 8U, xml_elem_children, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &xml_children_doc},
    {"children_by_tag", 15U, xml_elem_children_by_tag, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U, 0,
     xml_tag_param_names, NULL, NULL, &xml_children_by_tag_doc},
    {"child", 5U, xml_elem_child, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL, 0U, 0,
     xml_tag_param_names, NULL, "xml.Element", &xml_child_doc},
};

static const vigil_native_class_t xml_classes[] = {
    {"Document", 8U, xml_doc_fields, DOC_FIELD_COUNT, xml_doc_methods,
     sizeof(xml_doc_methods) / sizeof(xml_doc_methods[0]), NULL, &xml_doc_doc},
    {"Element", 7U, xml_elem_fields, ELEM_FIELD_COUNT, xml_elem_methods,
     sizeof(xml_elem_methods) / sizeof(xml_elem_methods[0]), NULL, &xml_elem_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_xml = {
    "xml", 3U, NULL, 0U, xml_classes, sizeof(xml_classes) / sizeof(xml_classes[0]), &xml_module_doc};
