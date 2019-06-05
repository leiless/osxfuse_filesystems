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

#define FUSE_USE_VERSION            26
#include <fuse.h>

#include "utils.h"

struct loopbackfs_config {
    int ci;     /* Case insensitive? */
};

static struct loopbackfs_config loopbackfs_cfg; /* Zeroed out */

/*
 * Loopback fs implementations
 *
 * NOTE: All following functions return 0 on success  -errno on failure
 */

#define RET_TO_ERRNO(e)     ((e) ? -errno : 0)

/**
 * Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are ignored.
 * The 'st_ino' field is ignored except if the 'use_ino' mount option is given.
 */
static int lb_getattr(const char *st, struct stat *stbuf)
{
    int e;

    assert_nonnull(st);
    assert_nonnull(stbuf);

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
    ssize_t n;

    assert_nonnull(path);
    assert_nonnull(buf);
    assert(sz > 0);

    n = readlink(path, buf, sz-1);
    if (n >= 0) buf[sz] = '\0';

    return RET_TO_ERRNO(n);
}

/**
 * Create a file node
 *
 * This is called for creation of all non-directory, non-symlink nodes.
 * If the filesystem defines a create() method,
 *  then for regular files that will be called instead.
 */
static int lb_mknod(const char *path, mode_t mode, dev_t dev)
{
    int e;

    assert_nonnull(path);

    if (S_ISFIFO(mode)) {
        e = mkfifo(path, mode);
    } else {
        e = mknod(path, mode, dev);
    }

    return RET_TO_ERRNO(e);
}

/**
 * Create a directory
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits use  mode|S_IFDIR
 * */
static int lb_mkdir(const char *path, mode_t mode)
{
    int e;

    assert_nonnull(path);

    if (!(mode | S_IFDIR)) {
        SYSLOG_WARN("mkdir()  mode %#x without type spec.", mode);
    }

    e = mkdir(path, mode | S_IFDIR);

    return RET_TO_ERRNO(e);
}

/**
 * Remove a file
 */
static int lb_unlink(const char *path)
{
    assert_nonnull(path);
    return RET_TO_ERRNO(unlink(path));
}

/** Remove a directory */
static int lb_rmdir(const char *path)
{
    assert_nonnull(path);
    return RET_TO_ERRNO(rmdir(path));
}

/**
 * Create a symbolic link       lnk -> dst
 */
static int lb_symlink(const char *dst, const char *lnk)
{
    assert_nonnull(dst);
    assert_nonnull(lnk);
    return RET_TO_ERRNO(symlink(dst, lnk));
}

/**
 * Rename a file
 */
static int lb_rename(const char *old, const char *new)
{
    assert_nonnull(old);
    assert_nonnull(new);
    return RET_TO_ERRNO(rename(old, new));
}

/**
 * Create a hard link to a file     lnk -> dst
 */
static int lb_link(const char *dst, const char *lnk)
{
    assert_nonnull(dst);
    assert_nonnull(lnk);
    return RET_TO_ERRNO(link(dst, lnk));
}

/**
 * Change the permission bits of a file
 */
static int lb_chmod(const char *path, mode_t mode)
{
    assert_nonnull(path);
    return chmod(path, mode);
}

/**
 * Change the owner and group of a file
 */
static int lb_chown(const char *path, uid_t owner, gid_t group)
{
    assert_nonnull(path);
    return RET_TO_ERRNO(chown(path, owner, group));
}

/**
 * Change the size of a file
 */
static int lb_truncate(const char *path, off_t len)
{
    assert_nonnull(path);
    /* Don't assert(len >= 0)  truncate(2) will return EINVAL if it's negative */
    return RET_TO_ERRNO(truncate(path, len));
}

/**
 * File open operation
 *
 * No creation (O_CREAT, O_EXCL) and by default also no
 * truncation (O_TRUNC) flags will be passed to open(). If an
 * application specifies O_TRUNC, fuse first calls truncate()
 * and then open(). Only if 'atomic_o_trunc' has been
 * specified and kernel version is 2.6.24 or later, O_TRUNC is
 * passed on to open.
 *
 * Unless the 'default_permissions' mount option is given,
 * open should check if the operation is permitted for the
 * given flags. Optionally open may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to all file operations.
 *
 * Changed in version 2.2
 */
static int lb_open(const char *path, struct fuse_file_info *fi)
{
    int fd;

    assert_nonnull(path);
    assert_nonnull(fi);

    fd = open(path, fi->flags);
    if (fd >= 0) fi->fh = fd;

    return RET_TO_ERRNO(fd);
}

/**
 * Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.     An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
static int lb_read(
        const char *path,
        char *buf,
        size_t sz,
        off_t off,
        struct fuse_file_info *fi)
{
    ssize_t n;

    assert_nonnull(path);
    assert(!!buf | !sz);    /* Fail if buf is NULL yet sz not zero */
    /* Don't assert(off >= 0)  pread(2) will return EINVAL if it's negative */
    assert_nonnull(fi);

    n = pread((int) fi->fh, buf, sz, off);
    return RET_TO_ERRNO(n);
}

static struct fuse_operations loopback_op = {
    .getattr = lb_getattr,
    .readlink = lb_readlink,

    /* Deprecated, use readdir() instead */
    .getdir = NULL,

    .mknod = lb_mknod,
    .mkdir = lb_mkdir,
    .unlink = lb_unlink,
    .rmdir = lb_rmdir,
    .symlink = lb_symlink,
    .rename = lb_rename,
    .link = lb_link,
    .chmod = lb_chmod,
    .chown = lb_chown,
    .truncate = lb_truncate,

    /**
     * Change the access and/or modification times of a file
     * Deprecated, use utimens() instead.
     */
    .utime = NULL,

    .open = lb_open,
    .read = lb_read,

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

