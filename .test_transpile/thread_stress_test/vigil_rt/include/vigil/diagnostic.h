#ifndef VIGIL_DIAGNOSTIC_H
#define VIGIL_DIAGNOSTIC_H

#include <stddef.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_diagnostic_severity
    {
        VIGIL_DIAGNOSTIC_ERROR = 0,
        VIGIL_DIAGNOSTIC_WARNING = 1,
        VIGIL_DIAGNOSTIC_NOTE = 2
    } vigil_diagnostic_severity_t;

    typedef struct vigil_diagnostic
    {
        vigil_diagnostic_severity_t severity;
        vigil_source_span_t span;
        vigil_string_t message;
    } vigil_diagnostic_t;

    typedef struct vigil_diagnostic_list
    {
        vigil_runtime_t *runtime;
        vigil_diagnostic_t *items;
        size_t count;
        size_t capacity;
    } vigil_diagnostic_list_t;

    VIGIL_API void vigil_diagnostic_list_init(vigil_diagnostic_list_t *list, vigil_runtime_t *runtime);
    VIGIL_API void vigil_diagnostic_list_clear(vigil_diagnostic_list_t *list);
    VIGIL_API void vigil_diagnostic_list_free(vigil_diagnostic_list_t *list);
    VIGIL_API size_t vigil_diagnostic_list_count(const vigil_diagnostic_list_t *list);
    VIGIL_API const char *vigil_diagnostic_severity_name(vigil_diagnostic_severity_t severity);
    /*
     * The returned pointer is invalidated by any subsequent list mutation,
     * including append calls that may reallocate the backing array.
     */
    VIGIL_API const vigil_diagnostic_t *vigil_diagnostic_list_get(const vigil_diagnostic_list_t *list, size_t index);
    VIGIL_API vigil_status_t vigil_diagnostic_list_append(vigil_diagnostic_list_t *list,
                                                          vigil_diagnostic_severity_t severity,
                                                          vigil_source_span_t span, const char *message,
                                                          size_t message_length, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_diagnostic_list_append_cstr(vigil_diagnostic_list_t *list,
                                                               vigil_diagnostic_severity_t severity,
                                                               vigil_source_span_t span, const char *message,
                                                               vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_diagnostic_format(const vigil_source_registry_t *registry,
                                                     const vigil_diagnostic_t *diagnostic, vigil_string_t *output,
                                                     vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
