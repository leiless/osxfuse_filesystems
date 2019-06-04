/*
 * Created 190604 lynnl
 *
 * Xcode settings:
 *  1) Other Linker Flags:      -losxfuse
 *  2) Header Search Paths:     /usr/local/include/osxfuse/fuse
 *  3) Library Search Paths:    /usr/local/lib
 *
 * Original authors:
 *  Amit Singh
 *  Miklos Szeredi <miklos@szeredi.hu>
 *
 * see:
 *  osxfuse/filesystems/filesystems-c/loopback/loopback.c
 */

#define FUSE_USE_VERSION            26
#define _FILE_OFFSET_BITS           64

#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE    1
#endif

#include <stdio.h>
#include <stddef.h>     /* offsetof() */
#include <stdlib.h>
#include <unistd.h>     /* readlink(2) */
#include <errno.h>

#include <sys/stat.h>   /* umask(2) */
#include <sys/stat.h>   /* lstat(2) */

#define FUSE_USE_VERSION    26
#include <fuse.h>

struct loopbackfs_config {
    int ci;     /* Case insensitive? */
};

static struct loopbackfs_config loopbackfs_cfg; /* Zeroed out */

/*
 * Loopback fs implementations
 *
 * NOTE: All following functions return 0 on success  -errno on failure
 */

#define RET_TO_ERRNO(e)     (e ? -errno : 0)

/**
 * Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are ignored.
 * The 'st_ino' field is ignored except if the 'use_ino' mount option is given.
 */
static int lb_getattr(const char *st, struct stat *stbuf)
{
    int e;

    e = lstat(st, stbuf);
    if (e == 0) {
#if FUSE_VERSION >= 29
        /*
         * [sic]
         * The optimal I/O size can be set on a per-file basis.
         * Setting st_blksize to zero will cause the kernel extension to
         *  fall back on the global I/O size
         *  which can be specified at mount-time (option iosize).
         */
        stbuf->st_blksize = 0;
#endif
    }

    return RET_TO_ERRNO(e);
}

/**
 * Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.
 * The buffer size argument includes the space for the terminating
 * null character.
 *
 * If the linkname is too long to fit in the buffer, it should be truncated.
 * The return value should be 0 for success.
 */
static int lb_readlink(const char *path, char *buf, size_t sz)
{
    ssize_t rd;
    /* TODO: assert sz > 0 */
    rd = readlink(path, buf, sz-1);
    if (rd >= 0) buf[sz] = '\0';
    return RET_TO_ERRNO(rd);
}

static struct fuse_operations loopback_op = {
    .getattr = lb_getattr,
    .readlink = lb_readlink,

    /* Deprecated, use readdir() instead */
    .getdir = NULL,

    /* TODO: more */
};

static struct fuse_opt loopback_opts[] = {
    {"case-insensitive", offsetof(struct loopbackfs_config, ci), 1},
    FUSE_OPT_END,
};

int main(int argc, char *argv[])
{
    int e;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &loopbackfs_cfg, loopback_opts, NULL) < 0) {
        exit(1);
    }

    umask(0);
    e = fuse_main(args.argc, args.argv, &loopback_op, NULL);

    fuse_opt_free_args(&args);
    return 0;
}

