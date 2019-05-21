#
# Makefile for clockfs
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

EXEC := clockfs clockfs_ll

all: clockfs clockfs_ll

clockfs: CPPFLAGS += -g -DDEBUG
clockfs: CFLAGS += -O0
clockfs: clockfs.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

clockfs_ll: CPPFLAGS += -g -DDEBUG
clockfs_ll: CFLAGS += -O0
clockfs_ll: clockfs_ll.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) $< -o $@

clean:
	rm -rf *.o *.dSYM $(EXEC)

.PHONY: all clean clockfs clockfs_ll

