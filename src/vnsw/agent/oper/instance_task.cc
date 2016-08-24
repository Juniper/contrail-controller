/*
 * Copyright (c) 2014 Juniper Networks, Inc. All right reserved.
 */

#include <tbb/tbb.h>
#include "oper/instance_task.h"
#include "base/logging.h"
#include "io/event_manager.h"

InstanceTask::InstanceTask()
: is_running_(false), start_time_(0), reattempts_(0)
{}

InstanceTaskExecvp::InstanceTaskExecvp(const std::string &cmd,
                            int cmd_type, EventManager *evm) :
        cmd_(cmd), errors_(*(evm->io_service())), setup_done_(false),
        pid_(0), cmd_type_(cmd_type) {
}

void InstanceTaskExecvp::ReadErrors(const boost::system::error_code &ec,
                                                   size_t read_bytes) {
    if (read_bytes) {
        if (!on_error_cb_.empty())
            on_error_cb_(this, rx_buff_);
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        if (!on_exit_cb_.empty()) {
            on_exit_cb_(this, ec);
        }
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(
                    errors_,
                    boost::asio::buffer(rx_buff_, kBufLen),
                    boost::bind(&InstanceTaskExecvp::ReadErrors,
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

bool InstanceTaskExecvp::IsSetup() {
    return setup_done_;
}

// If there is an error before the fork, task is set to "not running"
// and "false" is returned to caller so that caller can take appropriate
// action on task. If an error is encounted after  fork, it is very
// likely that child process is running so we keep the task status as
// "running" and return "false" to caller, so that caller does not
// attempt to run the same task again. In this case, the child process
// exit notification can not be received by instance manager, hence
// instance manager has to rely on TaskTimeout delete the task. 
bool InstanceTaskExecvp::Run() {
    std::vector<std::string> argv;

    is_running_ = true;

    boost::split(argv, cmd_, boost::is_any_of(" "), boost::token_compress_on);
    std::vector<const char *> c_argv(argv.size() + 1);
    for (std::size_t i = 0; i != argv.size(); ++i) {
        c_argv[i] = argv[i].c_str();
    }

    int err[2];
    if (pipe(err) < 0) {
        return is_running_ = false;
    }

    pid_ = vfork();
    if (pid_ == 0) {
        close(err[0]);
        dup2(err[1], STDERR_FILENO);
        close(err[1]);

        close(STDOUT_FILENO);
        close(STDIN_FILENO);

        /* Close all the open fds before execvp */
        CloseTaskFds();
        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        _exit(127);
    }

    close(err[1]);

    start_time_ = time(NULL);

    int fd = ::dup(err[0]);
    close(err[0]);
    if (fd == -1) {
        //is_running_ is still true indicating the child process is
        //running. Caller needs to verify the status before acting on
        //the task again
        return false;
    }
    boost::system::error_code ec;
    errors_.assign(fd, ec);
    if (ec) {
        close(fd);

        //is_running_ is still true indicating the child process is
        //running. Caller needs to verify the status before acting on
        //the task again
        return false;
    }
    setup_done_ = true;

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&InstanceTaskExecvp::ReadErrors,
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
