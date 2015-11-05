/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <sys/eventfd.h>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <set>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>

#include "ksync_object.h"
#include "ksync_sock.h"

KSyncTxQueue::KSyncTxQueue(KSyncSock *sock) :
    event_fd_(-1),
    sock_(sock),
    enqueues_(0),
    dequeues_(0),
    write_events_(0) {
    queue_len_ = 0;
    shutdown_ = false;
}

KSyncTxQueue::~KSyncTxQueue() {
    if (work_queue_) {
        //delete work_queue_;
        work_queue_ = NULL;
    }
}

static void *KSyncIoRun(void *arg) {
    KSyncTxQueue *queue = (KSyncTxQueue *)(arg);
    queue->Run();
    return NULL;
}

void KSyncTxQueue::Init(bool use_work_queue) {
    if (use_work_queue) {
        assert(work_queue_ == NULL);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        work_queue_ = new WorkQueue<IoContext *>
            (scheduler->GetTaskId("Ksync::AsyncSend"), 0,
             boost::bind(&KSyncSock::SendAsyncImpl, sock_, _1));
        work_queue_->SetExitCallback
            (boost::bind(&KSyncSock::OnEmptyQueue, sock_, _1));
        return;
    }
    assert((event_fd_ = eventfd(0, (FD_CLOEXEC | EFD_SEMAPHORE))) >= 0);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int ret = pthread_create(&event_thread_, &attr, KSyncIoRun, this);
    if (ret != 0) {
        LOG(ERROR, "pthread_create error : " <<  strerror(ret) );
        assert(0);
    }
}

void KSyncTxQueue::Shutdown() {
    shutdown_ = true;
    if (work_queue_) {
        assert(work_queue_->Length() == 0);
        work_queue_->Shutdown();
        return;
    }

    uint64_t u = 1;
    assert(write(event_fd_, &u, sizeof(u)) == sizeof(u));
    while (queue_len_ != 0) {
        usleep(1);
    }
    pthread_join(event_thread_, NULL);
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
        assert(write(event_fd_, &u, sizeof(u)) == sizeof(u));
        write_events_++;
    }
    return true;
}

bool KSyncTxQueue::Run() {
    while (1) {
        uint64_t u = 0;
        assert(read(event_fd_, &u, sizeof(u)) >= (int)sizeof(u));
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
    }
    return true;
}
