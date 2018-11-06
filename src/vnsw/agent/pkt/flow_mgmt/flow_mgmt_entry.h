/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_MGMT_ENTRY_H__
#define __AGENT_PKT_FLOW_MGMT_ENTRY_H__

#include <string>
#include <map>
#include <boost/intrusive/list.hpp>
#include <base/util.h>
#include <pkt/flow_mgmt/flow_mgmt_key_node.h>
#include <filter/acl_entry.h>

class Agent;
class FlowEntry;
class FlowMgmtManager;
class FlowMgmtRequest;
class VrfFlowMgmtTree;

class FlowMgmtKey;
class InetRouteFlowMgmtKey;
class BgpAsAServiceFlowMgmtKey;
class BgpAsAServiceFlowMgmtRequest;

class AclDBEntry;
class AclFlowResp;
class AclFlowCountResp;

class FlowMgmtEntry {
public:
    // Flow management state of the entry
    enum State {
        INVALID,
        OPER_NOT_SEEN,
        OPER_ADD_SEEN,
        OPER_DEL_SEEN,
    };

    typedef boost::intrusive::member_hook<FlowMgmtKeyNode,
            boost::intrusive::list_member_hook<>,
            &FlowMgmtKeyNode::hook_> Node;
    typedef boost::intrusive::list<FlowMgmtKeyNode, Node> FlowList;

    static const int MaxResponses = 100;

    FlowMgmtEntry() : oper_state_(OPER_NOT_SEEN) {
    }
    virtual ~FlowMgmtEntry() {
        assert(flow_list_.size() == 0);
    }

    uint32_t Size() const { return flow_list_.size(); }
    // Make flow dependent on the DBEntry
    virtual bool Add(FlowEntry *flow, FlowMgmtKeyNode *node);
    // Remove flow from dependency tree
    virtual bool Delete(FlowEntry *flow, FlowMgmtKeyNode *node);

    // Handle Add/Change event for DBEntry
    virtual bool OperEntryAdd(FlowMgmtManager *mgr, const FlowMgmtRequest *req,
                              FlowMgmtKey *key);
    virtual bool OperEntryChange(FlowMgmtManager *mgr,
                                 const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle Delete event for DBEntry
    virtual bool OperEntryDelete(FlowMgmtManager *mgr,
                                 const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle Delete event for Non-DBEntry
    virtual bool NonOperEntryDelete(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key) { return true; }
    // Can the entry be deleted?
    virtual bool CanDelete() const;

    void set_oper_state(State state) { oper_state_ = state; }
    State oper_state() const { return oper_state_; }
    uint32_t gen_id() const { return gen_id_; }
    const FlowList &flow_list() const { return flow_list_; }

protected:
    // Add seen from OperDB entry
    State oper_state_;
    uint32_t gen_id_;
    FlowList flow_list_;

private:
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtEntry);
};

class AclFlowMgmtEntry : public FlowMgmtEntry {
public:
    typedef std::map<std::string, int> AceIdFlowCntMap;
    AclFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~AclFlowMgmtEntry() { }
    void FillAclFlowSandeshInfo(const AclDBEntry *acl, AclFlowResp &data,
                                const int last_count, Agent *agent);
    void FillAceFlowSandeshInfo(const AclDBEntry *acl, AclFlowCountResp &data,
                                const std::string &ace_id);
    bool Add(const AclEntryIDList *ace_id_list, FlowEntry *flow,
             const AclEntryIDList *old_id_list, FlowMgmtKeyNode *node);
    bool Delete(const AclEntryIDList *ace_id_list, FlowEntry *flow,
                FlowMgmtKeyNode *node);
    void DecrementAceIdCountMap(const AclEntryIDList *id_list);

private:
    std::string GetAceSandeshDataKey(const AclDBEntry *acl,
                                     const std::string &ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl,
                                         const int last_count) const;
    uint32_t flow_miss_;
    AceIdFlowCntMap aceid_cnt_map_;
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtEntry);
};

