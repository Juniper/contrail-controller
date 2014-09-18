/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_kstate_h
#define vnsw_agent_kstate_h

#include <cmn/agent_cmn.h>
#include <vr_types.h>
#include <vr_flow.h>
#include <kstate/kstate_types.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_types.h>
#include <ksync/agent_ksync_types.h>

class KState : public AgentSandeshContext {
public:
    static const int kMaxEntriesPerResponse = 100;
    KState(const std::string &s, Sandesh *obj) : response_context_(s), 
        response_object_(obj), vr_response_code_(0), more_context_(NULL) {}

    void EncodeAndSend(Sandesh &encoder);
    virtual void SendResponse() = 0;
    virtual void SendNextRequest() = 0;
    virtual void Handler() = 0;
    virtual void Release() {
        if (response_object_) {
            response_object_->Release();
            response_object_ = NULL;
        }
    }
    const std::string response_context() const { return response_context_; }
    Sandesh *response_object() const { return response_object_; }
    void *more_context() const { return more_context_; }
    void set_vr_response_code(int value) { vr_response_code_ = value; }
    bool MoreData() const;
    virtual void IfMsgHandler(vr_interface_req *req);
    virtual void NHMsgHandler(vr_nexthop_req *req);
    virtual void RouteMsgHandler(vr_route_req *req);
    virtual void MplsMsgHandler(vr_mpls_req *req);
    virtual void MirrorMsgHandler(vr_mirror_req *req);
    virtual int VrResponseMsgHandler(vr_response *r);
    virtual void FlowMsgHandler(vr_flow_req *req) {}
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req);
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req);
    virtual void VxLanMsgHandler(vr_vxlan_req *req);
protected:
    std::string response_context_;
    Sandesh *response_object_;
    int vr_response_code_; /* response code from kernel */
    void *more_context_; /* context to hold marker info */
private:
    void UpdateContext(void *);
    const std::string PrefixToString(const std::vector<int8_t> &prefix);
};

#endif // vnsw_agent_kstate_h
