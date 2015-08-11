/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_FLOW_TABLE_H__
#define __AGENT_FLOW_TABLE_H__

#include <map>
#if defined(__GNUC__)
#include "base/compiler.h"
#if __GNUC_PREREQ(4, 5)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#endif
#include <boost/uuid/random_generator.hpp>
#if defined(__GNUC__) && __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include <boost/uuid/uuid_io.hpp>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>
#include <tbb/mutex.h>
#include <base/util.h>
#include <net/address.h>
#include <db/db_table_walker.h>
#include <cmn/agent_cmn.h>
#include <oper/mirror_table.h>
#include <filter/traffic_action.h>
#include <filter/acl_entry.h>
#include <filter/acl.h>
#include <pkt/pkt_types.h>
#include <pkt/pkt_handler.h>
#include <pkt/pkt_init.h>
#include <pkt/pkt_flow_info.h>
#include <pkt/flow_entry.h>
#include <sandesh/sandesh_trace.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <filter/acl.h>
#include <sandesh/common/flow_types.h>

class FlowStatsCollector;
class PktSandeshFlow;
class FetchFlowRecord;
struct VmFlowInfo;
class FlowEntry;
class FlowTable;
class FlowTableKSyncEntry;
class FlowMgmtResponse;

struct FlowTaskMsg : public InterTaskMsg {
    FlowTaskMsg(FlowEntry * fe) : InterTaskMsg(0), fe_ptr(fe) { }
    virtual ~FlowTaskMsg() { }

    FlowEntryPtr fe_ptr;
};

struct Inet4FlowKeyCmp {
    bool operator()(const FlowKey &lhs, const FlowKey &rhs) {
        const FlowKey &lhs_base = static_cast<const FlowKey &>(lhs);
        return lhs_base.IsLess(rhs);
    }
};

class FlowTable {
public:
    static const std::string kTaskName;
    static const int MaxResponses = 100;
    static boost::uuids::random_generator rand_gen_;

    typedef std::map<FlowKey, FlowEntry *, Inet4FlowKeyCmp> FlowEntryMap;
    typedef std::map<const VmEntry *, VmFlowInfo *> VmFlowTree;
    typedef std::pair<const VmEntry *, VmFlowInfo *> VmFlowPair;
    typedef boost::function<bool(FlowEntry *flow)> FlowEntryCb;

    FlowTable(Agent *agent);
    virtual ~FlowTable();

    void Init();
    void InitDone();
    void Shutdown();

    // Table managment routines
    FlowEntry *Allocate(const FlowKey &key);
    void Add(FlowEntry *flow, FlowEntry *rflow);
    FlowEntry *Find(const FlowKey &key);
    bool Delete(const FlowKey &key, bool del_reverse_flow);
    void DeleteAll();
    // Test code only used method
    void DeleteFlow(const AclDBEntry *acl, const FlowKey &key,
                    AclEntryIDList &id_list);
    bool ValidFlowMove(const FlowEntry *new_flow,
                       const FlowEntry *old_flow) const;

    // VM/VN flow info routines
    uint32_t max_vm_flows() const { return max_vm_flows_; }
    void set_max_vm_flows(uint32_t num_flows) { max_vm_flows_ = num_flows; }
    uint32_t VmFlowCount(const VmEntry *vm);
    uint32_t VmLinkLocalFlowCount(const VmEntry *vm);

    void VnFlowCounters(const VnEntry *vn, uint32_t *in_count, 
                        uint32_t *out_count);
    // Accessor routines
    Agent *agent() const { return agent_; }
    size_t Size() { return flow_entry_map_.size(); }
    uint32_t linklocal_flow_count() const { return linklocal_flow_count_; }
    FlowTable::FlowEntryMap::iterator begin() {
        return flow_entry_map_.begin();
    }
    FlowTable::FlowEntryMap::iterator end() {
        return flow_entry_map_.end(); 
    }

    static const std::string &TaskName() { return kTaskName; }
    // Sandesh routines
    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                               const int last_count);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, 
                           int ace_id);
   
    void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    virtual void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);

    void RevaluateFlow(FlowEntry *flow);
    void DeleteMessage(FlowEntry *flow);

    void RevaluateInterface(FlowEntry *flow);
    void RevaluateVn(FlowEntry *flow);
    void RevaluateAcl(FlowEntry *flow);
    void RevaluateNh(FlowEntry *flow);
    void DeleteVrf(VrfEntry *vrf);
    void RevaluateRoute(FlowEntry *flow, const AgentRoute *route);
    bool FlowResponseHandler(const FlowMgmtResponse *resp);

    bool FlowRouteMatch(const InetUnicastRouteEntry *rt, uint32_t vrf,
                        Address::Family family, const IpAddress &ip,
                        uint8_t plen);
    bool FlowInetRpfMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowInetSrcMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowInetDstMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowBridgeSrcMatch(FlowEntry *flow, const BridgeRouteEntry *rt);
    bool FlowBridgeDstMatch(FlowEntry *flow, const BridgeRouteEntry *rt);
    bool RevaluateSgList(FlowEntry *flow, const AgentRoute *rt,
                         const SecurityGroupList &sg_list);
    bool RevaluateRpfNH(FlowEntry *flow, const AgentRoute *rt);

    // Update flow port bucket information
    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);

    void UpdateKSync(FlowEntry *flow);

    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class FetchFlowRecord;
    friend class PktFlowInfo;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:
    Agent *agent_;
    FlowEntryMap flow_entry_map_;

    VmFlowTree vm_flow_tree_;
    uint32_t max_vm_flows_;     // maximum flow count allowed per vm
    uint32_t linklocal_flow_count_;  // total linklocal flows in the agent

    std::string GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl,
                                         const int last_count);

    void DeleteInternal(FlowEntryMap::iterator &it);
    void ResyncAFlow(FlowEntry *fe);
    void DeleteFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm);
    void DeleteVmFlows(const VmEntry *vm);

    void AddFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe, const VmEntry *vm);

    void SendFlows(FlowEntry *flow, FlowEntry *rflow);
    void SendFlowInternal(FlowEntry *fe);

    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);
    void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow);
    void SetUnderlayInfo(FlowEntry *flow, FlowDataIpv4 &s_flow);
    bool SetUnderlayPort(FlowEntry *flow, FlowDataIpv4 &s_flow);

    DISALLOW_COPY_AND_ASSIGN(FlowTable);
};

struct FlowEntryCmp {
    bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
        FlowEntry *lhs = l.get();
        FlowEntry *rhs = r.get();

        return (lhs < rhs);
    }
};

typedef std::set<FlowEntryPtr, FlowEntryCmp> FlowEntryTree;
struct VmFlowInfo {
    VmFlowInfo() : linklocal_flow_count() {}
    ~VmFlowInfo() {}

    VmEntryConstRef vm_entry;
    FlowEntryTree fet;
    uint32_t linklocal_flow_count;
};

extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);
extern void GetFlowSandeshActionParams(const FlowAction &, std::string &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
