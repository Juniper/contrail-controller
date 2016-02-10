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
    typedef boost::function<void(InstanceTask *task, const std::string &msg)>
        OnDataCallback;
    typedef boost::function<void(InstanceTask *task,
            const boost::system::error_code &ec)>OnExitCallback;

    InstanceTask();
    virtual ~InstanceTask() {}

    virtual bool Run() = 0;
    virtual void Stop() = 0;
    virtual void Terminate() = 0;

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

    void set_on_data_cb(OnDataCallback cb) {
        on_data_cb_ = cb;
    }

    void set_on_exit_cb(OnExitCallback cb) {
        on_exit_cb_ = cb;
    }

 protected:
    bool is_running_;
    time_t start_time_;
    OnDataCallback on_data_cb_;
    OnExitCallback on_exit_cb_;
};

class InstanceTaskExecvp : public InstanceTask {
 public:
    static const size_t kBufLen = 4096;

    InstanceTaskExecvp(const std::string &name, const std::string &cmd,
                       int cmd_type, EventManager *evm);

    bool Run();
    void Stop();
    void Terminate();

    pid_t pid() const {
        return pid_;
    }

    void set_cmd(std::string cmd) {
        cmd_ = cmd;
    }

    const std::string &cmd() const {
        return cmd_;
    }

    int cmd_type() const {
        return cmd_type_;
    }

    void set_pipe_stdout(bool pipe) {
        pipe_stdout_ = pipe;
    }

 private:
    void ReadData(const boost::system::error_code &ec, size_t read_bytes);

    const std::string name_;
    std::string cmd_;
    boost::asio::posix::stream_descriptor input_;
    char rx_buff_[kBufLen];
    AgentSignal::SignalChildHandler sig_handler_;

    pid_t pid_;
    int cmd_type_;
    bool pipe_stdout_;
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
