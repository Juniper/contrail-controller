#ifndef vnsw_agent_stats_sandesh_context_h
#define vnsw_agent_stats_sandesh_context_h

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include "vr_message.h"
#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"
#include <assert.h>

class AgentStatsCollector;

class AgentStatsSandeshContext: public AgentSandeshContext {
public:
    AgentStatsSandeshContext(AgentStatsCollector *col) ;
    virtual ~AgentStatsSandeshContext();

    AgentStatsCollector *collector() { return collector_; }
    int marker_id() const { return marker_id_; }
    void set_marker_id(int id) { marker_id_ = id; }
    void set_response_code(int value) { response_code_ = value; }
    bool MoreData() const;

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
    virtual void VxLanMsgHandler(vr_vxlan_req *req) {
        assert(0);
    }
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req);
    /* Vr-response is expected from kernel and mock code.
     * For each dump response we get vr_response with negative 
     * value for errors and positive value (indicating number of 
     * entries being sent) for success.
     */
    virtual int VrResponseMsgHandler(vr_response *r);
    virtual void FlowMsgHandler(vr_flow_req *req) {
        assert(0);
    }
private:
    AgentStatsCollector *collector_;
    int marker_id_;
    int response_code_;
    DISALLOW_COPY_AND_ASSIGN(AgentStatsSandeshContext);
};

#endif //vnsw_agent_stats_sandesh_context_h