class VnFlowMgmtEntry : public FlowMgmtEntry {
public:
    VnFlowMgmtEntry() :
        FlowMgmtEntry(), ingress_flow_count_(0), egress_flow_count_(0) {
    }
    virtual ~VnFlowMgmtEntry() { }

    void UpdateCounterOnAdd(FlowEntry *flow, bool add_flow, bool local_flow,
                            bool old_ingress);
    void UpdateCounterOnDel(FlowEntry *flow, bool local_flow, bool old_ingress);
    uint32_t ingress_flow_count() const { return ingress_flow_count_; }
    uint32_t egress_flow_count() const { return egress_flow_count_; }

private:
    uint32_t ingress_flow_count_;
    uint32_t egress_flow_count_;
    DISALLOW_COPY_AND_ASSIGN(VnFlowMgmtEntry);
};

class InterfaceFlowMgmtEntry : public FlowMgmtEntry {
public:
    InterfaceFlowMgmtEntry() : FlowMgmtEntry(), flow_created_(0),
        flow_aged_(0) { }
    virtual ~InterfaceFlowMgmtEntry() { }

    bool Add(FlowEntry *flow, FlowMgmtKeyNode *node);
    bool Delete(FlowEntry *flow, FlowMgmtKeyNode *node);
    uint64_t flow_created() const { return flow_created_; }
    uint64_t flow_aged() const { return flow_aged_; }

private:
    uint64_t flow_created_;
    uint64_t flow_aged_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtEntry);
};

class NhFlowMgmtEntry : public FlowMgmtEntry {
public:
    NhFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~NhFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtEntry);
};

class RouteFlowMgmtEntry : public FlowMgmtEntry {
public:
    RouteFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~RouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtEntry);
};

class InetRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    InetRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    virtual ~InetRouteFlowMgmtEntry() { }
    // Handle covering routeEntry
    bool RecomputeCoveringRouteEntry(FlowMgmtManager *mgr,
                                     InetRouteFlowMgmtKey *covering_route,
                                     InetRouteFlowMgmtKey *key);
    bool HandleNhChange(FlowMgmtManager *mgr, const FlowMgmtRequest *req,
                        FlowMgmtKey *key);

private:
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtEntry);
};

class BridgeRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    BridgeRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    virtual ~BridgeRouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtEntry);
};

class VrfFlowMgmtEntry : public FlowMgmtEntry {
public:
    struct Data {
        Data(VrfFlowMgmtEntry *vrf_mgmt_entry, const VrfEntry *vrf,
             AgentRouteTable *table);
        virtual ~Data();
        void ManagedDelete();
        bool deleted() const { return deleted_; }

        bool deleted_;
        LifetimeRef<Data> table_ref_;
        VrfFlowMgmtEntry *vrf_mgmt_entry_;
        const VrfEntry *vrf_;
    };

    VrfFlowMgmtEntry(VrfFlowMgmtTree *vrf_tree, const VrfEntry *vrf);
    virtual ~VrfFlowMgmtEntry() { }
    bool CanDelete() const;
    VrfFlowMgmtTree *vrf_tree() const { return vrf_tree_; }
    uint32_t vrf_id() const { return vrf_id_; }

private:
    friend class VrfFlowMgmtTree;
    const VrfEntry *vrf_;
    uint32_t vrf_id_;
    Data inet4_;
    Data inet6_;
    Data bridge_;
    // Back reference for the tree
    VrfFlowMgmtTree *vrf_tree_;
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtEntry);
};

class BgpAsAServiceFlowMgmtEntry : public FlowMgmtEntry {
public:
    BgpAsAServiceFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~BgpAsAServiceFlowMgmtEntry() { }
    virtual bool NonOperEntryDelete(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key);
    virtual bool HealthCheckUpdate(Agent *agent, FlowMgmtManager *mgr,
                                   BgpAsAServiceFlowMgmtKey &key,
                                   BgpAsAServiceFlowMgmtRequest *req);

private:
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtEntry);
};

#endif // __AGENT_PKT_FLOW_MGMT_ENTRY_H__
