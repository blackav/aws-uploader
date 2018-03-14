#include "awss3api.h"
#include "subprocess.h"
#include "md5_base64_file.h"
#include "extract_file.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

aws::s3::Result
aws::s3::create_multipart_upload(
        const std::string &bucket,
        const std::string &key)
{
    Subprocess sp;
    Result res;
    const Subprocess &csp = sp;

    sp.set_cmd("aws");
    sp.add_args({ "s3api", "create-multipart-upload", "--bucket", bucket, "--key", key });
    if (!sp.run_and_wait()) {
        res.message = "aws s3 execution failed";
        res.errors = sp.move_error();
        fprintf(stderr, "errors: <%s>\n", res.errors.c_str());
        return res;
    }

    fprintf(stderr, "output: <%s>\n", csp.output().c_str());
    fprintf(stderr, "error: <%s>\n", csp.error().c_str());

    rapidjson::Document document;
    rapidjson::ParseResult pr = document.Parse(csp.output().c_str());
    if (!pr) {
        res.message = "json parse failed";
        res.errors = rapidjson::GetParseError_En(pr.Code());
        return res;
    }

    if (!document.HasMember("Bucket") || !document["Bucket"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Bucket' field is missing or not String";
        return res;
    }
    if (!document.HasMember("Key") || !document["Key"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Key' field is missing or not String";
        return res;
    }
    if (!document.HasMember("UploadId") || !document["UploadId"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'UploadId' field is missing or not String";
        return res;
    }

    res.success = true;
    res.bucket = document["Bucket"].GetString();
    res.key = document["Key"].GetString();
    res.upload_id = document["UploadId"].GetString();

    return res;
}

aws::s3::Result
aws::s3::abort_multipart_upload(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id)
{
    Subprocess sp;
    Result res;
    const Subprocess &csp = sp;

    sp.set_cmd({ "aws", "s3api", "abort-multipart-upload", "--bucket", bucket, "--key", key, "--upload-id", upload_id });
    if (!sp.run_and_wait()) {
        res.message = "aws s3 execution failed";
        res.errors = sp.move_error();
        fprintf(stderr, "errors: <%s>\n", res.errors.c_str());
        return res;
    }

    fprintf(stderr, "output: <%s>\n", csp.output().c_str());
    fprintf(stderr, "error: <%s>\n", csp.error().c_str());
    res.success = true;
    return res;
}

aws::s3::Result
aws::s3::complete_multipart_upload(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id,
        const std::string &multipart_upload_file)
{
    Result res;
    Subprocess sp;
    const Subprocess &csp = sp;
    std::string upload_url = std::string("file://") + multipart_upload_file;

    sp.set_cmd({ "aws", "s3api", "complete-multipart-upload", "--bucket", bucket, "--key", key, "--upload-id", upload_id, "--multipart-upload", upload_url });

    if (!sp.run_and_wait()) {
        res.message = "aws s3 execution failed";
        res.errors = sp.move_error();
        fprintf(stderr, "errors: <%s>\n", res.errors.c_str());
        return res;
    }

    fprintf(stderr, "output: <%s>\n", csp.output().c_str());
    fprintf(stderr, "error: <%s>\n", csp.error().c_str());

    rapidjson::Document document;
    rapidjson::ParseResult pr = document.Parse(csp.output().c_str());
    if (!pr) {
        res.message = "json parse failed";
        res.errors = rapidjson::GetParseError_En(pr.Code());
        return res;
    }

    if (!document.HasMember("Bucket") || !document["Bucket"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Bucket' field is missing or not String";
        return res;
    }
    if (!document.HasMember("Key") || !document["Key"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Key' field is missing or not String";
        return res;
    }
    if (!document.HasMember("Location") || !document["Location"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Location' field is missing or not String";
        return res;
    }
    if (!document.HasMember("ETag") || !document["ETag"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'ETag' field is missing or not String";
        return res;
    }

    res.success = true;
    res.bucket = document["Bucket"].GetString();
    res.key = document["Key"].GetString();
    res.location = document["Location"].GetString();
    res.etag = document["ETag"].GetString();

    return res;
}

aws::s3::Result
aws::s3::upload_part(
        const std::string &bucket,
        const std::string &key,
        const std::string &upload_id,
        const std::string &tmp_dir,
        int part_number,
        int fd,
        off_t beg,
        off_t end)
{
    char b64buf[64];
    std::string content_length_str;
    std::string part_number_str;
    Result res;
    Subprocess sp;
    const Subprocess &csp = sp;

    if (md5_base64_fd_offsets(fd, beg, end, b64buf, sizeof(b64buf)) < 0) {
        return res;
    }
    content_length_str = std::to_string(static_cast<long long>(end - beg));
    part_number_str = std::to_string(part_number);

    char tmp_name_buf[PATH_MAX];
    int tfd = create_temporary_fd(tmp_name_buf, sizeof(tmp_name_buf), tmp_dir.c_str());
    if (tfd < 0) {
      return res;
    }
    if (extract_file_fd(tfd, fd, beg, end) < 0) {
        close(tfd);
        unlink(tmp_name_buf);
        return res;
    }
    close(tfd); tfd = -1;

    fprintf(stderr, "temporary: %s\n", tmp_name_buf);

    sp.set_cmd({ "aws", "s3api", "upload-part",
                "--bucket", bucket,
                "--key", key,
                "--upload-id", upload_id,
                "--part-number", part_number_str,
                "--content-length", content_length_str,
                "--content-md5", b64buf,
                "--body", tmp_name_buf });
    sp.set_input_file_range(fd, beg, end);
    if (!sp.run_and_wait()) {
        res.message = "aws s3 execution failed";
        res.errors = sp.move_error();
        fprintf(stderr, "errors: <%s>\n", res.errors.c_str());
        unlink(tmp_name_buf);
        return res;
    }

    fprintf(stderr, "output: <%s>\n", csp.output().c_str());
    fprintf(stderr, "error: <%s>\n", csp.error().c_str());

    rapidjson::Document document;
    rapidjson::ParseResult pr = document.Parse(csp.output().c_str());
    if (!pr) {
        res.message = "json parse failed";
        res.errors = rapidjson::GetParseError_En(pr.Code());
        unlink(tmp_name_buf);
        return res;
    }

    if (!document.HasMember("ETag") || !document["ETag"].IsString()) {
        res.message = "json parse failed";
        res.errors = "'Bucket' field is missing or not String";
        unlink(tmp_name_buf);
        return res;
    }

    res.success = true;
    res.etag = document["ETag"].GetString();

    unlink(tmp_name_buf);

    return res;
}
