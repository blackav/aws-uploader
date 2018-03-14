#include "extract_file.h"
#include "random.h"
#include "base32.h"

#include <sys/sendfile.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>

enum { MAX_COPY_CHUNK = 1 * 1024 * 1024 * 1024 };

int
extract_file_fd(
        int dstfd,
        int srcfd,
        off_t beg,
        off_t end)
{
    int retval = 0;
    off_t copy_size = end - beg;
    if (!copy_size) return 0;
    assert(copy_size > 0);

    off_t off_in = beg;
    while (copy_size > 0) {
        ssize_t port_size = MAX_COPY_CHUNK;
        if (copy_size < port_size) port_size = copy_size;

        ssize_t rem_size = port_size;
        while (rem_size > 0) {
            ssize_t r = sendfile(dstfd, srcfd, &off_in, rem_size);
            if (r < 0) {
                fprintf(stderr, "extract_file_fd: sendfile: %s\n",
                        strerror(errno));
                exit(1);
            }
            if (!r) {
                fprintf(stderr, "sendfile returned 0!\n");
                exit(1);
            }
            fprintf(stderr, "sendfile success: %lld\n", (long long) r);
            rem_size -= r;
        }

        copy_size -= port_size;
    }

    return retval;
}

int
extract_file(
        const char *name,
        int srcfd,
        off_t beg,
        off_t end)
{
    int file_created = 0;
    int retval = 0;
    off_t copy_size = end - beg;
    assert(copy_size >= 0);

    int dstfd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
    if (dstfd < 0) {
        fprintf(stderr, "extract_file: open '%s': %s\n",
                name, strerror(errno));
        retval = -1;
        goto cleanup;
    }
    file_created = 1;
    if (ftruncate(dstfd, copy_size) < 0) {
        fprintf(stderr, "extract_file: ftruncate: %s\n",
                strerror(errno));
        retval = -1;
        goto cleanup;
    }
    retval = extract_file_fd(dstfd, srcfd, beg, end);
    if (retval < 0) {
        goto cleanup;
    }
    if (fsync(dstfd) < 0) {
        fprintf(stderr, "extract_file: fsync: %s\n",
                strerror(errno));
        retval = -1;
        goto cleanup;
    }

cleanup:
    if (dstfd >= 0) close(dstfd);
    if (retval < 0 && file_created) unlink(name);
    return retval;
}

char *
extract_dirname(char *buf, size_t size, const char *path)
{
    char *slptr;
    if (!path || !(slptr = strrchr(path, '/'))) {
        snprintf(buf, size, "./");
    } else {
        snprintf(buf, size, "%.*s", (int)(slptr + 1 - path), path);
    }
    return buf;
}

int
create_temporary_fd(
        char *buf,
        size_t size,
        const char *path)
{
    int attempts = 0;
    char out_path[PATH_MAX];
    const char *separator = "/";
    const char *s;

    if ((!path || !*path) && (s = getenv("XDG_RUNTIME_DIR"))) {
        path = s;
    }
    if ((!path || !*path) && (s = getenv("TMPDIR"))) {
        path = s;
    }
    if ((!path || !*path)) {
        path = "/tmp";
    }
    int pathlen = strlen(path);
    if (pathlen > 0 && path[pathlen - 1] == '/') {
        separator = "";
    }

    while (1) {
        unsigned char rand_key[16];
        random_bytes(rand_key, sizeof(rand_key));
        unsigned char rand_name[32];
        base32_buf((unsigned char *) rand_name, rand_key, sizeof(rand_key), 0);
        snprintf(out_path, sizeof(out_path), "%s%s%s", path, separator, rand_name);
        int tfd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
        if (tfd >= 0) {
            snprintf(buf, size, "%s", out_path);
            return tfd;
        }
        if (errno != EEXIST) {
            fprintf(stderr, "create_temporary_fd: open(%s) failed: %s\n", out_path, strerror(errno));
            return -1;
        }
        if (attempts == 5) {
            fprintf(stderr, "create_temporary_fd: too many attempts at opening temporary file\n");
            return -1;
        }
        ++attempts;
    }
}

