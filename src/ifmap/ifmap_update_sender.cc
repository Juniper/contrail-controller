/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update_sender.h"
#include "base/task.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"

using namespace std;

IFMapUpdateSender::IFMapUpdateSender(IFMapServer *server,
                                     IFMapUpdateQueue *queue)
    : server_(server), queue_(queue), message_(new IFMapMessage()),
      task_scheduled_(false), queue_active_(false) {
}

IFMapUpdateSender::~IFMapUpdateSender() {
    delete(message_);
}

class IFMapUpdateSender::SendTask : public Task {
public:
    explicit SendTask(IFMapUpdateSender *sender)
        : Task(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"), 0),
          sender_(sender) {
    }
    virtual bool Run() {
        BitSet send_scheduled;
        sender_->GetSendScheduled(&send_scheduled);
        sender_->send_blocked_.Reset(send_scheduled);
        for (size_t i = send_scheduled.find_first(); i != BitSet::npos;
             i = send_scheduled.find_next(i)) {
            // Dequeue from client marker (i).
            IFMAP_UPD_SENDER_TRACE(IFMapUSSendScheduled, "Send scheduled for",
                send_scheduled.ToNumberedString(), "client", i,
                sender_->queue_->GetMarker(i)->ToString());
            sender_->Send(sender_->queue_->GetMarker(i));
        }
        if (sender_->queue_active_) {
            // Dequeue from tail marker.
            // Reset queue_active_
            IFMAP_UPD_SENDER_TRACE(IFMapUSQueueActive, "Queue active for",
                sender_->queue_->tail_marker()->ToString());
            sender_->Send(sender_->queue_->tail_marker());
            sender_->queue_active_ = false;
        }
        return true;
    }

    std::string Description() const { return "IFMapUpdateSender::SendTask"; }
private:
    IFMapUpdateSender *sender_;
};

void IFMapUpdateSender::StartTask() {
    if (!task_scheduled_) {
        // create new task
        SendTask *send_task = new SendTask(this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(send_task);
        task_scheduled_ = true;
    }
}

void IFMapUpdateSender::QueueActive() {
    if (queue_active_) {
        return;
    }
    queue_active_ = true;
    tbb::mutex::scoped_lock lock(mutex_);
    StartTask();
}

void IFMapUpdateSender::SendActive(int index) {
    tbb::mutex::scoped_lock lock(mutex_);
    send_scheduled_.set(index);
    StartTask();
}

void IFMapUpdateSender::GetSendScheduled(BitSet *current) {
    tbb::mutex::scoped_lock lock(mutex_);
    *current = send_scheduled_;
    send_scheduled_.clear();
    task_scheduled_ = false;
}

void IFMapUpdateSender::CleanupClient(int index) {
    tbb::mutex::scoped_lock lock(mutex_);
    send_scheduled_.reset(index);
    send_blocked_.reset(index);
}

// We return only under 2 conditions:
// 1. All the clients in the marker are blocked.
// 2. We have finished traversing the Q.
// Invariant: while we are traversing the Q, the marker that we are working
// with only has ready clients. As soon as a client blocks, we split it out and
// continue with the ready set.
void IFMapUpdateSender::Send(IFMapMarker *imarker) {
    IFMapMarker *marker = imarker;

    // Get the clients in this marker that are blocked. If all of the clients in
    // this marker are blocked, we are done.
    BitSet blocked_clients;
    blocked_clients = (marker->mask & send_blocked_);
    if (blocked_clients == marker->mask) {
        return;
    }

    // If any of the clients are blocked, create a new marker for the set of
    // blocked clients, insert it before marker and continue with the ready
    // set.
    if (!blocked_clients.empty()) {
        IFMAP_UPD_SENDER_TRACE(IFMapUSSplitBlocked, "Splitting blocked clients",
            blocked_clients.ToNumberedString(), "from", marker->ToString());
        queue_->MarkerSplitBefore(marker, marker, blocked_clients);
    }

    IFMapListEntry *next = queue_->Next(marker);
    BitSet base_send_set;

    // Start with the node after the 'marker'
    for (IFMapListEntry *curr = next; curr != NULL; curr = next) {
        next = queue_->Next(curr);

        if (curr->IsMarker()) {
            IFMapMarker *next_marker = static_cast<IFMapMarker *>(curr);
            // Processing the next_marker can change the send_set and all
            // clients in the next_marker should have already seen the updates
            // currently sitting in the buffer. So, flush the buffer to the
            // existing client-set before processing the marker so that we dont
            // send duplicates.
            if (!message_->IsEmpty()) {
                BitSet blocked_set;
                SendUpdate(base_send_set, &blocked_set);
            }
            bool done;
            marker = ProcessMarker(marker, next_marker, &done);
            if (done) {
                // All the clients in this marker are blocked. We are done.
                return;
            }
            // marker has the ready clients. Continue as if we are starting
            // fresh.
            base_send_set.clear();
            continue;
        }

        // ...else its an update or delete
 
        IFMapUpdate *update = static_cast<IFMapUpdate *>(curr);
        BitSet send_set = update->advertise() & marker->mask;
        if (send_set.empty()) {
            continue;
        }

        if (base_send_set.empty()) {
            base_send_set = send_set;
        }

        // Flush the message to all possible clients if:
        // 1. The buffer is full OR
        // 2. The send_set is changing and buffer is filled.
        if (message_->IsFull() ||
            ((base_send_set != send_set) && !message_->IsEmpty())) {

            BitSet blocked_set;
            SendUpdate(base_send_set, &blocked_set);
            if (!blocked_set.empty()) {
                // All the clients in this marker are blocked. We are done.
                if (blocked_set == marker->mask) {
                    IFMAP_UPD_SENDER_TRACE(IFMapUSAllBlocked, marker->ToString(),
                        "blocked before", curr->ToString());
                    queue_->MoveMarkerBefore(marker, curr);
                    return;
                }
                // Only a subset of clients in this marker are blocked. Insert
                // a marker for them 'before' curr since they have seen
                // everything before curr. Let the ready clients continue the
                // traversal.
                IFMAP_UPD_SENDER_TRACE(IFMapUSSubsetBlocked, "Clients",
                    blocked_set.ToNumberedString(), "blocked before",
                    curr->ToString(), "and split from", marker->ToString());
                queue_->MarkerSplitBefore(marker, curr, blocked_set);
                send_set.Reset(blocked_set);
            }

            // The send_set for this marker is changing. Pick up the new one.
            base_send_set = send_set;
        }

        // base_send_set is same as send_set at this point.
        ProcessUpdate(update, base_send_set);
    }

    // The buffer will be filled in the common case of updates being added
    // after the tail_marker.
    BitSet blk_set;
    if (!message_->IsEmpty()) {
        SendUpdate(base_send_set, &blk_set);
    }
    // If the last node in the Q was the tail_marker, we would have already
    // flushed the buffer and merged with it and we would be the last node in
    // the Q.
    IFMapListEntry *last = queue_->GetLast();
    if (marker != last) {
        // Since we have reached the end of the Q, we better be the tail_marker
        assert(marker == queue_->tail_marker());
        // If we have any blocked clients, splitting markers for them is not
        // useful at this point. Just move the marker to the end of the Q,
        // immediately after last, even if it has blocked clients. Being lazy
        // is advantageous since by the time we get the next trigger, a blocked
        // client could have become ready and splitting the marker now would be
        // useless.
        IFMAP_UPD_SENDER_TRACE(IFMapUSMoveAfterLast, "Moving", marker->ToString(),
            "before", last->ToString(), "with blocked_set",
            blk_set.ToNumberedString());
        queue_->MoveMarkerAfter(marker, last);
    }
    return;
}

void IFMapUpdateSender::ProcessUpdate(IFMapUpdate *update,
                                      const BitSet &base_send_set) {
    LogAndCountSentUpdate(update, base_send_set);

    // Append the contents of the update-node to the message.
    message_->EncodeUpdate(update);

    // Clean up the node if everybody has seen it.
    update->AdvertiseReset(base_send_set);
    if (update->advertise().empty()) {
        queue_->Dequeue(update);
    }
    // Update may be freed.
    server_->exporter()->StateUpdateOnDequeue(update, base_send_set,
                                              update->IsDelete());
}

// blocked_set is a subset of send_set
void IFMapUpdateSender::SendUpdate(BitSet send_set, BitSet *blocked_set) {
    IFMapClient *client;
    bool send_result;

    assert(!message_->IsEmpty());

    for (size_t i = send_set.find_first(); i != BitSet::npos;
         i = send_set.find_next(i)) {
        assert(!send_blocked_.test(i));
        client = server_->GetClient(i);
        assert(client);

        message_->SetReceiverInMsg(client->identifier());
        // Close the message to save the document as string
        message_->Close();

        // Send the string version of the message to the client.
        send_result = client->SendUpdate(message_->get_string());

        // Keep track of all the clients whose buffers are full. 
        if (!send_result) {
            blocked_set->set(i);
            send_blocked_.set(i);
        }
    }
    // Reset the message to init things for the next message
    message_->Reset();
}

// marker is before next_marker in the Q. next_marker could be the tail_marker.
// 'done' is set to true only if all the clients in the union of the
// client-sets of the 2 markers are blocked.
IFMapMarker* IFMapUpdateSender::ProcessMarker(IFMapMarker *marker,
                                              IFMapMarker *next_marker,
                                              bool *done) {
    // There should never be a marker beyond the tail_marker
    assert(marker != queue_->tail_marker());

    // Get the union (total_set) of the client-sets in the 2 markers. Then, get
    // the subset of clients in the union that are blocked (blocked_set). The
    // remaining subset of clients are ready (ready_set).
    BitSet total_set = (marker->mask | next_marker->mask);
    BitSet blocked_set = (total_set & send_blocked_);
    BitSet ready_set;
    ready_set.BuildComplement(total_set, blocked_set); // *this = lhs & ~rhs

    // If all the clients are ready or all are blocked, merge marker into
    // next_marker. marker will be deleted.
    if (blocked_set.empty() || ready_set.empty()) {
        IFMAP_UPD_SENDER_TRACE(IFMapUSMarkerMerge, "Merging", marker->ToString(),
            "into", next_marker->ToString());
        queue_->MarkerMerge(next_marker, marker, marker->mask);
        assert(next_marker->mask == total_set);
    } else {
        // We have both, ready and blocked, clients. First, merge both the
        // markers into next_marker so that next_marker has the total_set. Then
        // split next_marker into 2 markers: first with the blocked_set and the
        // second with the ready_set, with first(blocked) preceding the
        // second(ready).
        IFMAP_UPD_SENDER_TRACE(IFMapUSMarkerMerge, "Merging", marker->ToString(),
            "into", next_marker->ToString());
        queue_->MarkerMerge(next_marker, marker, marker->mask);
        assert(next_marker->mask == total_set);
        IFMAP_UPD_SENDER_TRACE(IFMapUSMarkerSplit, "Splitting blocked clients",
            blocked_set.ToNumberedString(), "from", next_marker->ToString());
        queue_->MarkerSplitBefore(next_marker, next_marker, blocked_set);
    }
    if (ready_set.empty()) {
        // If all the clients are blocked, we are done.
        *done = true;
    } else {
        // Atleast some clients are ready to continue.
        *done = false;
    }

    // next_marker has the ready_set if done is false
    return next_marker;
}

void IFMapUpdateSender::LogAndCountSentUpdate(IFMapUpdate *update,
                                              const BitSet &base_send_set) {
    size_t total = base_send_set.count();
    // Avoid dealing with return value of BitSet::npos
    if (total) {
        string name = update->ConfigName();
        string operation = update->TypeToString();
        size_t client_id = base_send_set.find_first();
        while (total--) {
            IFMapClient *client = server_->GetClient(client_id);
            if (client) {
                IFMAP_DEBUG_ONLY(IFMapClientSendInfo, operation, name,
                                 client->identifier(), client->name());
                if (update->IsNode()) {
                    if (update->IsUpdate()) {
                        client->incr_update_nodes_sent();
                    } else if (update->IsDelete()) {
                        client->incr_delete_nodes_sent();
                    } else {
                        assert(0);
                    }
                } else if (update->IsLink()) {
                    if (update->IsUpdate()) {
                        client->incr_update_links_sent();
                    } else if (update->IsDelete()) {
                        client->incr_delete_links_sent();
                    } else {
                        assert(0);
                    }
                }
            }
            client_id = base_send_set.find_next(client_id);
        }
    }
}

