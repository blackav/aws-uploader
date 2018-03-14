#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <sys/mman.h>

#include "base64.h"

enum { MMAP_WINDOW_SIZE = 64 * 1024 * 1024 };

int
md5_base64_fd_offsets(
        int fd,
        off_t beg,
        off_t end,
        char *b64_buf,
        size_t b64_size)
{
    int retval = 0;
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;
    MD5_Init(&ctx);

    while (beg < end) {
        off_t rem_size = end - beg;
        size_t mmap_size = MMAP_WINDOW_SIZE;
        if (rem_size < mmap_size) {
            mmap_size = rem_size;
        }
        const char *ptr = mmap(NULL, mmap_size, PROT_READ, MAP_PRIVATE, fd, beg);
        if (ptr == MAP_FAILED) {
            fprintf(stderr, "md5_base64_fd_offsets: mmap: %s\n",
                    strerror(errno));
            retval = -1;
            break;
        }
        MD5_Update(&ctx, ptr, mmap_size);
        munmap((void*) ptr, mmap_size);
        beg += mmap_size;
    }

    MD5_Final(digest, &ctx);

    if (b64_size >= ((MD5_DIGEST_LENGTH + 2) / 3 * 4 + 1)) {
        base64_encode((char*) digest, sizeof(digest), b64_buf);
    } else {
        char tmpbuf[(MD5_DIGEST_LENGTH + 2) / 3 * 4 + 1];
        base64_encode((char*) digest, sizeof(digest), tmpbuf);
        snprintf(b64_buf, b64_size, "%s", tmpbuf);
    }

    return retval;
}

int md5_base64_fd(int fd, char *b64_buf, size_t b64_size)
{
    struct stat stb;
    if (fstat(fd, &stb) < 0) {
        fprintf(stderr, "md5_base64_fd: fstat: %s\n",
                strerror(errno));
        return -1;
    }
    if (!S_ISREG(stb.st_mode)) {
        fprintf(stderr, "md5_base64_fd: not a regular file\n");
        return -1;
    }

    return md5_base64_fd_offsets(fd, 0, stb.st_size, b64_buf, b64_size);
}

int md5_base64_file(const char *path, char *b64_buf, size_t b64_size)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "md5_base64_file: open '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    int res = md5_base64_fd(fd, b64_buf, b64_size);
    close(fd);
    return res;
}
