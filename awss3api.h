// -*- mode: c++ -*-
#pragma once

#include <string>

namespace aws { namespace s3 {

struct Result
{
    bool success = false;

    std::string message;   // in case of error, short message
    std::string errors;    // extended message (log from invocation)

    std::string bucket;
    std::string upload_id;
    std::string key;
    std::string etag;
    std::string location;

    Result() = default;
    Result(const Result &other) = delete;
    Result(Result &&other) = default;

    Result &operator=(const Result &other) = delete;
    Result &operator=(Result &&other) = default;

    explicit operator bool() const { return success; }
    bool operator! () const { return !success; }
};

Result
create_multipart_upload(
        const std::string &bucket,
        const std::string &key);

Result
abort_multipart_upload(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id);

Result
complete_multipart_upload(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id,
        const std::string &multipart_upload_file);

Result
upload_part(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id,
        const std::string &tmp_dir,
        int part_number,
        int fd,
        off_t beg,
        off_t end);

} }
