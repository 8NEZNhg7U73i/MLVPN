/* -*- mode: c; c-file-style: "openbsd" -*- */
/*  $OpenBSD: log.c,v 1.11 2007/12/07 17:17:00 reyk Exp $   */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* By default, logging is done on stderr. */
static int debug = 1;
static int level = 0;
static int log_opened = 0;

/* Logging can be modified by providing an appropriate log handler. */
static void (*logh)(int severity, const char *msg, void *) = NULL;
static void *logh_arg = NULL;

static void vlog(int, const char *, const char *, va_list);
static void logit(int, const char *, const char *, ...);

void log_warnx(const char *token, const char *emsg, ...);

#define MAX_DBG_TOKENS 40
static const char *tokens[MAX_DBG_TOKENS + 1] = {NULL};

void
log_init(int n_debug, int n_level, const char *progname)
{
	int is_root = getuid() == 0;
    debug = n_debug;
    level = n_level;

	if (log_opened && is_root)
	{
		log_info("log", "Closing syslog: debug=%d, level=%d", debug, level);
		closelog();
	}
	if (is_root) {
		log_info("log", "(Re)Opening syslog: debug=%d, level=%d", debug, level);
		openlog(progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		log_opened = 1;
	}
    tzset();
    log_info("log", "Loglevel config is now: debug=%d, level=%d", debug, level);
}

void
log_register(void (*cb)(int, const char*, void*), void *arg)
{
    logh = cb;
    logh_arg = arg;
}

void
log_clear_accept(void)
{
    tokens[0] = NULL;
}

void
log_accept(const char *token)
{
    int i;
    for (i = 0; i < MAX_DBG_TOKENS; i++) {
        if (tokens[i] == NULL) {
            tokens[i+1] = NULL;
            tokens[i] = token;
            return;
        }
    }
}

static void
logit(int pri, const char *token, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlog(pri, token, fmt, ap);
    va_end(ap);
}

static char *
date()
{
    /* Return the current date as incomplete ISO 8601 (2012-12-12T16:13:30) */
    static char date[] = "2012-12-12T16:13:30";
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", tmp);
    return date;
}

static const char *
translate(int fd, int priority)
{
    /* Translate a syslog priority to a string. With colors if the output is a terminal. */
    int tty = isatty(fd);
    switch (tty) {
    case 1:
        switch (priority) {
        case LOG_EMERG:   return "\033[1;37;41m[EMRG";
        case LOG_ALERT:   return "\033[1;37;41m[ALRT";
        case LOG_CRIT:    return "\033[1;37;41m[CRIT";
        case LOG_ERR:     return "\033[1;31m[ ERR";
        case LOG_WARNING: return "\033[1;33m[WARN";
        case LOG_NOTICE:  return "\033[1;34m[NOTI";
        case LOG_INFO:    return "\033[1;34m[INFO";
        case LOG_DEBUG:   return "\033[1;30m[ DBG";
        }
        break;
    default:
        switch (priority) {
        case LOG_EMERG:   return "[EMRG";
        case LOG_ALERT:   return "[ALRT";
        case LOG_CRIT:    return "[CRIT";
        case LOG_ERR:     return "[ ERR";
        case LOG_WARNING: return "[WARN";
        case LOG_NOTICE:  return "[NOTI";
        case LOG_INFO:    return "[INFO";
        case LOG_DEBUG:   return "[ DBG";
        }
    }
    return "[UNKN]";
}

static void
vlog(int pri, const char *token, const char *fmt, va_list ap)
{
    if (logh) {
        char *result;
        if (vasprintf(&result, fmt, ap) != -1) {
            logh(pri, result, logh_arg);
            free(result);
            return;
        }
        /* Otherwise, fallback to output on stderr. */
    }
    if (logh) {
        char *nfmt;
        /* best effort in out of mem situations */
        if (asprintf(&nfmt, "%s %s%s%s]%s %s\n",
            date(),
            translate(STDERR_FILENO, pri),
            token ? "/" : "", token ? token : "",
            isatty(STDERR_FILENO) ? "\033[0m" : "",
            fmt) == -1) {
            vfprintf(stderr, fmt, ap);
            fprintf(stderr, "\n");
        } else {
            vfprintf(stderr, nfmt, ap);
            free(nfmt);
        }
        fflush(stderr);
    } else
        vsyslog(pri, fmt, ap);
}


void
log_warn(const char *token, const char *emsg, ...)
{
    char    *nfmt;
    va_list  ap;

    /* best effort to even work in out of memory situations */
    if (emsg == NULL)
        logit(LOG_WARNING, "%s", strerror(errno));
    else {
        va_start(ap, emsg);

        if (asprintf(&nfmt, "%s: %s", emsg, strerror(errno)) == -1) {
            /* we tried it... */
            vlog(LOG_WARNING, token, emsg, ap);
            logit(LOG_WARNING, "%s", strerror(errno));
        } else {
            vlog(LOG_WARNING, token, nfmt, ap);
            free(nfmt);
        }
        va_end(ap);
    }
}

void
log_warnx(const char *token, const char *emsg, ...)
{
    va_list  ap;

    va_start(ap, emsg);
    vlog(LOG_WARNING, token, emsg, ap);
    va_end(ap);
}

void
log_crit(const char *token, const char *emsg, ...)
{
    va_list  ap;

    va_start(ap, emsg);
    vlog(LOG_CRIT, token, emsg, ap);
    va_end(ap);
}

void
log_info(const char *token, const char *emsg, ...)
{
    va_list  ap;

    if (level > 1 || logh) {
        va_start(ap, emsg);
        vlog(LOG_INFO, token, emsg, ap);
        va_end(ap);
    }
}

static int
log_debug_accept_token(const char *token)
{
    int i;
    if (token && tokens[0] != NULL) {
        for (i = 0;
             (i < MAX_DBG_TOKENS) && (tokens[i] != NULL);
             i++) {
            if (!strcmp(tokens[i], token))
                return 1;
        }
    }
    return 0;
}

void
log_debug(const char *token, const char *emsg, ...)
{
    va_list  ap;

    if ((level == 2 && log_debug_accept_token(token)) || logh || level > 2) {
        va_start(ap, emsg);
        vlog(LOG_DEBUG, token, emsg, ap);
        va_end(ap);
    }
}

void
fatal(const char *token, const char *emsg)
{
    if (emsg == NULL)
        logit(LOG_CRIT, token ? token : "fatal", "%s", strerror(errno));
    else
        if (errno)
            logit(LOG_CRIT, token ? token : "fatal", "%s: %s",
                emsg, strerror(errno));
        else
            logit(LOG_CRIT, token ? token : "fatal", "%s", emsg);

    exit(1);
}

void
fatalx(const char *emsg)
{
    errno = 0;
    fatal(NULL, emsg);
}
