#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/log.h"

static const vigil_logger_t VIGIL_DEFAULT_LOGGER = {VIGIL_LOG_INFO, vigil_stderr_log_handler, NULL};

static const vigil_logger_t *vigil_resolve_logger(const vigil_logger_t *logger)
{
    if (logger == NULL)
    {
        return &VIGIL_DEFAULT_LOGGER;
    }

    return logger;
}

static int vigil_log_level_is_valid(vigil_log_level_t level)
{
    return level >= VIGIL_LOG_DEBUG && level <= VIGIL_LOG_FATAL;
}

static vigil_status_t vigil_logger_validate(const vigil_logger_t *logger, vigil_error_t *error)
{
    if (!vigil_log_level_is_valid(logger->minimum_level))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "logger minimum_level is invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_log_handler_fn vigil_resolve_handler(const vigil_logger_t *logger)
{
    if (logger->handler != NULL)
    {
        return logger->handler;
    }

    return vigil_stderr_log_handler;
}

static void vigil_write_quoted(FILE *stream, const char *value)
{
    size_t index;

    fputc('"', stream);
    if (value != NULL)
    {
        for (index = 0U; value[index] != '\0'; index += 1U)
        {
            char ch;

            ch = value[index];
            switch (ch)
            {
            case '\\':
            case '"':
                fputc('\\', stream);
                fputc(ch, stream);
                break;
            case '\n':
                fputs("\\n", stream);
                break;
            case '\r':
                fputs("\\r", stream);
                break;
            case '\t':
                fputs("\\t", stream);
                break;
            default:
                fputc(ch, stream);
                break;
            }
        }
    }
    fputc('"', stream);
}

void vigil_logger_init(vigil_logger_t *logger)
{
    if (logger == NULL)
    {
        return;
    }

    *logger = VIGIL_DEFAULT_LOGGER;
}

const char *vigil_log_level_name(vigil_log_level_t level)
{
    switch (level)
    {
    case VIGIL_LOG_DEBUG:
        return "debug";
    case VIGIL_LOG_INFO:
        return "info";
    case VIGIL_LOG_WARNING:
        return "warning";
    case VIGIL_LOG_ERROR:
        return "error";
    case VIGIL_LOG_FATAL:
        return "fatal";
    default:
        return "unknown";
    }
}

int vigil_logger_should_log(const vigil_logger_t *logger, vigil_log_level_t level)
{
    const vigil_logger_t *resolved_logger;

    if (!vigil_log_level_is_valid(level))
    {
        return 0;
    }

    resolved_logger = vigil_resolve_logger(logger);
    if (level == VIGIL_LOG_FATAL)
    {
        return 1;
    }

    return level >= resolved_logger->minimum_level;
}

void vigil_stderr_log_handler(void *user_data, const vigil_log_record_t *record)
{
    size_t index;

    (void)user_data;

    if (record == NULL || record->message == NULL)
    {
        return;
    }

    fprintf(stderr, "level=%s msg=", vigil_log_level_name(record->level));
    vigil_write_quoted(stderr, record->message);

    for (index = 0U; index < record->field_count; index += 1U)
    {
        const vigil_log_field_t *field;

        field = &record->fields[index];
        if (field->key == NULL || field->value == NULL)
        {
            continue;
        }

        fprintf(stderr, " %s=", field->key);
        vigil_write_quoted(stderr, field->value);
    }

    fputc('\n', stderr);
    fflush(stderr);
}

vigil_status_t vigil_logger_log(const vigil_logger_t *logger, vigil_log_level_t level, const char *message,
                                const vigil_log_field_t *fields, size_t field_count, vigil_error_t *error)
{
    const vigil_logger_t *resolved_logger;
    vigil_log_handler_fn handler;
    vigil_log_record_t record;
    size_t index;
    vigil_status_t status;

    vigil_error_clear(error);

    if (!vigil_log_level_is_valid(level))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "log level is invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "log message must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (field_count != 0U && fields == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "log fields must not be null when field_count is non-zero");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < field_count; index += 1U)
    {
        if (fields[index].key == NULL || fields[index].value == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "log fields must define key and value");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }

    resolved_logger = vigil_resolve_logger(logger);
    status = vigil_logger_validate(resolved_logger, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (!vigil_logger_should_log(resolved_logger, level))
    {
        return VIGIL_STATUS_OK;
    }

    handler = vigil_resolve_handler(resolved_logger);
    record.level = level;
    record.message = message;
    record.message_length = strlen(message);
    record.fields = fields;
    record.field_count = field_count;
    handler(resolved_logger->user_data, &record);

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_logger_debug(const vigil_logger_t *logger, const char *message, vigil_error_t *error)
{
    return vigil_logger_log(logger, VIGIL_LOG_DEBUG, message, NULL, 0U, error);
}

vigil_status_t vigil_logger_info(const vigil_logger_t *logger, const char *message, vigil_error_t *error)
{
    return vigil_logger_log(logger, VIGIL_LOG_INFO, message, NULL, 0U, error);
}

vigil_status_t vigil_logger_warning(const vigil_logger_t *logger, const char *message, vigil_error_t *error)
{
    return vigil_logger_log(logger, VIGIL_LOG_WARNING, message, NULL, 0U, error);
}

vigil_status_t vigil_logger_error(const vigil_logger_t *logger, const char *message, vigil_error_t *error)
{
    return vigil_logger_log(logger, VIGIL_LOG_ERROR, message, NULL, 0U, error);
}

void vigil_logger_fatal(const vigil_logger_t *logger, const char *message, const vigil_log_field_t *fields,
                        size_t field_count)
{
    vigil_error_t error;
    vigil_log_record_t record;

    memset(&error, 0, sizeof(error));
    if (vigil_logger_log(logger, VIGIL_LOG_FATAL, message, fields, field_count, &error) != VIGIL_STATUS_OK)
    {
        record.level = VIGIL_LOG_FATAL;
        record.message = vigil_error_message(&error) != NULL ? vigil_error_message(&error) : "fatal log failure";
        record.message_length = strlen(record.message);
        record.fields = NULL;
        record.field_count = 0U;
        vigil_stderr_log_handler(NULL, &record);
    }

    abort();
}
