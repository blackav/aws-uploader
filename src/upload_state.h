#pragma once

#include <string>

class UploadState
{
    std::string file_;

public:
    UploadState() noexcept;

    UploadState(const UploadState &) = delete;
    UploadState(UploadState &&) = delete;

    UploadState &operator= (const UploadState &) = delete;
    UploadState &operator= (UploadState &) = delete;

    const std::string &file() const { return file_; }
    void set_file(const std::string &file)
    {
        file_.assign(file);
    }
    void set_file(std::string &&file)
    {
        file_.assign(file);
    }
};
