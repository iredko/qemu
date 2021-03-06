/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

struct Error
{
    char *msg;
    ErrorClass err_class;
    const char *src, *func;
    int line;
};

Error *error_abort;

static void error_do_abort(Error *err)
{
    fprintf(stderr, "Unexpected error in %s() at %s:%d:\n",
            err->func, err->src, err->line);
    error_report_err(err);
    abort();
}

static void error_setv(Error **errp,
                       const char *src, int line, const char *func,
                       ErrorClass err_class, const char *fmt, va_list ap)
{
    Error *err;
    int saved_errno = errno;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));
    err->msg = g_strdup_vprintf(fmt, ap);
    err->err_class = err_class;
    err->src = src;
    err->line = line;
    err->func = func;

    if (errp == &error_abort) {
        error_do_abort(err);
    }

    *errp = err;

    errno = saved_errno;
}

void error_set_internal(Error **errp,
                        const char *src, int line, const char *func,
                        ErrorClass err_class, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_setv(errp, src, line, func, err_class, fmt, ap);
    va_end(ap);
}

void error_setg_internal(Error **errp,
                         const char *src, int line, const char *func,
                         const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_setv(errp, src, line, func, ERROR_CLASS_GENERIC_ERROR, fmt, ap);
    va_end(ap);
}

void error_setg_errno_internal(Error **errp,
                               const char *src, int line, const char *func,
                               int os_errno, const char *fmt, ...)
{
    va_list ap;
    char *msg;
    int saved_errno = errno;

    if (errp == NULL) {
        return;
    }

    va_start(ap, fmt);
    error_setv(errp, src, line, func, ERROR_CLASS_GENERIC_ERROR, fmt, ap);
    va_end(ap);

    if (os_errno != 0) {
        msg = (*errp)->msg;
        (*errp)->msg = g_strdup_printf("%s: %s", msg, strerror(os_errno));
        g_free(msg);
    }

    errno = saved_errno;
}

void error_setg_file_open_internal(Error **errp,
                                   const char *src, int line, const char *func,
                                   int os_errno, const char *filename)
{
    error_setg_errno_internal(errp, src, line, func, os_errno,
                              "Could not open '%s'", filename);
}

#ifdef _WIN32

void error_setg_win32_internal(Error **errp,
                               const char *src, int line, const char *func,
                               int win32_err, const char *fmt, ...)
{
    va_list ap;
    char *msg1, *msg2;

    if (errp == NULL) {
        return;
    }

    va_start(ap, fmt);
    error_setv(errp, src, line, func, ERROR_CLASS_GENERIC_ERROR, fmt, ap);
    va_end(ap);

    if (win32_err != 0) {
        msg1 = (*errp)->msg;
        msg2 = g_win32_error_message(win32_err);
        (*errp)->msg = g_strdup_printf("%s: %s (error: %x)", msg1, msg2,
                                       (unsigned)win32_err);
        g_free(msg2);
        g_free(msg1);
    }
}

#endif

Error *error_copy(const Error *err)
{
    Error *err_new;

    err_new = g_malloc0(sizeof(*err));
    err_new->msg = g_strdup(err->msg);
    err_new->err_class = err->err_class;

    return err_new;
}

ErrorClass error_get_class(const Error *err)
{
    return err->err_class;
}

const char *error_get_pretty(Error *err)
{
    return err->msg;
}

void error_report_err(Error *err)
{
    error_report("%s", error_get_pretty(err));
    error_free(err);
}

void error_free(Error *err)
{
    if (err) {
        g_free(err->msg);
        g_free(err);
    }
}

void error_propagate(Error **dst_errp, Error *local_err)
{
    if (local_err && dst_errp == &error_abort) {
        error_do_abort(local_err);
    } else if (dst_errp && !*dst_errp) {
        *dst_errp = local_err;
    } else if (local_err) {
        error_free(local_err);
    }
}
