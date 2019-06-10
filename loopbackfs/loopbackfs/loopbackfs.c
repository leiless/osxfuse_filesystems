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
#include <string.h>
#include <unistd.h>     /* readlink(2) */
#include <dirent.h>     /* DIR */
#include <errno.h>

#include <sys/stat.h>   /* umask(2) */
#include <sys/stat.h>   /* lstat(2) */
#include <sys/xattr.h>

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

/**
 * Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.     An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
static int lb_write(
        const char *path,
        const char *buf,
        size_t sz,
        off_t off,
        struct fuse_file_info *fi)
{
    ssize_t wr;

    assert_nonnull(path);
    assert(!!buf | !sz);    /* Fail if buf is NULL yet sz not zero */
    assert_nonnull(fi);

    wr = pwrite((int) fi->fh, buf, sz, off);
    return RET_TO_ERRNO(wr);
}

/**
 * Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
static int lb_statfs(const char *path, struct statvfs *st)
{
    assert_nonnull(path);
    assert_nonnull(st);

    return RET_TO_ERRNO(statvfs(path, st));
}

/**
 * Possibly flush cached data
 * see: struct fuse_operations.flush
 */
static int lb_flush(const char *path, struct fuse_file_info *fi)
{
    int fd;

    assert_nonnull(path);
    assert_nonnull(fi);

    fd = dup((int) fi->fh);
    if (fd < 0) return RET_TO_ERRNO(fd);
    return RET_TO_ERRNO(close(fd));
}

/**
 * Release an open file
 * The return value of release is ignored.
 */
static int lb_release(const char *path, struct fuse_file_info *fi)
{
    assert_nonnull(path);
    assert_nonnull(fi);

    return RET_TO_ERRNO(close((int) fi->fh));
}

/**
 * Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
static int lb_fsync(
        const char *path,
        int datasync,
        struct fuse_file_info *fi)
{
    assert_nonnull(path);
    assert_nonnull(fi);
    return RET_TO_ERRNO(fsync((int) fi->fh));
}

#define XATTR_APPLE_PREFIX          "com.apple."
#define A_KAUTH_FILESEC_XATTR       "com.apple.system.Security"
#define P_KAUTH_FILESEC_XATTR       "pseudo." A_KAUTH_FILESEC_XATTR

/**
 * Replace A_KAUTH_FILESEC_XATTR with P_KAUTH_FILESEC_XATTR
 */
static const char *map_xattr_name(const char *name)
{
    assert_nonnull(name);
    return strcmp(name, A_KAUTH_FILESEC_XATTR) ? name : P_KAUTH_FILESEC_XATTR;
}

/**
 * Set extended attributes
 */
static int lb_setxattr(
        const char *path,
        const char *name,
        const char *value,
        size_t size,
        int options,
        uint32_t position)
{
    int e;

    assert_nonnull(path);
    assert_nonnull(name);
    assert(!!value || !size);

    if (!strncmp(name, XATTR_APPLE_PREFIX, STRLEN(XATTR_APPLE_PREFIX))) {
        /*
         * The XATTR_NOSECURITY, XATTR_NODEFAULT flag implies a kernel request
         * Specify in user space will result in errno EINVAL
         *
         * see:
         *  xnu/bsd/vfs/vfs_syscalls.c#setxattr()
         *  xnu/bsd/vfs/vfs_xattr.c#vn_setxattr()
         */
        options &= ~(XATTR_NOSECURITY | XATTR_NODEFAULT);
    }

    e = setxattr(path, map_xattr_name(name), value, size, position, options);
    return RET_TO_ERRNO(e);
}

/**
 * Get extended attributes
 *
 * XXX: OSX FUSE getxattr callback doesn't provide options parameter
 *  thusly we use XATTR_NOFOLLOW as default options
 */
static int lb_getxattr(
        const char *path,
        const char *name,
        char *value,
        size_t size,
        uint32_t position)
{
    static int options = XATTR_NOFOLLOW;
    ssize_t sz;

    assert_nonnull(path);
    assert_nonnull(name);

    sz = getxattr(path, map_xattr_name(name), value, size, position, options);
    return RET_TO_ERRNO(sz);
}

/**
 * List extended attributes
 *
 * XXX: OSX FUSE listxattr callback doesn't provide options parameter
 *  thusly we use XATTR_NOFOLLOW as default options
 */
static int lb_listxattr(const char *path, char *namebuf, size_t size)
{
    static int options = XATTR_NOFOLLOW;
    ssize_t rd;

    assert_nonnull(path);

    rd = listxattr(path, namebuf, size, options);
    if (rd > 0) {
        if (namebuf != NULL) {
            size_t len = 0;
            char *curr = namebuf;
            size_t currlen;

            do {
                currlen = strlen(curr) + 1;

                /* Don't expose fake A_KAUTH_FILESEC_XATTR to user space */
                if (!strcmp(curr, P_KAUTH_FILESEC_XATTR)) {
                    (void) memmove(curr, curr + currlen, rd - len - currlen);
                    rd -= currlen;
                    break;
                }

                curr += currlen;
                len += currlen;
            } while (len < rd);
        } else {
#if 1
            /*
             * listxattr(2) don't have to return strict name buffer size
             *  since it's only a snapshot
             * if we don't check P_KAUTH_FILESEC_XATTR
             *  which we may overcommit return name buffer size(if it present)
             *  it's fine and we reduced a syscall call(getxattr)
             */
            ssize_t rd2 = getxattr(path, P_KAUTH_FILESEC_XATTR, NULL, 0, 0, options);
            if (rd2 >= 0) rd -= STRLEN(P_KAUTH_FILESEC_XATTR) + 1;
            assert(rd >= 0);
#endif
        }
    }

    return RET_TO_ERRNO(rd);
}

