#include "awss3api.h"
#include "extract_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <vector>

constexpr off_t part_size = 512*1024*1024;
constexpr off_t min_part_size = 128*1024*1024;

int main(int argc, char *argv[])
{
    std::string bucket_name;
    std::string bucket_key;
    std::string input_file;

    if (sizeof(off_t) != sizeof(long long)) {
        fprintf(stderr, "long file support disabled\n");
        return 1;
    }

    int argi = 1;
    while (argi < argc) {
        if (!strcmp(argv[argi], "--bucket")) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "argument expected after --bucket\n");
                return 1;
            }
            bucket_name.assign(argv[argi + 1]);
            argi += 2;
        } else if (!strcmp(argv[argi], "--key")) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "argument expected after --key\n");
                return 1;
            }
            bucket_key.assign(argv[argi + 1]);
            argi += 2;
        } else if (!strcmp(argv[argi], "--")) {
            ++argi;
            break;
        } else if (argv[argi][0] == '-') {
            fprintf(stderr, "unhandled option '%s'\n", argv[argi]);
            return 1;
        } else {
            break;
        }
    }
    if (argi >= argc) {
        fprintf(stderr, "filename expected\n");
        return 1;
    }
    if (argi + 1 < argc) {
        fprintf(stderr, "only single file upload supported\n");
        return 1;
    }
    input_file.assign(argv[argi]);

    if (!bucket_name.length()) {
        fprintf(stderr, "--bucket option is required");
    }
    if (!input_file.length()) {
        fprintf(stderr, "input file name is required");
    }
    if (!bucket_key.length()) {
        bucket_key = input_file;
    }

    int fd = open(input_file.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", input_file.c_str(), strerror(errno));
        return 1;
    }
    struct stat stb;
    if (fstat(fd, &stb) < 0) {
        fprintf(stderr, "fstat failed: %s\n", strerror(errno));
        return 1;
    }
    if (!S_ISREG(stb.st_mode)) {
        fprintf(stderr, "not a regular file\n");
        return 1;
    }
    if (stb.st_size <= 0) {
        fprintf(stderr, "empty file\n");
        return 1;
    }

    char test_dir[PATH_MAX];
    extract_dirname(test_dir, sizeof(test_dir), input_file.c_str());
    fprintf(stderr, "tmpdir: %s\n", test_dir);
    std::string test_dirs(test_dir);

    aws::s3::Result res = aws::s3::create_multipart_upload(bucket_name, bucket_key);
    printf("res.success: %d\n", res.success);
    printf("res.bucket: %s\n", res.bucket.c_str());
    printf("res.key: %s\n", res.key.c_str());
    printf("res.upload_id: %s\n", res.upload_id.c_str());
    if (!res.success) {
        return 1;
    }

    std::vector<std::string> parts;
    off_t cur_beg = 0;
    off_t end = stb.st_size;
    int part_number = 0;

    while (cur_beg < end) {
        off_t upload_size = 0;
        if (cur_beg + part_size + min_part_size >= end) {
            upload_size = end - cur_beg;
        } else if (cur_beg + part_size >= end) {
            upload_size = end - cur_beg;
        } else {
            upload_size = part_size;
        }
        ++part_number;

        aws::s3::Result res2 = aws::s3::upload_part(bucket_name, bucket_key, res.upload_id, test_dirs,
                                                    part_number, fd, cur_beg, cur_beg + upload_size);
        printf("res2.success: %d\n", res2.success);
        printf("res2.ETag: %s\n", res2.etag.c_str());
        if (!res2.success) {
            aws::s3::abort_multipart_upload(bucket_name, bucket_key, res.upload_id);
            return 1;
        }

        parts.push_back(std::move(res2.etag));
        cur_beg += upload_size;
    }

    char parts_path_buf[PATH_MAX];
    int pfd = create_temporary_fd(parts_path_buf, sizeof(parts_path_buf), test_dirs.c_str());
    if (pfd < 0) {
        aws::s3::abort_multipart_upload(bucket_name, bucket_key, res.upload_id);
        return 1;
    }
    FILE *pf = fdopen(pfd, "w"); pfd = -1;
    fprintf(pf, "{\n  \"Parts\": [\n");
    for (size_t i = 0; i < parts.size(); ++i) {
        fprintf(pf, "    {\n      \"ETag\": %s,\n      \"PartNumber\": %d\n    }",
                parts[i].c_str(), (int) i + 1);
        if (i + 1 < parts.size()) fprintf(pf, ",");
        fprintf(pf, "\n");
    }
    fprintf(pf, "  ]\n}\n");
    fflush(pf);
    if (ferror(pf)) {
        unlink(parts_path_buf);
        aws::s3::abort_multipart_upload(bucket_name, bucket_key, res.upload_id);
        return 1;
    }
    fclose(pf);

    aws::s3::Result res3 = aws::s3::complete_multipart_upload(bucket_name, bucket_key, res.upload_id, parts_path_buf);
    printf("res3.success: %d\n", res3.success);
    if (!res3.success) {
        aws::s3::abort_multipart_upload(bucket_name, bucket_key, res.upload_id);
        unlink(parts_path_buf);
        return 1;
    }
    unlink(parts_path_buf);

    return 0;
}
