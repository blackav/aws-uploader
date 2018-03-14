#include "subprocess.h"

#include <sstream>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/resource.h>
#include <sys/wait.h>

bool
Subprocess::run_and_wait()
{
    signal(SIGPIPE, SIG_IGN);

    if (pipe(in_pipe) < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: pipe: %s\n",
                strerror(errno));
        return false;
    }
    if (pipe(out_pipe) < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: pipe: %s\n",
                strerror(errno));
        return false;
    }
    if (pipe(err_pipe) < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: pipe: %s\n",
                strerror(errno));
        return false;
    }
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: fork: %s\n",
                strerror(errno));
        return false;
    }
    if (!pid) {
        dup2(in_pipe[0], 0);
        close(in_pipe[0]); close(in_pipe[1]);
        dup2(out_pipe[1], 1);
        close(out_pipe[0]); close(out_pipe[1]);
        dup2(err_pipe[1], 2);
        close(err_pipe[0]); close(err_pipe[1]);

        char **aaa = (char**) calloc(args_.size() + 2, sizeof(aaa[0]));
        aaa[0] = (char*) cmd_.c_str();
        for (size_t i = 0; i < args_.size(); ++i) {
            aaa[i + 1] = (char*) args_[i].c_str();
        }
        execvp(aaa[0], aaa);
        fprintf(stderr, "Subprocess::run_and_wait: execvp: %s\n",
                strerror(errno));
        _exit(1);
    }

    close(in_pipe[0]); in_pipe[0] = -1;
    close(out_pipe[1]); out_pipe[1] = -1;
    close(err_pipe[1]); err_pipe[1] = -1;

    if (!input_.size() && input_fd < 0) {
        close(in_pipe[1]); in_pipe[1] = -1;
    }

    sigset_t ss, olds;
    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigprocmask(SIG_BLOCK, &ss, &olds);
    signal_fd = signalfd(-1, &ss, 0);
    if (signal_fd < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: signalfd: %s\n",
                strerror(errno));
        return false;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        fprintf(stderr, "Subprocess::run_and_wait: epoll: %s\n",
                strerror(errno));
        return false;
    }

    if (in_pipe[1] >= 0) {
        fcntl(in_pipe[1], F_SETFL, fcntl(in_pipe[1], F_GETFL, 0) | O_NONBLOCK);
    }
    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, fcntl(err_pipe[0], F_GETFL, 0) | O_NONBLOCK);

    int fd_count = 0;
    if (in_pipe[1] >= 0) {
        struct epoll_event ev1 = { EPOLLOUT, { .fd = in_pipe[1] } };
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, in_pipe[1], &ev1);
        ++fd_count;
    }
    {
        struct epoll_event ev2 = { EPOLLIN, { .fd = out_pipe[0] } };
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, out_pipe[0], &ev2);
        ++fd_count;
    }
    {
        struct epoll_event ev3 = { EPOLLIN, { .fd = err_pipe[0] } };
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, err_pipe[0], &ev3);
        ++fd_count;
    }
    {
        struct epoll_event ev4 = { EPOLLIN, { .fd = signal_fd } };
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_fd, &ev4);
    }

    while (fd_count > 0) {
        constexpr int EVENT_SIZE = 5;
        struct epoll_event evs[EVENT_SIZE];
        int n = epoll_wait(epoll_fd, evs, EVENT_SIZE, -1);
        if (n < 0) {
            fprintf(stderr, "Subprocess::run_and_wait: epoll_wait: %s\n",
                    strerror(errno));
            return false;
        }
        if (!n) {
            // the process is finished, and no data received from pipes
            if (in_pipe[1] >= 0) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                close(in_pipe[1]); in_pipe[1] = -1;
                --fd_count;
            }
            if (out_pipe[0] >= 0) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, out_pipe[0], NULL);
                close(out_pipe[0]); out_pipe[0] = -1;
                --fd_count;
            }
            if (err_pipe[0] >= 0) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, err_pipe[0], NULL);
                close(err_pipe[0]); err_pipe[0] = -1;
                --fd_count;
            }
            continue;
        }
        for (int i = 0; i < n; ++i) {
            struct epoll_event *pev = &evs[i];
            if (in_pipe[1] >= 0 && (pev->events & (EPOLLOUT | EPOLLHUP))
                && pev->data.fd == in_pipe[1]) {
                if (input_fd >= 0) {
                    // splice a file descriptor
                    while (1) {
                        off_t diff = input_end - input_beg;
                        assert((size_t) diff == diff);
                        size_t wsz = diff;
                        assert((ssize_t) wsz > 0);
                        ssize_t ww = splice(input_fd, &input_beg, in_pipe[1], NULL, wsz, 0);
                        if (ww < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            // that's ok
                            break;
                        } else if (ww < 0 && errno == EPIPE) {
                            // that's ok
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else if (ww < 0) {
                            // report error
                            fprintf(stderr, "Subprocess::run_and_wait: splice: %s\n",
                                    strerror(errno));
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else if (!ww) {
                            fprintf(stderr, "Subprocess::run_and_wait: splice returned 0!\n");
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else {
                            if (input_beg == input_end) {
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                                close(in_pipe[1]); in_pipe[1] = -1;
                                --fd_count;
                                break;
                            }
                        }
                    }
                } else {
                    while (1) {
                        size_t wsz = input_.size() - input_ptr;
                        assert((ssize_t) wsz > 0);
                        ssize_t ww = write(in_pipe[1], input_.data() + input_ptr, wsz);
                        if (ww < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            // that's ok
                            break;
                        } else if (ww < 0 && errno == EPIPE) {
                            // that's ok
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else if (ww < 0) {
                            // report error
                            fprintf(stderr, "Subprocess::run_and_wait: write: %s\n",
                                    strerror(errno));
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else if (!ww) {
                            fprintf(stderr, "Subprocess::run_and_wait: write returned 0!\n");
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                            close(in_pipe[1]); in_pipe[1] = -1;
                            --fd_count;
                            break;
                        } else {
                            input_ptr += ww;
                            if (input_ptr == input_.size()) {
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, in_pipe[1], NULL);
                                close(in_pipe[1]); in_pipe[1] = -1;
                                --fd_count;
                                break;
                            }
                        }
                    }
                }
            } else if (signal_fd >= 0 && (pev->events & (EPOLLIN | EPOLLHUP))
                       && pev->data.fd == signal_fd) {
                struct signalfd_siginfo sif;
                ssize_t rr = read(signal_fd, &sif, sizeof(sif));
                if (rr < 0) {
                    fprintf(stderr, "Subprocess::run_and_wait: read signalfd: %s\n",
                            strerror(errno));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, NULL);
                    close(signal_fd); signal_fd = -1;
                } else if (!rr) {
                    fprintf(stderr, "Subprocess::run_and_wait: signalfd EOF\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, NULL);
                    close(signal_fd); signal_fd = -1;
                } else if (rr != sizeof(sif)) {
                    fprintf(stderr, "Subprocess::run_and_wait: short read\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, NULL);
                    close(signal_fd); signal_fd = -1;
                } else if (sif.ssi_signo != SIGCHLD) {
                    fprintf(stderr, "Subprocess::run_and_wait: signal %d\n",
                            sif.ssi_signo);
                } else if ((int) sif.ssi_pid != pid) {
                    fprintf(stderr, "Subprocess::run_and_wait: pid %d\n",
                            sif.ssi_pid);
                } else {
                    //process_finished = true;
                }
            } else if (out_pipe[0] >= 0 && (pev->events & (EPOLLIN | EPOLLHUP))
                       && pev->data.fd == out_pipe[0]) {
                while (1) {
                    char buf[65536 * 3];
                    ssize_t rr = read(out_pipe[0], buf, sizeof(buf));
                    if (rr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (rr < 0) {
                        fprintf(stderr, "Subprocess::run_and_wait: read: %s\n",
                                strerror(errno));
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, out_pipe[0], NULL);
                        close(out_pipe[0]); out_pipe[0] = -1;
                        --fd_count;
                        break;
                    } else if (!rr) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, out_pipe[0], NULL);
                        close(out_pipe[0]); out_pipe[0] = -1;
                        --fd_count;
                        break;
                    } else {
                        output_.append(buf, rr);
                    }
                }
            } else if (err_pipe[0] >= 0 && (pev->events & (EPOLLIN | EPOLLHUP))
                       && pev->data.fd == err_pipe[0]) {
                while (1) {
                    char buf[65536 * 3];
                    ssize_t rr = read(err_pipe[0], buf, sizeof(buf));
                    if (rr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (rr < 0) {
                        fprintf(stderr, "Subprocess::run_and_wait: read: %s\n",
                                strerror(errno));
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, err_pipe[0], NULL);
                        close(err_pipe[0]); err_pipe[0] = -1;
                        --fd_count;
                        break;
                    } else if (!rr) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, err_pipe[0], NULL);
                        close(err_pipe[0]); err_pipe[0] = -1;
                        --fd_count;
                        break;
                    } else {
                        error_.append(buf, rr);
                    }
                }
            } else {
                fprintf(stderr, "Subprocess::run_and_wait: epoll event %d on fd %d\n",
                        pev->events, pev->data.fd);
                if (in_pipe[1] >= 0 && pev->data.fd == in_pipe[1]) {
                    fprintf(stderr, "Subprocess::run_and_wait: closing stdin pipe\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pev->data.fd, NULL);
                    close(in_pipe[1]); in_pipe[1] = -1;
                    --fd_count;
                } else if (signal_fd >= 0 && pev->data.fd == signal_fd) {
                    fprintf(stderr, "Subprocess::run_and_wait: closing signalfd\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pev->data.fd, NULL);
                    close(signal_fd); signal_fd = -1;
                } else if (out_pipe[0] >= 0 && pev->data.fd == out_pipe[0]) {
                    fprintf(stderr, "Subprocess::run_and_wait: closing stdout pipe\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pev->data.fd, NULL);
                    close(out_pipe[0]); out_pipe[0] = -1;
                    --fd_count;
                } else if (err_pipe[0] >= 0 && pev->data.fd == err_pipe[0]) {
                    fprintf(stderr, "Subprocess::run_and_wait: closing stderr pipe\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pev->data.fd, NULL);
                    close(err_pipe[0]); err_pipe[0] = -1;
                    --fd_count;
                } else {
                    fprintf(stderr, "Subprocess::run_and_wait: removing unexpected fd\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pev->data.fd, NULL);
                }
            }
        }
    }

    if (in_pipe[1] >= 0) {
        close(in_pipe[1]); in_pipe[1] = -1;
    }
    if (out_pipe[0] >= 0) {
        close(out_pipe[0]); out_pipe[0] = -1;
    }
    if (err_pipe[0] >= 0) {
        close(err_pipe[0]); err_pipe[0] = -1;
    }
    if (signal_fd >= 0) {
        close(signal_fd); signal_fd = -1;
    }
    if (epoll_fd >= 0) {
        close(epoll_fd); epoll_fd = -1;
    }

    if (pid > 0) {
        struct rusage ru;
        int status = 0;
        int res = wait4(pid, &status, 0, &ru);
        if (res < 0) {
            fprintf(stderr, "Subprocess::run_and_wait: wait4: %s\n",
                    strerror(errno));
        } else if (res != pid) {
            fprintf(stderr, "Subprocess::run_and_wait: wrong PID\n");
        } else {
            proc_status = status;
            ru_utime = ru.ru_utime.tv_sec * 1000ULL + ru.ru_utime.tv_usec / 1000ULL;
            ru_stime = ru.ru_stime.tv_sec * 1000ULL + ru.ru_stime.tv_usec / 1000ULL;
            ru_maxrss = ru.ru_maxrss;
            ru_nivcsw = ru.ru_nivcsw;
            ru_nvcsw = ru.ru_nvcsw;
        }
    }
    pid = -1;

    return WIFEXITED(proc_status) && !WEXITSTATUS(proc_status);
}

Subprocess::~Subprocess()
{
    if (in_pipe[0] >= 0) close(in_pipe[0]);
    if (in_pipe[1] >= 0) close(in_pipe[1]);
    if (out_pipe[0] >= 0) close(out_pipe[0]);
    if (out_pipe[1] >= 0) close(out_pipe[1]);
    if (err_pipe[0] >= 0) close(err_pipe[0]);
    if (err_pipe[1] >= 0) close(err_pipe[1]);
    if (epoll_fd >= 0) close(epoll_fd);
    if (signal_fd >= 0) close(signal_fd);
}

bool
Subprocess::successful() const
{
    if (proc_status < 0) return false;
    return WIFEXITED(proc_status) && !WEXITSTATUS(proc_status);
}

std::string
Subprocess::stats() const
{
    std::ostringstream oss;
    oss << " utime=" << ru_utime << " stime=" << ru_stime
        << " maxrss=" << ru_maxrss << " nvcsw=" << ru_nvcsw
        << " nivcsw=" << ru_nivcsw;
    return oss.str();
}


