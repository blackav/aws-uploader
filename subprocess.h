#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <initializer_list>

class Subprocess
{
    std::vector<std::string> args_;
    std::string cmd_;

    std::string input_;
    int input_fd = -1;
    off_t input_beg = 0;
    off_t input_end = 0;

    // state
    int pid = -1;
    int in_pipe[2] = { -1, -1 };
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    int epoll_fd = -1;
    int signal_fd = -1;

    size_t input_ptr = 0;

    std::string output_;
    std::string error_;

    int proc_status = -1;
    uint64_t ru_utime = 0;
    uint64_t ru_stime = 0;
    uint64_t ru_maxrss = 0;
    uint64_t ru_nvcsw = 0;
    uint64_t ru_nivcsw = 0;

public:
    Subprocess() noexcept {}
    ~Subprocess();

    Subprocess(const Subprocess &other) = delete;
    Subprocess &operator= (const Subprocess &other) = delete;

    void set_cmd(const std::string &cmd) { cmd_.assign(cmd); }
    void set_cmd(std::string &&cmd) { cmd_.assign(cmd); }

    void add_arg(const std::string &arg) { args_.push_back(arg); }
    void add_arg(std::string &&arg) { args_.push_back(arg); }

    void add_args(std::initializer_list<std::string> lst)
    {
        args_.insert(args_.end(), lst);
    }
    void set_cmd(std::initializer_list<std::string> lst)
    {
        auto iter = lst.begin();
        cmd_.assign(*iter);
        ++iter;
        args_.insert(args_.end(), iter, lst.end());
    }

    void set_input(const std::string &input) { input_.assign(input); }
    void set_input(std::string &&input) { input_.assign(input); }
    void set_input_file_range(int fd, off_t beg, off_t end)
    {
        input_fd = fd;
        input_beg = beg;
        input_end = end;
    }

    bool run_and_wait();
    bool successful() const;

    const std::string &output() const { return output_; }
    std::string output() { return output_; }
    std::string &&move_output() { return std::move(output_); }

    const std::string &error() const { return error_; }
    std::string error() { return error_; }
    std::string &&move_error() { return std::move(error_); }

    std::string stats() const;
};
