#ifndef __EXTRACT_FILE_H__
#define __EXTRACT_FILE_H__

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int
extract_file_fd(
        int dstfd,
        int srcfd,
        off_t beg,
        off_t end);

int
extract_file(
        const char *name,
        int srcfd,
        off_t beg,
        off_t end);

char *
extract_dirname(
        char *buf,
        size_t size,
        const char *path);

int
create_temporary_fd(
        char *buf,
        size_t size,
        const char *path);
  
#ifdef __cplusplus
}
#endif

#endif
