/*
 * Copyright (c) 2014 Juniper Networks, Inc. All right reserved.
 */

#include <tbb/tbb.h>
#include "oper/instance_task.h"
#include "base/logging.h"
#include "io/event_manager.h"

InstanceTask::InstanceTask()
: is_running_(false), start_time_(0)
{}

InstanceTaskExecvp::InstanceTaskExecvp(const std::string &name,
                                       const std::string &cmd,
                                       int cmd_type, EventManager *evm) :
        name_(name), cmd_(cmd), input_(*(evm->io_service())),
        pid_(0), cmd_type_(cmd_type), pipe_stdout_(false) {
}

void InstanceTaskExecvp::ReadData(const boost::system::error_code &ec,
                                  size_t read_bytes) {
    if (read_bytes) {
        std::string data(rx_buff_, read_bytes);
        on_data_cb_(this, data);
    }

    if (ec) {
        boost::system::error_code close_ec;
        input_.close(close_ec);

        LOG(ERROR, "error value " << ec.value() << " for pid " << pid_ << "\n");

        if (!on_exit_cb_.empty()) {
            on_exit_cb_(this, ec);
        }
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    input_.async_read_some(boost::asio::buffer(rx_buff_, kBufLen),
                    boost::bind(&InstanceTaskExecvp::ReadData,
                                this, boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
}

void InstanceTaskExecvp::Stop() {
    assert(pid_);
    kill(pid_, SIGTERM);
}

void InstanceTaskExecvp::Terminate() {
    assert(pid_);
    kill(pid_, SIGKILL);
}

bool InstanceTaskExecvp::Run() {
    std::vector<std::string> argv;
    LOG(DEBUG, "NetNS run command: " << cmd_);

    is_running_ = true;

    boost::split(argv, cmd_, boost::is_any_of(" "), boost::token_compress_on);
    std::vector<const char *> c_argv(argv.size() + 1);
    for (std::size_t i = 0; i != argv.size(); ++i) {
        c_argv[i] = argv[i].c_str();
    }

    int err[2];
    if (pipe(err) < 0) {
        return false;
    }
    /*
     * temporarily block SIGCHLD signals
     */
    sigset_t mask;
    sigset_t orig_mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
        LOG(ERROR, "NetNS error: sigprocmask, " << strerror(errno));
    }

    pid_ = vfork();
    if (pid_ == 0) {
        close(err[0]);
        if (pipe_stdout_) {
            dup2(err[1], STDOUT_FILENO);
        } else {
            dup2(err[1], STDERR_FILENO);
        }
        close(err[1]);

        if (!pipe_stdout_) {
            close(STDOUT_FILENO);
            close(STDIN_FILENO);
        }

        /* Close all the open fds before execvp */
        CloseTaskFds();
        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        _exit(127);
    }
    if (sigprocmask(SIG_SETMASK, &orig_mask, NULL) < 0) {
        LOG(ERROR, "NetNS error: sigprocmask, " << strerror(errno));
    }
    close(err[1]);

    start_time_ = time(NULL);

    int fd = ::dup(err[0]);
    close(err[0]);
    if (fd == -1) {
        return is_running_ = false;
    }
    boost::system::error_code ec;
    input_.assign(fd, ec);
    if (ec) {
        close(fd);
        return is_running_ = false;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    input_.async_read_some(boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&InstanceTaskExecvp::ReadData,
                        this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    return true;
}

InstanceTaskQueue::InstanceTaskQueue(EventManager *evm) : evm_(evm),
                timeout_timer_(TimerManager::CreateTimer(
                               *evm_->io_service(),
                               "Instance Manager Task Timeout",
                               TaskScheduler::GetInstance()->GetTaskId(
                                               "db::DBTable"), 0)) {
}

InstanceTaskQueue::~InstanceTaskQueue() {
    TimerManager::DeleteTimer(timeout_timer_);
}

void InstanceTaskQueue::StartTimer(int time) {
    timeout_timer_->Start(time,
                          boost::bind(&InstanceTaskQueue::OnTimerTimeout,
                                      this),
                          boost::bind(&InstanceTaskQueue::TimerErrorHandler,
                                      this, _1, _2));
}

void InstanceTaskQueue::StopTimer() {
    timeout_timer_->Cancel();
}

bool InstanceTaskQueue::OnTimerTimeout() {
    if (! on_timeout_cb_.empty()) {
        on_timeout_cb_(this);
    }

    return true;
}

void InstanceTaskQueue::TimerErrorHandler(const std::string &name, std::string error) {
    LOG(ERROR, "NetNS timeout error: " << error);
}

void InstanceTaskQueue::Clear() {
    timeout_timer_->Cancel();

    while(!task_queue_.empty()) {
        InstanceTask *task = task_queue_.front();
        task_queue_.pop();
        delete task;
    }
}
