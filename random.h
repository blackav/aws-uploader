#ifndef __RANDOM_H__
#define __RANDOM_H__

#ifdef __cplusplus
extern "C" {
#endif

void
random_bytes(
        unsigned char *data,
        size_t size);

#ifdef __cplusplus
}
#endif

#endif
