/*
 * Created 190604 lynnl
 *
 * Xcode settings:
 *  1) Other Linker Flags:      -losxfuse
 *  2) Header Search Paths:     /usr/local/include/osxfuse/fuse
 *  3) Library Search Paths:    /usr/local/lib
 */

#define FUSE_USE_VERSION            26
#define _FILE_OFFSET_BITS           64

#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE    1
#endif

#include <stdio.h>
#include <stddef.h>     /* offsetof() */
#include <stdlib.h>

#include <sys/stat.h>   /* umask(2) */

#define FUSE_USE_VERSION    26
#include <fuse.h>

struct loopbackfs_config {
    int ci;     /* Case insensitive? */
};

static struct loopbackfs_config loopbackfs_cfg; /* Zeroed out */

static struct fuse_operations loopback_op = {
    /* TODO */
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

