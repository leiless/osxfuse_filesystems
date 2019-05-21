/*
 * Created 190521 lynnl
 */

#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <syslog.h>

/**
 * Should only used for `char[]'  NOT `char *'
 * Assume ends with null byte('\0')
 */
#define STRLEN(s)           (sizeof(s) - 1)

#define FSNAME              "hello_fs"

#define LOG(fmt, ...)       \
    (void) printf(FSNAME ": " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
    (void) fprintf(stderr, FSNAME ": [ERR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)   LOG("[DBG] " fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG("[WARN] " fmt, ##__VA_ARGS__)

/**
 * Do NOT use LOG_EMERG level  it'll broadcast to all users
 * macOS 10.13+ LOG_INFO, LOG_DEBUG levels rejected(log nothing)
 */
#define SYSLOG(fmt, ...)        \
    syslog(LOG_NOTICE, fmt "\n", ##__VA_ARGS__)
#define SYSLOG_ERR(fmt, ...)    \
    syslog(LOG_ERR, "[ERR] " fmt "\n", ##__VA_ARGS__)
#define SYSLOG_DBG(fmt, ...)    \
    syslog(LOG_NOTICE, "[DBG] " fmt "\n", ##__VA_ARGS__)
#define SYSLOG_WARN(fmt, ...)   \
    syslog(LOG_WARNING, "[WARN] " fmt "\n", ##__VA_ARGS__)

#define UNUSED(arg, ...)    (void) ((void) (arg), ##__VA_ARGS__)

#define assert_nonnull(p)   assert((p) != NULL)

#endif /* FS_UTILS_H */

