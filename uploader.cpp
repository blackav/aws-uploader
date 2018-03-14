#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "rapidjson/document.h"

#include "md5_base64_file.h"
#include "extract_file.h"
#include "random.h"
#include "base32.h"
#include "upload_state.h"

int main(int argc, char *argv[])
{
    int in_fd = open(argv[1], O_RDONLY, 0);
    printf("%zu\n", sizeof(off_t));
    (void) in_fd;
    off_t end = lseek(in_fd, 0, SEEK_END);
    printf("%lld\n", (long long) end);

    /*
    char buf[32];
    md5_base64_fd(in_fd, buf, sizeof(buf));
    printf("%s\n", buf);
    */
    unsigned char rawbuf[5];
    random_bytes(rawbuf, sizeof(rawbuf));
    char namebuf[32];
    base32_buf((unsigned char*) namebuf, rawbuf, sizeof(rawbuf), 0);

    off_t startpos = 8192;
    off_t extract_size = 128 * 1024 * 1024;
    extract_file(namebuf, in_fd, startpos, startpos + extract_size);

    UploadState us;
    us.set_file("xxx");
}
