#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "random.h"

static int urandom_fd = -1;

static const char random_path[] = "/dev/urandom";

void
random_init(void)
{
    if (urandom_fd >= 0) return;

    urandom_fd = open(random_path, O_RDONLY, 0);
    if (urandom_fd < 0) {
        fprintf(stderr, "random_init: open '%s' failed: %s\n",
                random_path, strerror(errno));
        exit(1);
    }
}

void
random_bytes(unsigned char *data, size_t size)
{
    assert((ssize_t) size > 0);

    if (urandom_fd < 0) random_init();

    unsigned char *ptr = data;
    ssize_t sz = size;
    while (sz > 0) {
        ssize_t r = read(urandom_fd, ptr, sz);
        if (r < 0) {
            fprintf(stderr, "random_bytes: read error: %s\n",
                    strerror(errno));
            exit(1);
        }
        if (!r) {
            fprintf(stderr, "random_bytes: EOF on random number source\n");
            exit(1);
        }
        ptr += r;
        sz -= r;
    }
}

