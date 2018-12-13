/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_MGMT_TREE_H__
#define __AGENT_PKT_FLOW_MGMT_TREE_H__

#include <cstdlib>
#include <map>
#include <pkt/flow_mgmt/flow_mgmt_key.h>

class FlowMgmtKeyNode;
class FlowMgmtEntry;

class BgpAsAServiceFlowMgmtRequest;

typedef std::map<FlowMgmtKey *, FlowMgmtKeyNode *, FlowMgmtKeyCmp> FlowMgmtKeyTree;

class FlowMgmtTree {
public:
    typedef std::map<FlowMgmtKey *, FlowMgmtEntry *, FlowMgmtKeyCmp> Tree;
    FlowMgmtTree(FlowMgmtManager *mgr) : mgr_(mgr) { }
    virtual ~FlowMgmtTree() {
        assert(tree_.size() == 0);
    }

    // Add a flow into dependency tree for an object
    // Creates an entry if not already present in the tree
    virtual bool Add(FlowMgmtKey *key, FlowEntry *flow,
                     FlowMgmtKeyNode *node);
    // Delete a flow from dependency tree for an object
    // Entry is deleted after all flows dependent on the entry are deleted
    // and DBEntry delete message is got from FlowTable
    virtual bool Delete(FlowMgmtKey *key, FlowEntry *flow,
                        FlowMgmtKeyNode *node);
    virtual void InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry);
    virtual void RemoveEntry(Tree::iterator it);

    // Handle DBEntry add
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle DBEntry change
    virtual bool OperEntryChange(const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle DBEntry delete
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);

    // Try delete a DBEntry
    virtual bool RetryDelete(FlowMgmtKey *key);

    // Get all Keys relavent for the tree and store them into FlowMgmtKeyTree
    virtual void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) = 0;
    // Allocate a FlowMgmtEntry for the tree
    virtual FlowMgmtEntry *Allocate(const FlowMgmtKey *key) = 0;

    // Called just before entry is deleted. Used to implement cleanup operations
    virtual void FreeNotify(FlowMgmtKey *key, uint32_t gen_id);

    FlowMgmtEntry *Locate(FlowMgmtKey *key);
    FlowMgmtEntry *Find(FlowMgmtKey *key);
    FlowMgmtKey *LowerBound(FlowMgmtKey *key);
    Tree &tree() { return tree_; }
    FlowMgmtManager *mgr() const { return mgr_; }
    static bool AddFlowMgmtKey(FlowMgmtKeyTree *tree, FlowMgmtKey *key);

protected:
    bool TryDelete(FlowMgmtKey *key, FlowMgmtEntry *entry);
    Tree tree_;
    FlowMgmtManager *mgr_;

private:
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtTree);
};

////////////////////////////////////////////////////////////////////////////
// Object specific information below
////////////////////////////////////////////////////////////////////////////
class AclFlowMgmtTree : public FlowMgmtTree {
public:
    AclFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~AclFlowMgmtTree() { }

    bool Add(FlowMgmtKey *key, FlowEntry *flow, FlowMgmtKey *old_key,
             FlowMgmtKeyNode *node);
    bool Delete(FlowMgmtKey *key, FlowEntry *flow,
                FlowMgmtKeyNode *node);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const MatchAclParamsList *acl_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);

private:
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtTree);
};

class VnFlowMgmtTree : public FlowMgmtTree {
public:
    VnFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    virtual ~VnFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);

    void VnFlowCounters(const VnEntry *vn,
                        uint32_t *ingress_flow_count,
                        uint32_t *egress_flow_count);
    void RemoveEntry(Tree::iterator it);
    void InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry);

private:
    // We need to support query of counters in VN from other threads.
    // So, implement synchronization on access to VN Flow Tree
    tbb::mutex mutex_;
    DISALLOW_COPY_AND_ASSIGN(VnFlowMgmtTree);
};

class InterfaceFlowMgmtTree : public FlowMgmtTree {
public:
    InterfaceFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    virtual ~InterfaceFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
    void InterfaceFlowCount(const Interface *itf, uint64_t *created,
                            uint64_t *aged, uint32_t *active_flows);
    void InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry);
    void RemoveEntry(Tree::iterator it);

private:
    // We need to support query of counters in Interface from other threads.
    // So, implement synchronization on access to Interface Flow Tree
    tbb::mutex mutex_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtTree);
};

class NhFlowMgmtTree : public FlowMgmtTree {
public:
    NhFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    virtual ~NhFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);

private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtTree);
};

class RouteFlowMgmtTree : public FlowMgmtTree {
public:
    RouteFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~RouteFlowMgmtTree() { }
    virtual bool HasVrfFlows(uint32_t vrf_id, Agent::RouteTableType type) = 0;

    virtual bool Delete(FlowMgmtKey *key, FlowEntry *flow, FlowMgmtKeyNode *node);
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);

private:
    void SetDBEntry(const FlowMgmtRequest *req, FlowMgmtKey *key);
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtTree);
};

