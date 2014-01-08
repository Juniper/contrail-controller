/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_update_sender__
#define __ctrlplane__ifmap_update_sender__

#include <tbb/mutex.h>
#include "base/bitset.h"
#include "ifmap/ifmap_encoder.h"

struct IFMapMarker;
class  IFMapUpdate;
class IFMapServer;
class IFMapState;
class IFMapUpdate;
class IFMapUpdateQueue;

class IFMapUpdateSender {
public:
    IFMapUpdateSender(IFMapServer *server, IFMapUpdateQueue *queue);
    virtual ~IFMapUpdateSender();

    // events

    // Event posted when the update queue has elements to transmit.
    virtual void QueueActive();

    // Event posted when a particular client is ready to send updates
    // (after previously blocking).
    virtual void SendActive(int index);

    void CleanupClient(int index);

    void SetServer(IFMapServer *srv) { server_ = srv; }

    void SetObjectsPerMessage(int num) {
        message_->SetObjectsPerMessage(num);
    }

    bool IsClientBlocked(int client_index) {
        return send_blocked_.test(client_index);
    }

private:
    class SendTask;
    friend class IFMapUpdateSenderTest;

    void StartTask();

    void Send(IFMapMarker *imarker);

    void SendUpdate(BitSet send_set, BitSet *blocked_set);

    IFMapMarker* ProcessMarker(IFMapMarker *marker, IFMapMarker *next_marker,
                               bool *done); 
    void ProcessUpdate(IFMapUpdate *update, const BitSet &base_send_set);

    void GetSendScheduled(BitSet *current);
    void LogSentUpdate(IFMapUpdate *update, const BitSet &base_send_set);

    IFMapServer *server_;
    IFMapUpdateQueue *queue_;
    IFMapMessage *message_;

    tbb::mutex mutex_;          // protect scheduling of send task
    bool task_scheduled_;
    bool queue_active_;
    BitSet send_scheduled_;     // client-set for which send active was called
    BitSet send_blocked_;       // client-set for clients that are blocked

    void SetSendBlocked(int client_index) {
        send_blocked_.set(client_index);
    }
};

#endif /* defined(__ctrlplane__ifmap_update_sender__) */