/**
 * Remove extended attributes
 *
 * XXX: OSX FUSE removexattr callback doesn't provide options parameter
 *  thusly we use XATTR_NOFOLLOW as default options
 */
static int lb_removexattr(const char *path, const char *name)
{
    static int options = XATTR_NOFOLLOW;
    int e;

    assert_nonnull(path);
    assert_nonnull(name);

    e = removexattr(path, map_xattr_name(name), options);
    return RET_TO_ERRNO(e);
}

struct loopback_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

/**
 * Open directory
 * File handle will be passed to readdir, closedir and fsyncdir
 */
static int lb_opendir(const char *path, struct fuse_file_info *fi)
{
    struct loopback_dirp *d;

    assert_nonnull(path);
    assert_nonnull(fi);

    d = malloc(sizeof(*d));
    if (d == NULL) return -ENOMEM;

    d->dp = opendir(path);
    if (d->dp == NULL) {
        free(d);
        return -errno;
    }

    d->entry = NULL;
    d->offset = 0;

    fi->fh = (uint64_t) d;
    return 0;
}

static inline struct loopback_dirp *get_dirp(struct fuse_file_info *fi)
{
    assert_nonnull(fi);
    return (struct loopback_dirp *) fi->fh;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
static int lb_releasedir(const char *path, struct fuse_file_info *fi)
{
    int e;
    struct loopback_dirp *d;

    assert_nonnull(path);
    assert_nonnull(fi);

    d = get_dirp(fi);

    assert_nonnull(d->dp);
    e = closedir(d->dp);
    free(d);

    return RET_TO_ERRNO(e);
}

/**
 * Initialize filesystem
 */
static void *lb_init(struct fuse_conn_info *conn)
{
    assert_nonnull(conn);

    FUSE_ENABLE_SETVOLNAME(conn);
    FUSE_ENABLE_XTIMES(conn);

    if (loopbackfs_cfg.ci) {
        FUSE_ENABLE_CASE_INSENSITIVE(conn);
    }

    return NULL;
}

/**
 * Clean up filesystem
 * Called on filesystem exit.
 */
static void lb_destroy(void *userdata)
{
    UNUSED(userdata);
}

/**
 * Create and open a file
 */
static int lb_create(
        const char *path,
        mode_t mode,
        struct fuse_file_info *fi)
{
    int fd;

    assert_nonnull(path);
    assert_nonnull(fi);

    fd = open(path, fi->flags, mode);
    if (fd < 0) return -errno;

    fi->fh = fd;
    return 0;
}

/**
 * Change the size of an open file
 */
static int lb_ftruncate(
        const char *path,
        off_t off,
        struct fuse_file_info *fi)
{
    assert_nonnull(path);
    assert_nonnull(fi);

    return RET_TO_ERRNO(ftruncate((int) fi->fh, off));
}

/**
 * Get attributes from an open file
 */
static int lb_fgetattr(
        const char *path,
        struct stat *st,
        struct fuse_file_info *fi)
{
    int e;

    assert_nonnull(path);
    assert_nonnull(st);
    assert_nonnull(fi);

    e = fstat((int) fi->fh, st);
    if (e == 0) {
#if FUSE_VERSION >= 29
        /* Fall back to global IO size  see: lb_getattr() */
        st->st_blksize = 0;
#endif
    }

    return RET_TO_ERRNO(e);
}

/**
 * Change the access and modification times of a file with nanosecond resolution
 */
static int lb_utimens(const char *path, const struct timespec tv[2])
{
    assert_nonnull(path);
    assert_nonnull(tv);
    /* Flag is zero, will follow symlink */
    return RET_TO_ERRNO(utimensat(AT_FDCWD, path, tv, 0));
}

static int lb_statfs_x(const char *path, struct statfs *st)
{
    assert_nonnull(path);
    assert_nonnull(st);
    return RET_TO_ERRNO(statfs(path, st));
}

static int lb_setvolname(const char *volname)
{
    assert_nonnull(volname);
    return 0;
}

static int lb_exchange(
        const char *path1,
        const char *path2,
        unsigned long options)
{
    assert_nonnull(path1);
    assert_nonnull(path2);
    /* NOTE: warn if options & ~0xffffffffUL */
    return RET_TO_ERRNO(exchangedata(path1, path2, (unsigned int) options));
}

static int lb_chflags(const char *path, uint32_t flags)
{
    assert_nonnull(path);
    return RET_TO_ERRNO(chflags(path, flags));
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
    .write = lb_write,
    .statfs = lb_statfs,
    .flush = lb_flush,
    .release = lb_release,
    .fsync = lb_fsync,

    .setxattr = lb_setxattr,
    .getxattr = lb_getxattr,
    .listxattr = lb_listxattr,
    .removexattr = lb_removexattr,

    .opendir = lb_opendir,
    /* readdir */
    .releasedir = lb_releasedir,

    /* TODO: fsyncdir */

    .init = lb_init,
    .destroy = lb_destroy,
    /* TODO: access */
    .create = lb_create,
    .ftruncate = lb_ftruncate,
    .fgetattr = lb_fgetattr,
    /* TODO: lock */
    .utimens = lb_utimens,
    /* TODO: bmap/ioctl/poll/write_buf/read_buf/flock/fallocate */

    .statfs_x = lb_statfs_x,
    .setvolname = lb_setvolname,
    .exchange = lb_exchange,
    /* TODO: getxtimes/setbkuptime/setchgtime/setcrtime */
    .chflags = lb_chflags,
    /* TODO: setattr_x/fsetattr_x */
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