class InetRouteFlowMgmtTree : public RouteFlowMgmtTree {
public:
     typedef Patricia::Tree<InetRouteFlowMgmtKey, &InetRouteFlowMgmtKey::node_,
             InetRouteFlowMgmtKey::KeyCmp> LpmTree;

    InetRouteFlowMgmtTree(FlowMgmtManager *mgr) : RouteFlowMgmtTree(mgr) { }
    virtual ~InetRouteFlowMgmtTree() { }

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree, uint32_t vrf,
                     const IpAddress &ip, uint8_t plen);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const IpAddress &ip, const FlowRouteRefMap *rt_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);

    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
    bool HasVrfFlows(uint32_t vrf_id, Agent::RouteTableType type);

    InetRouteFlowMgmtKey *LPM(const InetRouteFlowMgmtKey *key) {
        if (key->plen_ == 0)
            return NULL;
        return lpm_tree_.LPMFind(key);
    }

    void AddToLPMTree(InetRouteFlowMgmtKey *key) {
        InetRouteFlowMgmtKey *rt_key =
            static_cast<InetRouteFlowMgmtKey *>(key->Clone());
        if (lpm_tree_.Insert(rt_key) == false)
            delete rt_key;
    }

    void DelFromLPMTree(InetRouteFlowMgmtKey *key) {
        InetRouteFlowMgmtKey *rt_key = lpm_tree_.Find(key);
        if (rt_key != NULL) {
            lpm_tree_.Remove(rt_key);
            delete rt_key;
        }
    }
   bool RecomputeCoveringRoute(InetRouteFlowMgmtKey *covering_route,
                               InetRouteFlowMgmtKey *key);
   bool RouteNHChangeEvent(const FlowMgmtRequest *req, FlowMgmtKey *key);

private:
    LpmTree lpm_tree_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtTree);
};

class BridgeRouteFlowMgmtTree : public RouteFlowMgmtTree {
public:
    BridgeRouteFlowMgmtTree(FlowMgmtManager *mgr) : RouteFlowMgmtTree(mgr) { }
    virtual ~BridgeRouteFlowMgmtTree() { }
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
    bool HasVrfFlows(uint32_t vrf_id, Agent::RouteTableType type);

private:
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtTree);
};

////////////////////////////////////////////////////////////////////////////
// Flow Management tree for VRF. VRF tree does not follow the normal pattern
// for other DBEntries.
//
// VRF flow management implements following functions,
// 1. Generate event to delete VRF when all flows for a VRF are deleted
// 2. Implement lifetime reference to Route Table for INET/Bridge DBTables
//    The route-table must be present till all flows relavent for the flow are
//    present.
//    FlowLifetimeRef implements Lifetime actor on the route-table till all
//    flows for the route-table are deleted
//
// When a flow-add is got, we dont really check for presence of VRF or not.
// When adding a flow, FlowTable must ensure that VRF is valid at that time
//
// FlowTable module on the other hand, ensures that flow is not added on a
// non-existing or a deleted VRF
//
// The routes used in flow-entry refer to VRF by vrf-id (ex. RouteFlowRefMap).
// Hence, we dont have VRF pointer in all cases. Instead, we store vrf-id as
// key
////////////////////////////////////////////////////////////////////////////
class VrfFlowMgmtTree : public FlowMgmtTree {
public:
    // Build local mapping of vrf-id to VrfEntry mapping.
    // The mapping is already maintained in VrfTable. But, we cannot query it
    // since we run in parallel to DB Task context
    typedef std::map<uint32_t, const VrfEntry *> VrfIdMap;
    VrfFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~VrfFlowMgmtTree() { }

    virtual FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    void DeleteDefaultRoute(const VrfEntry *vrf);
    virtual void FreeNotify(FlowMgmtKey *key, uint32_t gen_id);
    void RetryDelete(uint32_t vrf_id);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);

private:
    VrfIdMap id_map_;
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtTree);
};

class BgpAsAServiceFlowMgmtTree : public FlowMgmtTree {
public:
    static const int kInvalidCnIndex = -1;
    BgpAsAServiceFlowMgmtTree(FlowMgmtManager *mgr, int index) :
        FlowMgmtTree(mgr), index_(index) {}
    virtual ~BgpAsAServiceFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
    bool BgpAsAServiceHealthCheckUpdate(Agent *agent,
                                        BgpAsAServiceFlowMgmtKey &key,
                                        BgpAsAServiceFlowMgmtRequest *req);
    bool BgpAsAServiceDelete(BgpAsAServiceFlowMgmtKey &key,
                             const FlowMgmtRequest *req);
    void DeleteAll();
    //Gets CN index from flow.
    static int GetCNIndex(const FlowEntry *flow);
    // Called just before entry is deleted. Used to implement cleanup operations
    virtual void FreeNotify(FlowMgmtKey *key, uint32_t gen_id);

private:
    int index_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtTree);
};

#endif // __AGENT_PKT_FLOW_MGMT_TREE_H__
