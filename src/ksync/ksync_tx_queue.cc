/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <sys/eventfd.h>
#include <algorithm>
#include <vector>
#include <set>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>

#include "ksync_object.h"
#include "ksync_sock.h"

static bool ksync_tx_queue_task_done_ = false;
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
    sock_(sock),
    enqueues_(0),
    dequeues_(0),
    write_events_(0),
    read_events_(0),
    busy_time_(0),
    measure_busy_time_(false) {
    queue_len_ = 0;
    shutdown_ = false;
}

KSyncTxQueue::~KSyncTxQueue() {
}

void KSyncTxQueue::Init(bool use_work_queue) {
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
    assert((event_fd_ = eventfd(0, (FD_CLOEXEC | EFD_SEMAPHORE))) >= 0);

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
    while (1) {
        uint64_t u = 0;
        ssize_t num = 0;

        while (1) {
            num = read(event_fd_, &u, sizeof(u));
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
