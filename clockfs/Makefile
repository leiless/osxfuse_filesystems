#
# Makefile for hello fs
#

CC ?= gcc

CPPFLAGS += -D__TS__=\"$(shell date +'%Y/%m/%d\ %H:%M:%S%z')\"

CPPFLAGS += -DFUSE_USE_VERSION=26
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CPPFLAGS += -D_DARWIN_USE_64_BIT_INODE

CFLAGS += -std=c99 -Wall -Wextra
CFLAGS += -arch i386
CFLAGS += -arch x86_64
CFLAGS += -mmacosx-version-min=10.4

# Root for FUSE for macOS includes and libraries
FUSE_ROOT ?= /usr/local
#FUSE_ROOT ?= /opt/local
INCLUDE_DIR ?= $(FUSE_ROOT)/include/osxfuse/fuse
LIBRARY_DIR ?= $(FUSE_ROOT)/lib

CFLAGS += -I$(INCLUDE_DIR) -L$(LIBRARY_DIR)

LIBS += -losxfuse

EXEC := hello_fs hello_fs-debug hello_fs_ll hello_fs_ll-debug

all: debug

release: hello_fs hello_fs_ll
debug: hello_fs-debug hello_fs_ll-debug

hello_fs: CFLAGS += -Os
hello_fs: hello_fs.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

hello_fs_ll: CFLAGS += -Os
hello_fs_ll: hello_fs_ll.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

hello_fs-debug: CPPFLAGS += -g -DDEBUG
hello_fs-debug: CFLAGS += -O0
hello_fs-debug: hello_fs.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

hello_fs_ll-debug: CPPFLAGS += -g -DDEBUG
hello_fs_ll-debug: CFLAGS += -O0
hello_fs_ll-debug: hello_fs_ll.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

clean:
	rm -rf *.o *.dSYM $(EXEC)

.PHONY: all clean hello_fs hello_fs-debug

