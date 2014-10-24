/*
 * Copyright (c) 2014 Juniper Networks, Inc. All right reserved.
 */

#include "oper/instance_task.h"

#include "base/logging.h"
#include "io/event_manager.h"

/*
 * InstanceTask class
 */
InstanceTask::InstanceTask(const std::string &cmd, int cmd_type, EventManager *evm) :
        cmd_(cmd), errors_(*(evm->io_service())), is_running_(false),
        pid_(0), cmd_type_(cmd_type), start_time_(0) {
}

void InstanceTask::ReadErrors(const boost::system::error_code &ec,
                               size_t read_bytes) {
    if (read_bytes) {
        errors_data_ << rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        std::string errors = errors_data_.str();
        if (errors.length() > 0) {
            LOG(ERROR, "NetNS run errors: " << std::endl << errors);

            if (!on_error_cb_.empty()) {
                on_error_cb_(this, errors);
            }
        }
        errors_data_.clear();

        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(
                    errors_,
                    boost::asio::buffer(rx_buff_, kBufLen),
                    boost::bind(&InstanceTask::ReadErrors,
                                this, boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
}

void InstanceTask::Stop() {
    assert(pid_);
    kill(pid_, SIGTERM);
}

void InstanceTask::Terminate() {
    assert(pid_);
    kill(pid_, SIGKILL);
}

pid_t InstanceTask::Run() {
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
        return -1;
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
        dup2(err[1], STDERR_FILENO);
        close(err[1]);

        close(STDOUT_FILENO);
        close(STDIN_FILENO);

        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        _exit(127);
    }
    if (sigprocmask(SIG_SETMASK, &orig_mask, NULL) < 0) {
        LOG(ERROR, "NetNS error: sigprocmask, " << strerror(errno));
    }
    close(err[1]);

    start_time_ = time(NULL);

    boost::system::error_code ec;
    errors_.assign(::dup(err[0]), ec);
    close(err[0]);
    if (ec) {
        is_running_ = false;
        return -1;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&InstanceTask::ReadErrors,
                        this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));

    return pid_;
}

/*
 *
 */
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
