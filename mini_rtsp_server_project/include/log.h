/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: log.h
 * Description: Small thread-safe logger for the embedded media server.
 ********************************************************************************/

#ifndef PROJECT_LOG_H
#define PROJECT_LOG_H

#include <stdarg.h>

typedef enum
{
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
} LogLevel;

void log_set_level(LogLevel level);
LogLevel log_get_level(void);
const char *log_level_name(LogLevel level);
int log_level_parse(const char *text, LogLevel *level);

void log_write(LogLevel level,
               const char *module,
               const char *format,
               ...);

void log_write_errno(LogLevel level,
                     const char *module,
                     int error_number,
                     const char *format,
                     ...);

#define LOG_ERROR(module, ...) \
    log_write(LOG_LEVEL_ERROR, (module), __VA_ARGS__)
#define LOG_WARN(module, ...) \
    log_write(LOG_LEVEL_WARN, (module), __VA_ARGS__)
#define LOG_INFO(module, ...) \
    log_write(LOG_LEVEL_INFO, (module), __VA_ARGS__)
#define LOG_DEBUG(module, ...) \
    log_write(LOG_LEVEL_DEBUG, (module), __VA_ARGS__)

#define LOG_ERROR_ERRNO(module, error_number, ...) \
    log_write_errno(LOG_LEVEL_ERROR, (module), (error_number), __VA_ARGS__)
#define LOG_WARN_ERRNO(module, error_number, ...) \
    log_write_errno(LOG_LEVEL_WARN, (module), (error_number), __VA_ARGS__)

#endif
