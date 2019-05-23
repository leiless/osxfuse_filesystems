/*
 * Created 190519 lynnl
 *
 * Clock FUSE filesystem implementation using low-level FUSE API
 *
 * Original authros:
 *  Miklos Szeredi <miklos@szeredi.hu>
 *  Benjamin Fleischer
 *
 * see:
 *  osxfuse/filesystems/filesystems-c/clock/clock_ll.c
 */

#define FUSE_USE_VERSION            26
#define _FILE_OFFSET_BITS           64

#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE    1
#endif

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>     /* usleep(3) */

#include <fuse_lowlevel.h>

#include "utils.h"

#define DATA_BUFSZ  64

static const char *file_name = "clock.txt";
static char file_data[DATA_BUFSZ];

static int clock_stat(fuse_ino_t ino, struct stat *stbuf)
{
    assert_nonnull(stbuf);

    stbuf->st_ino = ino;
    switch (ino) {
    case 1:
        stbuf->st_mode = S_IFDIR | 0755;    /* rwxr-xr-x */
        stbuf->st_nlink = 2;
        break;

    case 2:
        stbuf->st_mode = S_IFREG | 0444;    /* r--r--r-- */
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(file_data);
        break;

    default:
        return -1;
    }

    return 0;
}

static void clock_ll_lookup(
        fuse_req_t req,
        fuse_ino_t parent,
        const char *name)
{
    int e;
    struct fuse_entry_param param;

    assert_nonnull(req);
    assert_nonnull(name);

    SYSLOG_DBG("lookup()  parent: %#lx name: %s", parent, name);

    if (parent != 1 || strcmp(name, file_name) != 0) {
        e = fuse_reply_err(req, ENOENT);
        assert(e == 0);
        return;
    }

    (void) memset(&param, 0, sizeof(param));
    param.ino = 2;              /* see: clock_stat() */
    param.attr_timeout = 1.0;   /* in seconds */
    param.entry_timeout = 1.0;  /* in seconds */
    (void) clock_stat(param.ino, &param.attr);

    e = fuse_reply_entry(req, &param);
    assert(e == 0);
}

static void clock_ll_getattr(
        fuse_req_t req,
        fuse_ino_t ino,
        struct fuse_file_info *fi)
{
    int e;
    struct stat stbuf;

    assert_nonnull(req);
    assert(fi == NULL);

    SYSLOG_DBG("getattr()  ino: %#lx", ino);

    (void) memset(&stbuf, 0, sizeof(stbuf));

    if (clock_stat(ino, &stbuf) == 0) {
        e = fuse_reply_attr(req, &stbuf, 1.0);
        assert(e == 0);
    } else {
        e = fuse_reply_err(req, ENOENT);
        assert(e == 0);
    }
}

struct dirbuf {
    char *p;
    size_t size;
};

static void dirbuf_add(
        fuse_req_t req,
        struct dirbuf *b,
        const char *name,
        fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize;
    char *newp;

    assert_nonnull(req);
    assert_nonnull(b);
    assert_nonnull(name);

    oldsize = b->size;

    b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);

    newp = realloc(b->p, b->size);
    assert_nonnull(newp);
    b->p = newp;

    (void) memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;

    (void) fuse_add_direntry(req, b->p + oldsize, b->size - oldsize,
                                            name, &stbuf, b->size);
}

#ifndef MIN
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#endif

/**
 * @return  0       on success
 *          -errno  for failure to send reply
 */
static int reply_buf_limited(
        fuse_req_t req,
        const char *buf,
        size_t bufsize,
        off_t off,
        size_t maxsize)
{
    assert(off >= 0);

    if ((size_t) off < bufsize)
        return fuse_reply_buf(req, buf + off, MIN(bufsize - off, maxsize));

    return fuse_reply_buf(req, NULL, 0);
}

static void clock_ll_readdir(
        fuse_req_t req,
        fuse_ino_t ino,
        size_t size,
        off_t off,
        struct fuse_file_info *fi)
{
    int e;
    struct dirbuf b;

    assert_nonnull(req);
    assert_nonnull(fi);
    assert(off >= 0);

    SYSLOG_DBG("readdir()  ino: %#lx size: %zu off: %lld fi->flags: %#x",
                        ino, size, off, fi->flags);

    if (ino != 1) {     /* If not root directory */
        e = fuse_reply_err(req, ENOTDIR);
        assert(e == 0);
        return;
    }

    (void) memset(&b, 0, sizeof(b));

    dirbuf_add(req, &b, ".", 1);                /* Root directory */
    dirbuf_add(req, &b, "..", 1);               /* Parent of root is itself */
    dirbuf_add(req, &b, file_name, 2);      /* The only regular file */

    e = reply_buf_limited(req, b.p, b.size, off, size);
    assert(e == 0);

    free(b.p);
}

static void clock_ll_open(
        fuse_req_t req,
        fuse_ino_t ino,
        struct fuse_file_info *fi)
{
    int e;

    assert_nonnull(req);
    assert_nonnull(fi);

    SYSLOG_DBG("open()  ino: %#lx fi->flags: %#x", ino, fi->flags);

