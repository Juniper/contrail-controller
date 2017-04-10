/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_MGMT_REQUEST_H__
#define __AGENT_FLOW_MGMT_REQUEST_H__

#include "pkt/flow_table.h"
#include "pkt/flow_event.h"

////////////////////////////////////////////////////////////////////////////
// Request to the Flow Management module
////////////////////////////////////////////////////////////////////////////
class FlowMgmtRequest {
public:
    enum Event {
        INVALID,
        ADD_FLOW,
        DELETE_FLOW,
        UPDATE_FLOW,
        ADD_DBENTRY,
        CHANGE_DBENTRY,
        DELETE_DBENTRY,
        RETRY_DELETE_VRF,
        DELETE_BGP_AAS_FLOWS,
        UPDATE_FLOW_STATS,
        DUMMY

    };

    FlowMgmtRequest(Event event, FlowEntry *flow) :
        event_(event), flow_(flow), db_entry_(NULL), vrf_id_(0), gen_id_(),
        bytes_(), packets_(), oflow_bytes_(), params_() {
            if (event == RETRY_DELETE_VRF)
                assert(vrf_id_);
    }

    FlowMgmtRequest(Event event, FlowEntry *flow,
                    const RevFlowDepParams &params) :
        event_(event), flow_(flow), db_entry_(NULL), vrf_id_(0), gen_id_(),
        bytes_(), packets_(), oflow_bytes_(), params_(params) {
    }

    FlowMgmtRequest(Event event, FlowEntry *flow, uint32_t bytes,
                    uint32_t packets, uint32_t oflow_bytes,
                    const boost::uuids::uuid &u) :
        event_(event), flow_(flow), db_entry_(NULL), vrf_id_(0), gen_id_(),
        bytes_(bytes), packets_(packets), oflow_bytes_(oflow_bytes), params_(),
        flow_uuid_(u) {
            if (event == RETRY_DELETE_VRF)
                assert(vrf_id_);
    }

    FlowMgmtRequest(Event event, const DBEntry *db_entry, uint32_t gen_id) :
        event_(event), flow_(NULL), db_entry_(db_entry), vrf_id_(0),
        gen_id_(gen_id), bytes_(), packets_(), oflow_bytes_(), params_() {
            if (event == RETRY_DELETE_VRF) {
                const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(db_entry);
                assert(vrf);
                vrf_id_ = vrf->vrf_id();
            }
    }

    FlowMgmtRequest(Event event) :
        event_(event), flow_(NULL), db_entry_(NULL), vrf_id_(),
        gen_id_(), bytes_(), packets_(), oflow_bytes_(), params_() {
    }

    virtual ~FlowMgmtRequest() { }

    // At the end of Flow Management Request, we may enqueue a response message
    // back to FlowTable module. Compute the message type to be enqueued in
    // response. Returns INVALID if no message to be enqueued
    FlowEvent::Event GetResponseEvent() const {
        FlowEvent::Event resp_event = FlowEvent::INVALID;
        if (event_ == DELETE_BGP_AAS_FLOWS)
            return FlowEvent::DELETE_FLOW;

        if (db_entry_ == NULL)
            return resp_event;

        if (dynamic_cast<const VrfEntry *>(db_entry_)) {
            return resp_event;
        }

        if (event_ == ADD_DBENTRY || event_ == CHANGE_DBENTRY) {
            resp_event = FlowEvent::REVALUATE_DBENTRY;
        } else if (event_ == DELETE_DBENTRY) {
            resp_event = FlowEvent::DELETE_DBENTRY;
        }

        // Add/Change in route needs complete recomputation of flows
        // 1. Bridge route change can be result of MAC-Move. This will need
        //    recomputing rpf-nh also.
        // 2. Add/Delete of inet-uc route can result in change of route used
        //    used for flow
        const AgentRoute *rt =
            dynamic_cast<const AgentRoute *>(db_entry_);
        if (rt) {
            resp_event = FlowEvent::RECOMPUTE_FLOW;
        }

        return resp_event;
    }

    Event event() const { return event_; }
    FlowEntryPtr &flow() { return flow_; }
    void set_flow(FlowEntry *flow) { flow_.reset(flow); }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t vrf_id() const { return vrf_id_; }
    uint32_t gen_id() const { return gen_id_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}
    const RevFlowDepParams& params() const { return params_; }
    void set_params(const RevFlowDepParams &params) {
        params_ = params;
    }
    boost::uuids::uuid flow_uuid() const { return flow_uuid_; }

private:
    Event event_;
    // FlowEntry pointer to hold flow reference till message is processed
    FlowEntryPtr flow_;
    // DBEntry pointer. The DBState from FlowTable module ensures DBEntry is
    // not deleted while message holds pointer
    const DBEntry *db_entry_;
    uint32_t vrf_id_;
    uint32_t gen_id_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;
    RevFlowDepParams params_;
    boost::uuids::uuid flow_uuid_;

    DISALLOW_COPY_AND_ASSIGN(FlowMgmtRequest);
};

class BgpAsAServiceFlowMgmtRequest : public FlowMgmtRequest {
public:
    enum Type {
        VMI,
        CONTROLLER
    };

    BgpAsAServiceFlowMgmtRequest(uint8_t index) :
        FlowMgmtRequest(FlowMgmtRequest::DELETE_BGP_AAS_FLOWS, NULL, 0),
        type_(BgpAsAServiceFlowMgmtRequest::CONTROLLER), vm_uuid_(),
        source_port_(), index_(index) { }
    BgpAsAServiceFlowMgmtRequest(boost::uuids::uuid vm_uuid,
                                 uint32_t source_port) :
        FlowMgmtRequest(FlowMgmtRequest::DELETE_BGP_AAS_FLOWS, NULL, 0),
        type_(BgpAsAServiceFlowMgmtRequest::VMI), vm_uuid_(vm_uuid),
        source_port_(source_port), index_() { }
    virtual ~BgpAsAServiceFlowMgmtRequest() { }
    BgpAsAServiceFlowMgmtRequest::Type type() const { return type_; }
    const boost::uuids::uuid &vm_uuid() const { return vm_uuid_; }
    uint32_t source_port() const { return source_port_; }
    uint8_t index() const { return index_; }

private:
    BgpAsAServiceFlowMgmtRequest::Type type_;
    boost::uuids::uuid vm_uuid_;
    uint32_t source_port_;
    uint8_t index_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtRequest);
};
#endif //  __AGENT_FLOW_MGMT_REQUEST_H__
