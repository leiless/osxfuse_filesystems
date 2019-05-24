/*
 * Created 190514 lynnl
 *
 * Hello FUSE filesystem implementation using high-level FUSE API
 *
 * see:
 *  osxfuse/filesystems/filesystems-c/hello/hello.c
 *  libfuse/example/hello.c
 */

#define FUSE_USE_VERSION            26
#define _FILE_OFFSET_BITS           64

#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE    1
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>     /* usleep(3) */

#include <fuse.h>
#include <fuse_lowlevel.h>

#include "utils.h"

#define DATA_BUFSZ  64

static const char *file_path = "/clock.txt";
static char file_data[DATA_BUFSZ];

static int clock_getattr(const char *path, struct stat *stbuf)
{
    assert_nonnull(path);
    assert_nonnull(stbuf);

    SYSLOG_DBG("getattr()  path: %s", path);

    if (strcmp(path, "/") == 0) {
        (void) memset(stbuf, 0, sizeof(*stbuf));

        /* Root directory of clockfs */
        stbuf->st_mode = S_IFDIR | 0755;    /* rwxr-xr-x */
        stbuf->st_nlink = 2;
    } else if (strcmp(path, file_path) == 0) {
        (void) memset(stbuf, 0, sizeof(*stbuf));

        stbuf->st_mode = S_IFREG | 0444;    /* r--r--r-- */
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(file_data);
    } else {
        /* No need to memset `stbuf' since it met an error */

        return -ENOENT;     /* Reject everything else */
    }

    return 0;
}

static int clock_open(const char *path, struct fuse_file_info *fi)
{
    assert_nonnull(path);
    assert_nonnull(fi);

    SYSLOG_DBG("open()  path: %s fi->flags: %#x", path, fi->flags);

    if (strcmp(path, file_path) != 0) {
        return -ENOENT;     /* We have only one regular file */
    }

    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;     /* Only O_RDONLY access mode is allowed */
    }

    return 0;
}

static int clock_read(
        const char *path,
        char *buf,
        size_t sz,
        off_t off,
        struct fuse_file_info *fi)
{
    size_t file_size;

    assert_nonnull(path);
    assert_nonnull(buf);
    assert(off >= 0);
    assert_nonnull(fi);

    SYSLOG_DBG("read()  path: %s size: %zu off: %lld fi->flags: %#x", path, sz, off, fi->flags);

    if (strcmp(path, file_path) != 0) {
        return -ENOENT;
    }

    file_size = strlen(file_data);

    /* Trying to read past EOF of file_path */
    if ((size_t) off >= file_size) {
        SYSLOG_WARN("Read past EOF?!  off: %lld size: %zu", off, sz);
        return 0;
    }

    if (off + sz > file_size)
        sz = file_size - off;   /* Trim the read to the file size */

    (void) memcpy(buf, file_data + off, sz);

    return (int) sz;
}

static int clock_readdir(
        const char *path,
        void *buf,
        fuse_fill_dir_t filler,
        off_t off,
        struct fuse_file_info *fi)
{
    assert_nonnull(path);
    assert_nonnull(buf);
    assert_nonnull(filler);
    assert(off >= 0);
    assert_nonnull(fi);

    SYSLOG_DBG("readdir()  path: %s off: %lld fi->flags: %#x", path, off, fi->flags);

    /* clockfs have only one directory(e.g. the root directory) */
    if (strcmp(path, "/") != 0) return -ENOENT;

    (void) filler(buf, ".", NULL, 0);           /* Current directory */
    (void) filler(buf, "..", NULL, 0);          /* Parent directory */
    (void) filler(buf, file_path + 1, NULL, 0); /* The only one regular file */

    return 0;
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

    SYSLOG("Clock update thread is up");

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
            SYSLOG_ERR("fuse_lowlevel_notify_inval_inode() fail  errno: %d", -e);
        }

        (void) usleep(250 * MSEC_PER_USEC);
    }

    SYSLOG("Clock update thread going to die...");
    pthread_exit(NULL);
}

static struct fuse_operations clockfs_ops = {
    .getattr = clock_getattr,
    .open = clock_open,
    .read = clock_read,
    .readdir = clock_readdir,
};

/**
 * see:
 *  osxfuse/fuse/lib/helper.c#fuse_main_real()
 *  osxfuse/fuse/lib/helper.c#fuse_main_common()
 *  osxfuse/fuse/lib/helper.c#fuse_setup()
 */
int main(int argc, char *argv[])
{
    int e;
    struct fuse *fuse;
    char *mp;
    int mt;
    struct fuse_session *se;
    pthread_t clock_thread;

    /* Setup syslog(3) */
    (void) setlogmask(LOG_UPTO(LOG_NOTICE));

    /* fuse_setup(3) will do fork(2) after successfully returned */
    fuse = fuse_setup(argc, argv, &clockfs_ops, sizeof(clockfs_ops), &mp, &mt, NULL);
    if (fuse == NULL) {
        LOG_ERROR("fuse_setup(3) fail");
        e = 1;
        goto out_setup;
    }

    se = fuse_get_session(fuse);
    assert_nonnull(se);

    if (pthread_create(&clock_thread, NULL, &clock_update, se) != 0) {
        SYSLOG_ERR("pthread_create(3) fail  errno: %d", errno);
        e = 2;
        goto out_thread;
    }

    /* TODO: call fuse_session_add_chan() */

    e = (mt ? fuse_loop_mt : fuse_loop)(fuse);
    if (e != 0) {
        SYSLOG_ERR("fuse_loop*(3) fail");
        e = 3;
    }

    /* NOTE: is it's ok to call fuse_session_exit(3)? */
    fuse_session_exit(se);

    e = pthread_join(clock_thread, NULL);
    if (e != 0) {
        SYSLOG_ERR("pthread_join(3) fail  errno: %d", e);
        e = 4;
    }

    /* TODO: call fuse_session_remove_chan() */

out_thread:
    fuse_teardown(fuse, mp);
out_setup:
    return e;
}

