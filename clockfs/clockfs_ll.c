/*
 * Created 190519 lynnl
 *
 * Hello FUSE filesystem implementation using low-level FUSE API
 *
 * see:
 *  osxfuse/filesystems/filesystems-c/hello/hello_ll.c
 *  libfuse/example/hello_ll.c
 */

#define FUSE_USE_VERSION    26
#define _FILE_OFFSET_BITS   64

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fuse_lowlevel.h>

#include "utils.h"

static const char *file_path = "/hello.txt";
static const char file_content[] = "Hello world!\n";
static const size_t file_size = STRLEN(file_content);

static int hello_stat(fuse_ino_t ino, struct stat *stbuf)
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
        stbuf->st_size = file_size;
        break;

    default:
        return -1;
    }

    return 0;
}

static void hello_ll_lookup(
        fuse_req_t req,
        fuse_ino_t parent,
        const char *name)
{
    int e;
    struct fuse_entry_param param;

    assert_nonnull(req);
    assert_nonnull(name);

    SYSLOG_DBG("lookup()  parent: %#lx name: %s", parent, name);

    if (parent != 1 || strcmp(name, file_path + 1) != 0) {
        e = fuse_reply_err(req, ENOENT);
        assert(e == 0);
        return;
    }

    (void) memset(&param, 0, sizeof(param));
    param.ino = 2;              /* see: hello_stat() */
    param.attr_timeout = 1.0;   /* in seconds */
    param.entry_timeout = 1.0;  /* in seconds */
    (void) hello_stat(param.ino, &param.attr);

    e = fuse_reply_entry(req, &param);
    assert(e == 0);
}

static void hello_ll_getattr(
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

    if (hello_stat(ino, &stbuf) == 0) {
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

static void hello_ll_readdir(
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
    dirbuf_add(req, &b, file_path + 1, 2);      /* The only regular file */

    e = reply_buf_limited(req, b.p, b.size, off, size);
    assert(e == 0);

    free(b.p);
}

static void hello_ll_open(
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

static void hello_ll_read(
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
    e = reply_buf_limited(req, file_content, file_size, off, size);
    assert(e == 0);
}

static const struct fuse_lowlevel_ops hello_ll_ops = {
    .lookup = hello_ll_lookup,
    .getattr = hello_ll_getattr,
    .readdir = hello_ll_readdir,
    .open = hello_ll_open,
    .read = hello_ll_read,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    struct fuse_session *se;
    char *mountpoint = NULL;
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

    se = fuse_lowlevel_new(&args, &hello_ll_ops, sizeof(hello_ll_ops), NULL /* user data */);
    if (se == NULL) {
        LOG_ERROR("fuse_lowlevel_new() fail");
        e = 4;
        goto out_se;
    }

    if (fuse_set_signal_handlers(se) != -1) {
        fuse_session_add_chan(se, ch);

        LOG("Type `umount %s' in shell to stop this fs", mountpoint);
        if (fuse_session_loop(se) == -1) {
            e = 5;
            LOG_ERROR("fuse_session_loop() fail");
        } else {
            LOG("session loop end  cleaning up...");
        }

        fuse_remove_signal_handlers(se);
        fuse_session_remove_chan(ch);
    } else {
        e = 6;
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

