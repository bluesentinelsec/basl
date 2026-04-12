#ifndef VIGIL_LOG_H
#define VIGIL_LOG_H

#include <stddef.h>

#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_log_level
    {
        VIGIL_LOG_DEBUG = 0,
        VIGIL_LOG_INFO = 1,
        VIGIL_LOG_WARNING = 2,
        VIGIL_LOG_ERROR = 3,
        VIGIL_LOG_FATAL = 4
    } vigil_log_level_t;

    typedef struct vigil_log_field
    {
        const char *key;
        const char *value;
    } vigil_log_field_t;

    typedef struct vigil_log_record
    {
        vigil_log_level_t level;
        const char *message;
        size_t message_length;
        const vigil_log_field_t *fields;
        size_t field_count;
    } vigil_log_record_t;

    typedef void (*vigil_log_handler_fn)(void *user_data, const vigil_log_record_t *record);

    typedef struct vigil_logger
    {
        vigil_log_level_t minimum_level;
        vigil_log_handler_fn handler;
        void *user_data;
    } vigil_logger_t;

    VIGIL_API void vigil_logger_init(vigil_logger_t *logger);
    VIGIL_API const char *vigil_log_level_name(vigil_log_level_t level);
    VIGIL_API int vigil_logger_should_log(const vigil_logger_t *logger, vigil_log_level_t level);
    VIGIL_API void vigil_stderr_log_handler(void *user_data, const vigil_log_record_t *record);
    VIGIL_API vigil_status_t vigil_logger_log(const vigil_logger_t *logger, vigil_log_level_t level,
                                              const char *message, const vigil_log_field_t *fields, size_t field_count,
                                              vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_logger_debug(const vigil_logger_t *logger, const char *message,
                                                vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_logger_info(const vigil_logger_t *logger, const char *message, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_logger_warning(const vigil_logger_t *logger, const char *message,
                                                  vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_logger_error(const vigil_logger_t *logger, const char *message,
                                                vigil_error_t *error);
    VIGIL_API void vigil_logger_fatal(const vigil_logger_t *logger, const char *message,
                                      const vigil_log_field_t *fields, size_t field_count);

#ifdef __cplusplus
}
#endif

#endif
