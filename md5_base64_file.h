#ifndef __MD5_BASE64_FILE_H__
#define __MD5_BASE64_FILE_H__

#ifdef __cplusplus
extern "C" {
#endif

int
md5_base64_fd_offsets(
        int fd,
        off_t beg,
        off_t end,
        char *b64_buf,
        size_t b64_size);

int
md5_base64_fd(
        int fd,
        char *b64_buf,
        size_t b64_size);

int
md5_base64_file(
        const char *path,
        char *b64_buf,
        size_t b64_size);

#ifdef __cplusplus
}
#endif

#endif