    if (ino != 2) {
        e = fuse_reply_err(req, EISDIR);
        assert(e == 0);
    } else if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        e = fuse_reply_err(req, EACCES);
        assert(e == 0);
    } else {
        e = fuse_reply_open(req, fi);
        assert(e == 0);
    }
}

static void clock_ll_read(
        fuse_req_t req,
        fuse_ino_t ino,
        size_t size,
        off_t off,
        struct fuse_file_info *fi)
{
    int e;

    assert_nonnull(req);
    assert_nonnull(fi);
    assert(off >= 0);

    SYSLOG_DBG("read()  ino: %#lx size: %zu off: %lld fi->flags: %#x",
                        ino, size, off, fi->flags);

    assert(ino == 2);
    e = reply_buf_limited(req, file_data, strlen(file_data), off, size);
    assert(e == 0);
}

static void fmt_datetime(char *str, size_t n)
{
    struct timeval tv;
    struct tm *t;

    assert_nonnull(str);
    assert(n > 0);

    (void) gettimeofday(&tv, NULL);     /* Won't fail */
    t = localtime(&tv.tv_sec);
    *str = '\0';

    if (t != NULL) {
        (void)
        snprintf(str, n, "%2d/%02d/%02d %02d:%02d:%02d.%03d%+05ld\n",
            (1900 + t->tm_year) % 100, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec,
            tv.tv_usec / 1000, t->tm_gmtoff * 100 / 3600);
    }
}

#define MSEC_PER_USEC   1000

static void *clock_update(void *arg)
{
    struct fuse_session *se;
    struct fuse_chan *ch;
    int e;

    assert_nonnull(arg);

    se = (struct fuse_session *) arg;
    ch = fuse_session_next_chan(se, NULL);

    while (!fuse_session_exited(se)) {
        fmt_datetime(file_data, sizeof(file_data));

        /*
         * fuse_lowlevel_notify_inval_inode() may return errno ENOTCONN (57)
         * it means fs's backing `struct fuse_ll' not yet initialized
         * case happens when function called sooner than fuse_session_loop()
         * see: osxfuse/fuse/lib/fuse_lowlevel.c#fuse_lowlevel_notify_inval_inode
         */
        e = fuse_lowlevel_notify_inval_inode(ch, 2, 0, 0);
        if (e != 0 && e != -ENOENT) {
            /*
             * inode 2(the only regular file) may not yet present in this fs
             * in such case fuse_lowlevel_notify_inval_inode() will return -ENOENT
             */
            LOG_ERROR("fuse_lowlevel_notify_inval_inode() fail  errno: %d", -e);
        }

        (void) usleep(250 * MSEC_PER_USEC);
    }

    pthread_exit(NULL);
}

static const struct fuse_lowlevel_ops clock_ll_ops = {
    .lookup = clock_ll_lookup,
    .getattr = clock_ll_getattr,
    .readdir = clock_ll_readdir,
    .open = clock_ll_open,
    .read = clock_ll_read,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    struct fuse_session *se;
    char *mountpoint = NULL;
    pthread_t clock_thread;
    int e;

    /* Setup syslog(3) */
    (void) setlogmask(LOG_UPTO(LOG_NOTICE));

    e = fuse_parse_cmdline(&args, &mountpoint, NULL, NULL);
    if (e == -1) {
        LOG_ERROR("fuse_parse_cmdline() fail");
        e = 1;
        goto out_fail;
    } else if (mountpoint == NULL) {
        if (argc == 1) LOG_ERROR("no mountpoint  -h for help");
        e = 2;
        goto out_args;
    }

    assert_nonnull(mountpoint);
    LOG("mountpoint: %s", mountpoint);

    ch = fuse_mount(mountpoint, &args);
    if (ch == NULL) {
        LOG_ERROR("fuse_mount() fail");
        e = 3;
        goto out_chan;
    }

    se = fuse_lowlevel_new(&args, &clock_ll_ops, sizeof(clock_ll_ops), NULL /* user data */);
    if (se == NULL) {
        LOG_ERROR("fuse_lowlevel_new() fail");
        e = 4;
        goto out_se;
    }

    if (fuse_set_signal_handlers(se) != -1) {
        fuse_session_add_chan(se, ch);

        LOG("Type `umount %s' in shell to stop this fs", mountpoint);

        if (pthread_create(&clock_thread, NULL, &clock_update, se) != 0) {
            LOG_ERROR("pthread_create(3) fail  errno: %d", errno);
            e = 5;
            goto out_pthread;
        }

        if (fuse_session_loop(se) == -1) {
            e = 6;
            LOG_ERROR("fuse_session_loop() fail");
        } else {
            LOG("session loop end  cleaning up...");
        }

        /* NOTE: is it's ok to call fuse_session_exit()? */
        fuse_session_exit(se);

        e = pthread_join(clock_thread, NULL);
        if (e != 0) {
            LOG_ERROR("pthread_join(3) fail  errno: %d", e);
            e = 8;
        }

out_pthread:
        fuse_remove_signal_handlers(se);
        fuse_session_remove_chan(ch);
    } else {
        e = 7;
        LOG_ERROR("fuse_set_signal_handlers() fail");
    }

    fuse_session_destroy(se);
out_se:
    fuse_unmount(mountpoint, ch);
out_chan:
    free(mountpoint);
out_args:
    fuse_opt_free_args(&args);
out_fail:
    return e;
}

