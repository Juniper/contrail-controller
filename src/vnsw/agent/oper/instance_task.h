/*
 * Copyright (c) 2014 Juniper Networks, Inc. All right reserved.
 */
#ifndef AGENT_OPER_INSTANCE_TASK_H__
#define AGENT_OPER_INSTANCE_TASK_H__

#include <queue>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include "base/timer.h"
#include "base/queue_task.h"
#include "cmn/agent_signal.h"
#include "cmn/agent_cmn.h"
#include "db/db_entry.h"

class EventManager;

class InstanceTask {
 public:
    typedef boost::function<void(InstanceTask *task, const std::string errors)>
        OnErrorCallback;
    typedef boost::function<void(InstanceTask *task,
            const boost::system::error_code &ec)>OnExitCallback;

    InstanceTask();
    virtual ~InstanceTask() {}

    virtual bool Run() = 0;
    virtual void Stop() = 0;
    virtual void Terminate() = 0;
    virtual bool IsSetup() = 0;

    // TODO reimplement instance_manager.cc to remove these two?
    virtual pid_t pid() const = 0;
    virtual const std::string &cmd() const = 0;

    virtual int cmd_type() const = 0;

    bool is_running() const {
        return is_running_;
    }

    time_t start_time() const {
        return start_time_;
    }

    void set_on_error_cb(OnErrorCallback cb) {
        on_error_cb_ = cb;
    }

    void set_on_exit_cb(OnExitCallback cb) {
        on_exit_cb_ = cb;
    }

    int incr_reattempts() {
        return ++reattempts_;
    }

    int reattempts() {
        return reattempts_;
    }

 protected:
    bool is_running_;
    time_t start_time_;
    OnErrorCallback on_error_cb_;
    int reattempts_;
    OnExitCallback on_exit_cb_;
};

class InstanceTaskExecvp : public InstanceTask {
 public:
    static const size_t kBufLen = 4096;

    InstanceTaskExecvp(const std::string &cmd,
        int cmd_type, EventManager *evm);

    bool Run();
    void Stop();
    void Terminate();
    bool IsSetup();

    pid_t pid() const {
        return pid_;
    }

    const std::string &cmd() const {
        return cmd_;
    }

    int cmd_type() const {
            return cmd_type_;
    }


 private:
    void ReadErrors(const boost::system::error_code &ec, size_t read_bytes);
    const std::string cmd_;
    boost::asio::posix::stream_descriptor errors_;
    bool setup_done_; // indicates whether errors_ has a valid descriptor or not
    std::stringstream errors_data_;
    char rx_buff_[kBufLen];

    pid_t pid_;
    int cmd_type_;
};

class InstanceTaskMethod : public InstanceTask {
 public:
    pid_t pid() const {
        return 0;
    } 
};

class InstanceTaskQueue {
public:
    typedef boost::function<void(InstanceTaskQueue *task_queue)> OnTimeoutCallback;

    InstanceTaskQueue(EventManager *evm);
    ~InstanceTaskQueue();

    bool OnTimerTimeout();
    void TimerErrorHandler(const std::string &name, std::string error);

    InstanceTask *Front() { return task_queue_.front(); }
    void Pop() { task_queue_.pop(); }
    bool Empty() { return task_queue_.empty(); }
    void Push(InstanceTask *task) { task_queue_.push(task); }
    int Size() { return task_queue_.size(); }
    void StartTimer(int time);
    void StopTimer();
    void Clear();

    void set_on_timeout_cb(OnTimeoutCallback cb) {
        on_timeout_cb_ = cb;
    }

private:
    EventManager *evm_;
    Timer *timeout_timer_;
    std::queue<InstanceTask *> task_queue_;
    OnTimeoutCallback on_timeout_cb_;
};

#endif  // AGENT_OPER_INSTANCE_TASK_H__
