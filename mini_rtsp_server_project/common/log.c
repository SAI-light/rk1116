/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: log.c
 * Description: Small thread-safe logger for the embedded media server.
 ********************************************************************************/

#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOG_MESSAGE_MAX 1024

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static LogLevel g_log_level = LOG_LEVEL_INFO;

static int text_equal_ignore_case(const char *left,
                                  const char *right)
{
    if (left == NULL || right == NULL)
        return 0;

    while (*left != '\0' && *right != '\0')
    {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right))
        {
            return 0;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

void log_set_level(LogLevel level)
{
    if (level < LOG_LEVEL_ERROR)
        level = LOG_LEVEL_ERROR;
    else if (level > LOG_LEVEL_DEBUG)
        level = LOG_LEVEL_DEBUG;

    pthread_mutex_lock(&g_log_lock);
    g_log_level = level;
    pthread_mutex_unlock(&g_log_lock);
}

LogLevel log_get_level(void)
{
    LogLevel level;

    pthread_mutex_lock(&g_log_lock);
    level = g_log_level;
    pthread_mutex_unlock(&g_log_lock);

    return level;
}

const char *log_level_name(LogLevel level)
{
    switch (level)
    {
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

int log_level_parse(const char *text,
                    LogLevel *level)
{
    if (text == NULL || level == NULL)
        return -1;

    if (text_equal_ignore_case(text, "error"))
        *level = LOG_LEVEL_ERROR;
    else if (text_equal_ignore_case(text, "warn") ||
             text_equal_ignore_case(text, "warning"))
        *level = LOG_LEVEL_WARN;
    else if (text_equal_ignore_case(text, "info"))
        *level = LOG_LEVEL_INFO;
    else if (text_equal_ignore_case(text, "debug"))
        *level = LOG_LEVEL_DEBUG;
    else
        return -1;

    return 0;
}

static void write_prefix(FILE *stream,
                         LogLevel level,
                         const char *module)
{
    struct timespec ts;
    struct tm tm_value;
    char time_text[32];

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0 &&
        localtime_r(&ts.tv_sec, &tm_value) != NULL &&
        strftime(time_text,
                 sizeof(time_text),
                 "%Y-%m-%d %H:%M:%S",
                 &tm_value) > 0U)
    {
        fprintf(stream,
                "%s.%03ld [%s] [%s] ",
                time_text,
                ts.tv_nsec / 1000000L,
                log_level_name(level),
                module != NULL ? module : "app");
    }
    else
    {
        fprintf(stream,
                "[%s] [%s] ",
                log_level_name(level),
                module != NULL ? module : "app");
    }
}

static void log_vwrite(LogLevel level,
                       const char *module,
                       const char *format,
                       va_list args,
                       int append_errno,
                       int error_number)
{
    char message[LOG_MESSAGE_MAX];
    FILE *stream;
    int saved_errno = errno;
    int length;

    if (level > log_get_level())
        return;

    length = vsnprintf(message, sizeof(message), format, args);
    if (length < 0)
        return;

    message[sizeof(message) - 1U] = '\0';
    stream = level <= LOG_LEVEL_WARN ? stderr : stdout;

    pthread_mutex_lock(&g_log_lock);
    write_prefix(stream, level, module);
    fputs(message, stream);

    if (append_errno)
    {
        fprintf(stream,
                ": %s (errno=%d)",
                strerror(error_number),
                error_number);
    }

    fputc('\n', stream);
    fflush(stream);
    pthread_mutex_unlock(&g_log_lock);

    errno = saved_errno;
}

void log_write(LogLevel level,
               const char *module,
               const char *format,
               ...)
{
    va_list args;

    va_start(args, format);
    log_vwrite(level, module, format, args, 0, 0);
    va_end(args);
}

void log_write_errno(LogLevel level,
                     const char *module,
                     int error_number,
                     const char *format,
                     ...)
{
    va_list args;

    va_start(args, format);
    log_vwrite(level,
               module,
               format,
               args,
               1,
               error_number);
    va_end(args);
}
