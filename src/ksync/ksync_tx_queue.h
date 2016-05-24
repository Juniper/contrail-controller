/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

// ksync_tx_queue.h
//
// Implmentation of a shared transmit queue between agent and KSync. 
//
// KSync i/o operations are done on KSync Netlink socket. Even if the socket is
// set to non-blocking mode, the KSync socket i/o call will block till VRouter
// completes message processing. So, netlink i/o is not done in agent context.
// All messages are enqueued to a transmit and messages and i/o is done thru
// the transmit queue. This ensures agent doesnt block due to ksync processing
//
// There are two implementations of transmit queue
//
// WorkQueue based
// ---------------
// WorkQueue based implementation is used only for UT cases. WorkQueue based
// implementation is not used in production enviroment due to performance
// reasons. When we have slow producer and fast consumer, WorkQueue will
// result in spawning of a "task" for every message. The task spawning happens
// in producer context. In case of flow processing, agent is slow producer and
// vrouter is fast consumer. As a result, using WorkQueue will result in many
// task spawns thereby introducing latencies.
//
// We retain WorkQueue based implementations since UT code depends on
// WaitForIdle() APIs to continue with validations. Using Event-FD based
// implementations cannot rely on WaitForIdle()
//
// Event-FD based
// --------------
// This implementation is based on event_fd. The producer will add the entry
// into a tbb::concurrent_queue and notify the consumer on an event_fd. The
// consumer will block with "read" on event_fd. On getting an event, it will
// drain the queue of all messages in a tight loop. Consumer blocks on "read"
// when there is no data in the queue. This is an efficient implementation of
// queue between agent and ksync
//
#ifndef controller_src_ksync_ksync_tx_queue_h
#define controller_src_ksync_ksync_tx_queue_h

#include <sys/eventfd.h>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <set>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
class KSyncSock;
class IoContext;

class KSyncTxQueue {
public:
    typedef tbb::concurrent_queue<IoContext *> Queue;

    KSyncTxQueue(KSyncSock *sock);
    ~KSyncTxQueue();

    void Init(bool use_work_queue);
    void Shutdown();
    bool Run();

    size_t enqueues() const { return enqueues_; }
    size_t dequeues() const { return dequeues_; }
    uint32_t write_events() const { return write_events_; }
    uint32_t read_events() const { return read_events_; }
    size_t queue_len() const { return queue_len_; }
    uint64_t busy_time() const { return busy_time_; }
    uint32_t max_queue_len() const { return max_queue_len_; }
    void set_measure_busy_time(bool val) const { measure_busy_time_ = val; }
    void ClearStats() const {
        max_queue_len_ = 0;
        enqueues_ = 0;
        dequeues_ = 0;
        busy_time_ = 0;
        read_events_ = 0;
    }

    bool Enqueue(IoContext *io_context) {
        return EnqueueInternal(io_context);
    }

private:
    bool EnqueueInternal(IoContext *io_context);

    WorkQueue<IoContext *> *work_queue_;
    int event_fd_;
    KSyncSock *sock_;
    Queue queue_;
    tbb::atomic<bool> shutdown_;
    pthread_t event_thread_;
    tbb::atomic<size_t> queue_len_;
    mutable size_t max_queue_len_;

    mutable size_t enqueues_;
    mutable size_t dequeues_;
    mutable size_t write_events_;
    mutable size_t read_events_;
    mutable uint64_t busy_time_;
    mutable bool measure_busy_time_;

    DISALLOW_COPY_AND_ASSIGN(KSyncTxQueue);
};

#endif  // controller_src_ksync_ksync_tx_queue_h
