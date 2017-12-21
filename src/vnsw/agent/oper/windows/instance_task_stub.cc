/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/instance_task.h>

// Not yet supported on Windows

InstanceTask::InstanceTask() {
}

InstanceTaskExecvp::InstanceTaskExecvp(const std::string &name,
                                       const std::string &cmd,
                                       int cmd_type, EventManager *evm) {
}

void InstanceTaskExecvp::ReadData(const boost::system::error_code &ec,
                                  size_t read_bytes) {
}

void InstanceTaskExecvp::Stop() {
}

void InstanceTaskExecvp::Terminate() {
}

bool InstanceTaskExecvp::IsSetup() {
    return false;
}

bool InstanceTaskExecvp::Run() {
    return true;
}

InstanceTaskQueue::InstanceTaskQueue(EventManager *evm) {
}

InstanceTaskQueue::~InstanceTaskQueue() {
}

void InstanceTaskQueue::StartTimer(int time) {
}

void InstanceTaskQueue::StopTimer() {
}

bool InstanceTaskQueue::OnTimerTimeout() {
    return true;
}

void InstanceTaskQueue::TimerErrorHandler(const std::string &name, std::string error) {
}

void InstanceTaskQueue::Clear() {
}
