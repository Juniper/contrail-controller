/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_segment_health_check_hpp
#define vnsw_agent_segment_health_check_hpp

#include "diag/diag.h"
#include "diag/diag_types.h"
#include "pkt/control_interface.h"

class HealthCheckInstanceService;

class SegmentHealthCheckPkt: public DiagEntry {
public:
    enum Status {
        SUCCESS,
        FAILURE
    };
    static const int kBufferSize = 1024;

    SegmentHealthCheckPkt(HealthCheckInstanceService *service,
                          DiagTable *diag_table);
    virtual ~SegmentHealthCheckPkt();

    virtual void SendRequest();
    void RequestTimedOut(uint32_t seqno);
    virtual void HandleReply(DiagPktHandler *handler);
    virtual void Retry();
    virtual bool IsDone() {
        /* Keep sending health-check packets until this object is removed.
         * We remove this object when an explicit delete request is received
         */
        return false;
    }
    virtual bool ResendOnTimerExpiry() { return false; }
    void set_service(HealthCheckInstanceService *svc) {
        service_ = svc;
    }
    void StopDelayTimer();
    void UpdateService(HealthCheckInstanceService *service);

private:
    void FillDiagHeader(AgentDiagPktData *data) const;
    void Notify(Status status);
    bool RetryHandler();
    int GetTimeout(const HealthCheckInstanceService *svc) const;
    int GetDelay(const HealthCheckInstanceService *svc) const;

    HealthCheckInstanceService *service_;
    Status     state_;
    int        delay_msecs_; //delay/interval in milliseconds
    Timer      *delay_timer_; //
};

#endif
