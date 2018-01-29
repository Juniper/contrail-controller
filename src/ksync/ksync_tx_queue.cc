/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/eventfd.h>

#include <algorithm>
#include <vector>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/thread.hpp>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>

#include "ksync_object.h"
#include "ksync_sock.h"

static bool ksync_tx_queue_task_done_ = false;

// Set CPU affinity for KSync Tx Thread based on cpu_pin_policy.
// By default CPU affinity is not set. cpu_pin_policy can change it,
//    "last"  : Last CPU-ID
//    "<num>" : Specifies CPU-ID to pin
static void set_thread_affinity(std::string cpu_pin_policy) {
    int num_cores = boost::thread::hardware_concurrency();
    if (!num_cores) {
        LOG(ERROR, "Failure in checking number of available threads");
        num_cores = 1;
    }
    char *p = NULL;
    int cpu_id = strtoul(cpu_pin_policy.c_str(), &p, 0);
    if (*p || cpu_pin_policy.empty()) {
        // cpu_pin_policy is non-integer
        // Assume pinning disabled by default
        cpu_id = -1;
        // If policy is "last", pick last CPU-ID
        boost::algorithm::to_lower(cpu_pin_policy);
        if (cpu_pin_policy == "last") {
            cpu_id = num_cores - 1;
        }
    } else {
        // cpu_pin_policy is integer
        // Disable pinning if configured value out of range
        if (cpu_id >= num_cores)
            cpu_id = -1;
    }

    if (cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        LOG(ERROR, "KsyncTxQueue CPU pinning policy <" << cpu_pin_policy
            << ">. KsyncTxQueue pinned to CPU " << cpu_id);
        sched_setaffinity(0, sizeof(cpuset), &cpuset);
    } else {
        LOG(ERROR, "KsyncTxQueue CPU pinning policy <" << cpu_pin_policy
            << ">. KsyncTxQueuen not pinned to CPU");
    }
}

class KSyncTxQueueTask : public Task {
public:
    KSyncTxQueueTask(TaskScheduler *scheduler, KSyncTxQueue *queue) :
        Task(scheduler->GetTaskId("Ksync::KSyncTxQueue"), 0), queue_(queue) {
    }
    ~KSyncTxQueueTask() {
        ksync_tx_queue_task_done_ = true;
    }

    bool Run() {
        queue_->Run();
        return true;
    }
    std::string Description() const { return "KSyncTxQueue"; }

private:
    KSyncTxQueue *queue_;
};

KSyncTxQueue::KSyncTxQueue(KSyncSock *sock) :
    work_queue_(NULL),
    event_fd_(-1),
    cpu_pin_policy_(),
    sock_(sock),
    enqueues_(0),
    dequeues_(0),
    write_events_(0),
    read_events_(0),
    busy_time_(0),
    measure_busy_time_(false) {
    queue_len_ = 0;
    shutdown_ = false;
    ClearStats();
}

KSyncTxQueue::~KSyncTxQueue() {
}

void KSyncTxQueue::Init(bool use_work_queue,
                        const std::string &cpu_pin_policy) {
    cpu_pin_policy_ = cpu_pin_policy;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    if (use_work_queue) {
        assert(work_queue_ == NULL);
        work_queue_ = new WorkQueue<IoContext *>
            (scheduler->GetTaskId("Ksync::AsyncSend"), 0,
             boost::bind(&KSyncSock::SendAsyncImpl, sock_, _1));
        work_queue_->SetExitCallback
            (boost::bind(&KSyncSock::OnEmptyQueue, sock_, _1));
        return;
    }
    assert((event_fd_ = eventfd(0, (EFD_CLOEXEC | EFD_SEMAPHORE))) >= 0);

    KSyncTxQueueTask *task = new KSyncTxQueueTask(scheduler, this);
    scheduler->Enqueue(task);
}

void KSyncTxQueue::Shutdown() {
    shutdown_ = true;
    if (work_queue_) {
        assert(work_queue_->Length() == 0);
        work_queue_->Shutdown();
        delete work_queue_;
        work_queue_ = NULL;
        return;
    }

    uint64_t u = 1;
    assert(write(event_fd_, &u, sizeof(u)) == sizeof(u));
    while (queue_len_ != 0) {
        usleep(1);
    }

    while(ksync_tx_queue_task_done_ != true) {
        usleep(1);
    }
    close(event_fd_);
}

bool KSyncTxQueue::EnqueueInternal(IoContext *io_context) {
    if (work_queue_) {
        work_queue_->Enqueue(io_context);
        return true;
    }
    queue_.push(io_context);
    enqueues_++;
    size_t ncount = queue_len_.fetch_and_increment() + 1;
    if (ncount > max_queue_len_)
        max_queue_len_ = ncount;
    if (ncount == 1) {
        uint64_t u = 1;
        int res = 0;
        while ((res = write(event_fd_, &u, sizeof(u))) < (int)sizeof(u)) {
            int ec = errno;
            if (ec != EINTR && ec != EIO) {
                LOG(ERROR, "KsyncTxQueue write failure : " << ec << " : "
                    << strerror(ec));
                assert(0);
            }
        }

        write_events_++;
    }
    return true;
}

bool KSyncTxQueue::Run() {
    set_thread_affinity(cpu_pin_policy_);
    while (1) {
        while (1) {
            uint64_t u = 0;
            ssize_t num = read(event_fd_, &u, sizeof(u));
            if (num >= (int)sizeof(u)) {
                break;
            }
            if (errno != EINTR && errno != EIO) {
                LOG(ERROR, "KsyncTxQueue read failure : " << errno << " : "
                    << strerror(errno));
                assert(0);
            }
        }
        read_events_++;

        uint64_t t1 = 0;
        if (measure_busy_time_)
            t1 = ClockMonotonicUsec();
        IoContext *io_context = NULL;
        while (queue_.try_pop(io_context)) {
            dequeues_++;
            queue_len_ -= 1;
            sock_->SendAsyncImpl(io_context);
        }
        sock_->OnEmptyQueue(false);
        if (shutdown_) {
            break;
        }

        if (t1)
            busy_time_ += (ClockMonotonicUsec() - t1);
    }
    return true;
}
