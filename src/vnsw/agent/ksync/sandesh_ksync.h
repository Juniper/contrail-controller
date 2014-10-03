/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "vr_types.h"

#include "ksync/agent_ksync_types.h"
#include "ksync/ksync_index.h"
#include "ksync/ksync_entry.h"
#include "ksync/ksync_object.h"
#include "ksync/ksync_netlink.h"
#include "ksync/ksync_sock.h"
#include <ksync/flowtable_ksync.h>

/* Sandesh Context used for processing vr_response and vr_flow_req
 * which is in response to Sandesh Adds/Deletes
 */
class KSyncSandeshContext : public AgentSandeshContext {
public:
    KSyncSandeshContext(FlowTableKSyncObject *obj) : flow_ksync_(obj) { 
        Reset(); 
    }

    virtual void IfMsgHandler(vr_interface_req *req);
    virtual void NHMsgHandler(vr_nexthop_req *req) {
        assert(0);
    }
    virtual void RouteMsgHandler(vr_route_req *req) {
        assert(0);
    }
    virtual void MplsMsgHandler(vr_mpls_req *req) {
        assert(0);
    }
    virtual void MirrorMsgHandler(vr_mirror_req *req) {
        assert(0);
    }
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req) {
        assert(0);
    }
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req) {
        assert(0);
    }
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req) {
        assert(0);
    }
    virtual void VxLanMsgHandler(vr_vxlan_req *req) {
        assert(0);
    }
    virtual int VrResponseMsgHandler(vr_response *r);
    virtual void FlowMsgHandler(vr_flow_req *r);
    
    int response_code() const { return response_code_; }
    int context_marker() const { return context_marker_; }
    void Reset() {
        response_code_ = 0;
        context_marker_ = -1;
    }
private:
    FlowTableKSyncObject *flow_ksync_;
    int response_code_;
    int context_marker_;
    DISALLOW_COPY_AND_ASSIGN(KSyncSandeshContext);
};
