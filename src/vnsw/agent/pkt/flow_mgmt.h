/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_TABLE_MGMT_H__
#define __AGENT_FLOW_TABLE_MGMT_H__

#include <boost/scoped_ptr.hpp>
#include "pkt/flow_table.h"
#include "pkt/flow_mgmt_request.h"
#include "pkt/flow_event.h"

////////////////////////////////////////////////////////////////////////////
// Flow Management module is responsible to keep flow action in-sync with
// changes to operational-db changes.
//
// Flow Management module tracks following,
// 1. Changes to fields of interest in the operational-db
// 2. Flows dependenent on operational-db entries
//
// On any change to an operational-db, it will ensure the dependent flows are
// revaluated to keep the flow action in-sync with latest operational state
//
// A flow can depend on following operational-entires,
// 1. Interface : Flow depends on the SG-Id of interface
// 2. VN        : Flow depends on policy of VN
// 3. ACL       : Flow depends on ACL Entries in ACL
// 4. Route     : Flow depends on the SG-Id list in the route for both source
//                and destination addresses
// 5. NextHop   : Flow depends on NH for RPF checks and for ECMP Component index
//
// An operational entry can potentially have many flows dependent on it. On
// change to an operational entry if flows are processed inline, it can lead to
// significant latency. The flow management module runs from a work-queue
// (flow-manager queue) to avoid the latency.
//
// The Flow Management module is responsible only to track the dependency
// between flows and operational entries. It does not *own* the flow entries.
// When flow management module identifies that a flow has to be revaluated
// it will invoke flow-table APIs.
//
// There are two sub-modules,
// - Flow Management DBClient
//   This module registers to DB notification for all tables of interest. It
//   enqueues events for flow revaluation in response to DBTable changes
// - Flow Management Tree module
//   This module maintains different data-structures to track flows affected
//   by change to a DBEntry. Its also responsible to trigger revaluation of
//   flows on change to a DBEntry
//
// There are 2 work-queues defined for flow-management,
// - Flow Management Request
//   All request events to flow-management module are enqueued thru this.
//   The Flow Management Tree module acts on events on this queue.
//   * Add of FLow
//   * Delete of Flow
//   * Add of DBEntry
//   * Change of DBEntry
//   * Delete of DBEntry
//   * Export of Flow
//
// - Flow Events
//   Flow Management Tree module may generate events in response to
//   requests. The response events are enqueued here. Example events are,
//   * Flow revaluation in response to DBEntry change
//   * Flow revaluation in response to DBEntry Add/Delete
//   * Flow deletion in response to DBEntry delete
//
// Workflow for flow manager module is given below,
// 1. Flow Table module will enqueue message to Flow Management queue on
//    add/delete/change of a flow. On Flow delete event, Flow Table module will
//    will also enqueue export of the flow.
// 2. Flow stats collection module will enqueue message to Flow Management
//    queue on export of flow
// 3. Flow Management module builds the following tracking information
//    - Operational entry to list of dependent flows
//    - Flow entry to list of operational-entries it is dependent on
// 4. DBClient module registers to DBTables of interest and tracks changes to
//    operational-db entries 
// 5. Flow Table module will enqueue a message to Flow Management queue on
//    add/delete/change of operational entries
// 6. The action in flow-management module for operational entry events will
//    depend on the operational entry type
//
//    VN Add/Change      : Revaluate flows for change in policy
//    VN Delete          : Delete dependent flows
//    VMI Add/Change     : Revaluate flows for change in SG entry in VMI
//    Interface Delete   : Delete dependent flows
//    NH Add/Change      : Revaluate flows for change in RPF-NH
//    NH Delete          : Delete dependent flows
//    ACL Add/Change     : Revaluate flows for change in ACL rules
//    ACL Delete         : Delete dependent flows
//    Bridge Add/Change  : Flow entries are revaluated for change in SG
//    Bridge Delete      : Delete dependent flows
//    Inet Route Add     : Add of an inet route can potentially alter the route
//                         used for a route. It can also potentially alter
//                         other attributes such as floating-ip used etc...
//                         So, all flows dependent on the "covering route" are
//                         enqueued for revluation by PktFlowInfo module
//    Inet Route Change  : Revluate for for change in RPF-NH, VN and SG
//    Inet Route Delete  : Flows depending on this route will start using the
//                         covering route. It can also potentially alter
//                         other attributes such as floating-ip used etc...
//                         So, all flows dependent on the "covering route" are
//                         enqueued for revluation by PktFlowInfo module
//
// Concurrency and references
// The Flow Management module can often lead to compute spikes. To ensure this
// doesnt affect flow setup rates, it is preferable to run flow-management
// module in parallel to both DBTable and Flow setup/teardown task.
//
// Since flow-management module runs in parallel to DBTable/Flow tasks, we must
// ensure that flows and the operational dbentries are not deleted till
// flow-management module references are gone. This is achieved by following,
//
// Flow reference
// --------------
// 1. All message between flow-management and flow-table/flow-stats module will
//    hold object references. This will ensure ref-count for object dont drop
//    till messages are processed.
// 2. Each flow seen by flow-management will hold a reference to FlowEntryInfo
//    in FlowEntry as flow_mgmt_info_
//
// Per FlowEntry mutex is used to synchronize access to same Flow between
// FlowTable and Flow Management module
//
// DBEntry reference
// -----------------
// Flow Management module will rely on DBState set by DBClient module to
// ensure a DBEntry is not deleted till all its flows are deleted.
//
// Flow Management DBClient               Flow Management module
//
// 1. On DBEntry deletion, enqueue a
//    message to Flow Management module
//                                        2. Find all dependent flows and
//                                           enqueue flow-delete request for
//                                           all flows. Mark the entry as
//                                           deleted
// 3. Delete the flow and enqueue a
//    flow-deleted message to
//    flow-management module
//                                        4. Find all operational entries the
//                                           flow is dependent on and remove
//                                           the flow from their dependent tree.
//                                           If an operational entry is deleted
//                                           and there are no more dependent
//                                           flows, enqueue a message to delete
//                                           the operational entry
// 5. Remove DBState for the DBEntry
//
// RouteTable reference
// -----------------
// The Inet and Bridge route tables should not be deleted till all flows using
// them are deleted. Flow Management module holds Lifetime reference to ensure
// DBTables are deleted only after all its flows are deleted
//
// VRF Reference
// -------------
// When a VRF add is seen, the DBClient sub-module will register for Inet and
// Bridge route tables for the VRF. The unregister for these tables should
// happen only after all flows on them are deleted. The Flow Management module
// will enqueue delete of VRF only after flow entries in all Inet and Bridge
// routes are deleted
//
// Additional functionality:
// -------------------------
// A few more additional functionality is pushed to flow-management module
// 1. Logging of flows
// 2. Flow UVE
// 3. Tracking per VN flows
//
// Duplicate Deletes:
// -----------------
// Consider following sequence of events,
// 1. Add ACL
// 2. Delete ACL
// 3. Delete ACL
//
// Correspondingly, 3 events are enqueued to Flow Management Request queue.
// When event (2) is processed, if ACL does not have any more flows, it will
// result in removing DBState for the ACL and eventual free of ACL before
// event (3) is processed. When event (3) is being processed, it may refer to
// a free'd DBEntry
//
// We use a gen-id to handle this scenario.The gen-id is incremented on every
// Delete notification received for DBEntry. When DELETE event is enqueued, it
// will also carry the gen-id field. The gen-id is also carried in the
// response events generated.
//
// When Flow Management Dbclient module receives FREE_DBENTRY event, it will
// ignore the message if gen-id does not match with latest value.
////////////////////////////////////////////////////////////////////////////

// Forward declaration
class FlowMgmtManager;
class VrfFlowMgmtTree;
class FlowMgmtDbClient;

////////////////////////////////////////////////////////////////////////////
// Flow Management module maintains following data structures
//
// - FlowEntryTree : Tree of all flow entries
//   FlowEntryPtr  : Key for the tree. Holds reference to FlowEntry
//   FlowEntryInfo : Data for the tree. Contains tree of DBEntries the flow is
//                   dependent on.
//
// - FlowMgmtTree  : Per operational entry tree. Tracks flow entries dependent
//                   on the operational entry
//
//                   An entry into the tree can be added when,
//                   1. Flow is added/changed and it refers to the DBEntry
//                   or
//                   2. DBEntry add/change message is got from DBClient
//
//                   Entry from the tree is removed,
//                   1. All flows dependent on the entry are deleted
//                   AND
//                   2. DBEntry delete message is got from DBClient
//
//   FlowEntryKey  : Key for the tree. Contains DBEntry pointer as key
//   FlowMgmtEntry : Data fot the tree. Maintains intrusive-list of
//                   FlowMgmtKeyNodes(which contains references to Flow entries)
//                   dependent on the DBEntry
//
// - AclFlowMgmtTree        : FlowMgmtTree for ACL
// - InterfaceFlowMgmtTree  : FlowMgmtTree for VM-Interfaces
// - VnFlowMgmtTree         : FlowMgmtTree for VN
// - InetRouteFlowMgmtTree  : FlowMgmtTree for IPv4 routes
// - InetRouteFlowMgmtTree  : FlowMgmtTree for IPv6 routes
// - BridgeRouteFlowMgmtTree: FlowMgmtTree for Bridge routes
// - NhFlowMgmtTree         : FlowMgmtTree for NH
// - VrfFlowMgmtTree        : FlowMgmtTree for VRF. It doesnt track the flows
//                            for the VRF. But is used to ensure VRF entry is
//                            not deleted till all route-entries are freed and
//                            DELETE event for VRF is processed
// - BgpAsAServiceFlowMgmtTree : FlowMgmtTree per control-node. This is
//                               maintained per control node because VMI can
//                               establish a bgp peer session with each control
//                               node.
////////////////////////////////////////////////////////////////////////////
class FlowMgmtKeyNode {
public:
    FlowMgmtKeyNode() : flow_(NULL) { }
    FlowMgmtKeyNode(FlowEntry *fe) : flow_(fe) { }
    virtual ~FlowMgmtKeyNode() { }
    FlowEntry *flow_entry() const { return flow_;}
private:
    friend class FlowMgmtEntry;
    FlowEntry *flow_;
    boost::intrusive::list_member_hook<> hook_;
};

class FlowMgmtKey {
public:
    enum Type {
        INVALID,
        INTERFACE,
        ACL,
        ACE_ID,
        VN,
        VM,
        INET4,
        INET6,
        BRIDGE,
        NH,
        VRF,
        BGPASASERVICE,
        END
    };

    FlowMgmtKey(Type type, const DBEntry *db_entry) :
        type_(type), db_entry_(db_entry) {
    }
    virtual ~FlowMgmtKey() { }

    // Clone the key
    virtual FlowMgmtKey *Clone() = 0;

    // Convert from FlowMgmtKey to FlowEvent
    virtual void KeyToFlowRequest(FlowEvent *req) {
        req->set_db_entry(db_entry_);
    }

    // Is DBEntry used as key. Routes dont use DBEntry as key
    virtual bool UseDBEntry() const { return true; }

    // Comparator
    virtual bool Compare(const FlowMgmtKey *rhs) const {
        return false;
    }
    bool IsLess(const FlowMgmtKey *rhs) const {
        if (type_ != rhs->type_)
            return type_ < rhs->type_;

        if (UseDBEntry()) {
            if (db_entry_ != rhs->db_entry_)
                return db_entry_ < rhs->db_entry_;
        }

        return Compare(rhs);
    }

    FlowEvent::Event FreeDBEntryEvent() const;
    Type type() const { return type_; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }

protected:
    Type type_;
    mutable const DBEntry *db_entry_;
private:
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtKey);
};

struct FlowMgmtKeyCmp {
    bool operator()(const FlowMgmtKey *l, const FlowMgmtKey *r) {
        return l->IsLess(r);
    }
};

typedef std::map<FlowMgmtKey *, FlowMgmtKeyNode *, FlowMgmtKeyCmp> FlowMgmtKeyTree;

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
protected:
    // Add seen from OperDB entry
    State oper_state_;
    uint32_t gen_id_;
    FlowList flow_list_;
private:
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtEntry);
};

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
class AclFlowMgmtKey : public FlowMgmtKey {
public:
    AclFlowMgmtKey(const AclDBEntry *acl, const AclEntryIDList *ace_id_list) :
        FlowMgmtKey(FlowMgmtKey::ACL, acl) {
        if (ace_id_list) {
            ace_id_list_ = *ace_id_list;
        }
    }
    virtual ~AclFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new AclFlowMgmtKey(static_cast<const AclDBEntry *>(db_entry()),
                                  &ace_id_list_);
    }
    const AclEntryIDList *ace_id_list() const { return &ace_id_list_; }
    void set_ace_id_list(const AclEntryIDList *list) {
        ace_id_list_ = *list;
    }
private:
    AclEntryIDList ace_id_list_;
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtKey);
};

class AclFlowMgmtEntry : public FlowMgmtEntry {
public:
    typedef std::map<int, int> AceIdFlowCntMap;
    AclFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~AclFlowMgmtEntry() { }
    void FillAclFlowSandeshInfo(const AclDBEntry *acl, AclFlowResp &data,
                                const int last_count, Agent *agent);
    void FillAceFlowSandeshInfo(const AclDBEntry *acl, AclFlowCountResp &data,
                                int ace_id);
    bool Add(const AclEntryIDList *ace_id_list, FlowEntry *flow,
             const AclEntryIDList *old_id_list, FlowMgmtKeyNode *node);
    bool Delete(const AclEntryIDList *ace_id_list, FlowEntry *flow,
                FlowMgmtKeyNode *node);
    void DecrementAceIdCountMap(const AclEntryIDList *id_list);
private:
    std::string GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl,
                                         const int last_count) const;
    uint32_t flow_miss_;
    AceIdFlowCntMap aceid_cnt_map_;
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtEntry);
};

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

class VnFlowMgmtKey : public FlowMgmtKey {
public:
    VnFlowMgmtKey(const VnEntry *vn) : FlowMgmtKey(FlowMgmtKey::VN, vn) { }
    virtual ~VnFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new VnFlowMgmtKey(static_cast<const VnEntry *>(db_entry()));
    }
private:
    DISALLOW_COPY_AND_ASSIGN(VnFlowMgmtKey);
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

class InterfaceFlowMgmtKey : public FlowMgmtKey {
public:
    InterfaceFlowMgmtKey(const Interface *intf) :
        FlowMgmtKey(FlowMgmtKey::INTERFACE, intf) {
    }
    virtual ~InterfaceFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new InterfaceFlowMgmtKey(static_cast<const Interface *>(db_entry()));
    }
private:
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtKey);
};

class InterfaceFlowMgmtEntry : public FlowMgmtEntry {
public:
    InterfaceFlowMgmtEntry() : FlowMgmtEntry(), flow_created_(0),
        flow_aged_(0) { }
    virtual ~InterfaceFlowMgmtEntry() { }

    bool Add(FlowEntry *flow, FlowMgmtKeyNode *node);
    bool Delete(FlowEntry *flow, FlowMgmtKeyNode *node);
    uint64_t flow_created() const {return flow_created_;}
    uint64_t flow_aged() const {return flow_aged_;}
private:
    uint64_t flow_created_;
    uint64_t flow_aged_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtEntry);
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

class NhFlowMgmtKey : public FlowMgmtKey {
public:
    NhFlowMgmtKey(const NextHop *nh) : FlowMgmtKey(FlowMgmtKey::NH, nh) { }
    virtual ~NhFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new NhFlowMgmtKey(static_cast<const NextHop *>(db_entry()));
    }
private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtKey);
};

class NhFlowMgmtEntry : public FlowMgmtEntry {
public:
    NhFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~NhFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtEntry);
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

class RouteFlowMgmtKey : public FlowMgmtKey {
public:
    static Type AddrToType(const IpAddress &addr) {
        if (addr.is_v4())
            return INET4;

        if (addr.is_v6())
            return INET6;

        assert(0);
    }

    static Type RouteToType(const AgentRoute *rt) {
        const InetUnicastRouteEntry *inet_rt =
            dynamic_cast<const InetUnicastRouteEntry *>(rt);
        if (inet_rt) {
            return AddrToType(inet_rt->addr());
        }

        if (dynamic_cast<const BridgeRouteEntry *>(rt))
            return BRIDGE;

        assert(0);
    }

    RouteFlowMgmtKey(Type type, uint32_t vrf_id) :
        FlowMgmtKey(type, NULL), vrf_id_(vrf_id) {
    }

    RouteFlowMgmtKey(Type type, const AgentRoute *rt, uint32_t vrf_id) :
        FlowMgmtKey(type, rt), vrf_id_(vrf_id) {
    }
    virtual ~RouteFlowMgmtKey() { }

    virtual bool UseDBEntry() const { return false; }
    uint32_t vrf_id() const { return vrf_id_; }
protected:
    uint32_t vrf_id_;
private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtKey);
};

class RouteFlowMgmtEntry : public FlowMgmtEntry {
public:
    RouteFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~RouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtEntry);
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

class InetRouteFlowMgmtKey : public RouteFlowMgmtKey {
public:
    InetRouteFlowMgmtKey(const InetUnicastRouteEntry *route) :
        RouteFlowMgmtKey(AddrToType(route->addr()), route, route->vrf_id()),
        ip_(route->addr()), plen_(route->plen()) {
    }

    InetRouteFlowMgmtKey(uint32_t vrf_id, const IpAddress &ip, uint8_t plen) :
        RouteFlowMgmtKey(AddrToType(ip), vrf_id), ip_(ip), plen_(plen) {
    }
    virtual ~InetRouteFlowMgmtKey() { }

    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const InetRouteFlowMgmtKey *rhs_key =
            static_cast<const InetRouteFlowMgmtKey *>(rhs);
        if (vrf_id_ != rhs_key->vrf_id_)
            return vrf_id_ < rhs_key->vrf_id_;

        if (ip_ != rhs_key->ip_)
            return ip_ < rhs_key->ip_;

        return plen_ < rhs_key->plen_;
    }

    class KeyCmp {
    public:
        static std::size_t BitLength(const InetRouteFlowMgmtKey *rt) {
            return (((sizeof(rt->vrf_id_) + sizeof(rt->type_)) << 3) +
                    rt->plen_);
        }

        static char ByteValue(const InetRouteFlowMgmtKey *rt, std::size_t idx) {
            const char *ch;
            std::size_t i = idx;
            if (i < sizeof(rt->vrf_id_)) {
                ch = (const char *)&rt->vrf_id_;
                return ch[sizeof(rt->vrf_id_) - i - 1];
            }
            i -= sizeof(rt->vrf_id_);
            if (i < sizeof(rt->type_)) {
                ch = (const char *)&rt->type_;
                return ch[sizeof(rt->type_) - i - 1];
            }
            i -= sizeof(rt->type_);
            if (rt->type_ == INET4) {
                return rt->ip_.to_v4().to_bytes()[i];
            } else if (rt->type_ == INET6) {
                return rt->ip_.to_v6().to_bytes()[i];
            } else {
                assert(0);
            }
        }
    };

    FlowMgmtKey *Clone() {
        if (ip_.is_v4()) {
            return new InetRouteFlowMgmtKey(vrf_id_, ip_.to_v4(), plen_);
        }

        if (ip_.is_v6()) {
            return new InetRouteFlowMgmtKey(vrf_id_, ip_.to_v6(), plen_);
        }

        return NULL;
    } 

    bool Match(const IpAddress &match_ip) const {
        if (ip_.is_v4()) {
            return (Address::GetIp4SubnetAddress(ip_.to_v4(), plen_) ==
                    Address::GetIp4SubnetAddress(match_ip.to_v4(), plen_));
        } else if (ip_.is_v6()) {
            return (Address::GetIp6SubnetAddress(ip_.to_v6(), plen_) ==
                    Address::GetIp6SubnetAddress(match_ip.to_v6(), plen_));
        }
        assert(0);
        return false;
    }

    bool NeedsReCompute(const FlowEntry *flow);


private:
    friend class InetRouteFlowMgmtTree;
    IpAddress ip_;
    uint8_t plen_;
    Patricia::Node node_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtKey);
};

class InetRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    InetRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    virtual ~InetRouteFlowMgmtEntry() { }
    // Handle covering routeEntry
    bool RecomputeCoveringRouteEntry(FlowMgmtManager *mgr,
                                     InetRouteFlowMgmtKey *covering_route,
                                     InetRouteFlowMgmtKey *key);
private:
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtEntry);
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

private:
    LpmTree lpm_tree_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtTree);
};

class BridgeRouteFlowMgmtKey : public RouteFlowMgmtKey {
public:
    BridgeRouteFlowMgmtKey(const BridgeRouteEntry *rt) :
        RouteFlowMgmtKey(BRIDGE, rt, rt->vrf_id()), mac_(rt->mac()) {
    }

    BridgeRouteFlowMgmtKey(uint32_t vrf_id, const MacAddress &mac) :
        RouteFlowMgmtKey(BRIDGE, vrf_id), mac_(mac) {
    }
    virtual ~BridgeRouteFlowMgmtKey() { }

    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const BridgeRouteFlowMgmtKey *rhs_key =
            static_cast<const BridgeRouteFlowMgmtKey *>(rhs);
        if (vrf_id_ != rhs_key->vrf_id_)
            return vrf_id_ < rhs_key->vrf_id_;

        return mac_ < rhs_key->mac_;
    }
    FlowMgmtKey *Clone() {
        return new BridgeRouteFlowMgmtKey(vrf_id(), mac_);
    }

private:
    friend class BridgeRouteFlowMgmtTree;
    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtKey);
};

class BridgeRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    BridgeRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    virtual ~BridgeRouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtEntry);
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
class VrfFlowMgmtKey : public FlowMgmtKey {
public:
    VrfFlowMgmtKey(const VrfEntry *vrf) :
        FlowMgmtKey(FlowMgmtKey::VRF, vrf) {
    }
    virtual ~VrfFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new VrfFlowMgmtKey(static_cast<const VrfEntry *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtKey);
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

////////////////////////////////////////////////////////////////////////////
// Flow Management tree for bgp as a service.
////////////////////////////////////////////////////////////////////////////
class BgpAsAServiceFlowMgmtKey : public FlowMgmtKey {
public:
    BgpAsAServiceFlowMgmtKey(const boost::uuids::uuid &uuid,
                             uint32_t source_port,
                             uint8_t cn_index) :
        FlowMgmtKey(FlowMgmtKey::BGPASASERVICE, NULL), uuid_(uuid),
        source_port_(source_port), cn_index_(cn_index) { }
    virtual ~BgpAsAServiceFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new BgpAsAServiceFlowMgmtKey(uuid_, source_port_, cn_index_);
    }
    virtual bool UseDBEntry() const { return false; }
    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const BgpAsAServiceFlowMgmtKey *rhs_key =
            static_cast<const BgpAsAServiceFlowMgmtKey *>(rhs);
        if (uuid_ != rhs_key->uuid_)
            return uuid_ < rhs_key->uuid_;
        if (cn_index_ != rhs_key->cn_index_)
            return cn_index_< rhs_key->cn_index_;
        return source_port_ < rhs_key->source_port_;
    }
    const boost::uuids::uuid &uuid() const { return uuid_; }
    uint32_t source_port() const { return source_port_; }
    uint8_t cn_index() const { return cn_index_; }

private:
    boost::uuids::uuid uuid_;
    uint32_t source_port_;
    uint8_t cn_index_; //Control node index
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtKey);
};

class BgpAsAServiceFlowMgmtEntry : public FlowMgmtEntry {
public:
    BgpAsAServiceFlowMgmtEntry() : FlowMgmtEntry() { }
    virtual ~BgpAsAServiceFlowMgmtEntry() { }
    virtual bool NonOperEntryDelete(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key);

private:
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtEntry);
};

class BgpAsAServiceFlowMgmtTree : public FlowMgmtTree {
public:
    static const int kInvalidCnIndex = -1;
    BgpAsAServiceFlowMgmtTree(FlowMgmtManager *mgr, int index) :
        FlowMgmtTree(mgr), index_(index) {}
    virtual ~BgpAsAServiceFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate(const FlowMgmtKey *key);
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

////////////////////////////////////////////////////////////////////////////
// Per flow information stored in flow-mgmt module. Holds a reference to
// flow so that flow active till flow-mgmt processing is done
////////////////////////////////////////////////////////////////////////////
class FlowEntryInfo {
public:
    FlowEntryInfo(FlowEntry *flow) :
        flow_(flow), tree_(), count_(0), ingress_(false), local_flow_(false) {
    }
    virtual ~FlowEntryInfo() { assert(tree_.size() == 0); }
private:
    friend class FlowMgmtManager;
    FlowEntryPtr flow_;
    FlowMgmtKeyTree tree_;
    uint32_t count_; // Number of times tree modified
    bool ingress_;
    bool local_flow_;
    DISALLOW_COPY_AND_ASSIGN(FlowEntryInfo);
};

class FlowMgmtManager {
public:
    typedef boost::shared_ptr<FlowMgmtRequest> FlowMgmtRequestPtr;
    typedef WorkQueue<FlowMgmtRequestPtr> FlowMgmtQueue;

    // Comparator for FlowEntryPtr
    struct FlowEntryRefCmp {
        bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
            FlowEntry *lhs = l.get();
            FlowEntry *rhs = r.get();

            return (lhs < rhs);
        }
    };

    // We want flow to be valid till Flow Management task is complete. So,
    // use FlowEntryPtr as key and hold reference to flow till we are done
    typedef std::map<FlowEntryPtr, FlowEntryInfo, FlowEntryRefCmp>
        FlowEntryTree;

    FlowMgmtManager(Agent *agent, uint16_t table_index);
    virtual ~FlowMgmtManager() { }

    void Init();
    void Shutdown();
    static void InitLogQueue(Agent *agent);
    static void ShutdownLogQueue();
    static void LogFlowUnlocked(FlowEntry *flow, const std::string &op);
    static bool LogHandler(FlowMgmtRequestPtr req);

    bool RequestHandler(FlowMgmtRequestPtr req);
    bool DBRequestHandler(FlowMgmtRequestPtr req);

    bool DBRequestHandler(FlowMgmtRequest *req, const DBEntry *entry);
    bool BgpAsAServiceRequestHandler(FlowMgmtRequest *req);
    bool DbClientHandler(const DBEntry *entry);
    void EnqueueFlowEvent(FlowEvent *event);
    void NonOperEntryEvent(FlowEvent::Event event, FlowEntry *flow);
    void DBEntryEvent(FlowEvent::Event event, FlowMgmtKey *key,
                      FlowEntry *flow);
    void FreeDBEntryEvent(FlowEvent::Event event, FlowMgmtKey *key,
                          uint32_t gen_id);

    Agent *agent() const { return agent_; }
    uint16_t table_index() const { return table_index_; }
    void AddEvent(FlowEntry *low);
    void DeleteEvent(FlowEntry *flow, const RevFlowDepParams &params);
    void FlowStatsUpdateEvent(FlowEntry *flow, uint32_t bytes, uint32_t packets,
                              uint32_t oflow_bytes,
                              const boost::uuids::uuid &u);
    void AddDBEntryEvent(const DBEntry *entry, uint32_t gen_id);
    void ChangeDBEntryEvent(const DBEntry *entry, uint32_t gen_id);
    void DeleteDBEntryEvent(const DBEntry *entry, uint32_t gen_id);
    void RetryVrfDeleteEvent(const VrfEntry *vrf);
    void RetryVrfDelete(uint32_t vrf_id);
    // Dummy event used for testing
    void DummyEvent();

    void VnFlowCounters(const VnEntry *vn,
                        uint32_t *ingress_flow_count,
                        uint32_t *egress_flow_count);
    void InterfaceFlowCount(const Interface *itf, uint64_t *created,
                            uint64_t *aged, uint32_t *active_flows);
    bool HasVrfFlows(uint32_t vrf);

    FlowMgmtDbClient *flow_mgmt_dbclient() const {
        return flow_mgmt_dbclient_.get();
    }

    const FlowMgmtQueue *request_queue() const { return &request_queue_; }
    const FlowMgmtQueue *log_queue() const { return log_queue_; }
    void DisableWorkQueue(bool disable) { request_queue_.set_disable(disable); }
    void BgpAsAServiceNotify(const boost::uuids::uuid &vm_uuid,
                             uint32_t source_port);
    void EnqueueUveAddEvent(const FlowEntry *flow) const;
    void EnqueueUveDeleteEvent(const FlowEntry *flow) const;

    void FlowUpdateQueueDisable(bool val);
    size_t FlowUpdateQueueLength();
    size_t FlowDBQueueLength();
    InetRouteFlowMgmtTree* ip4_route_flow_mgmt_tree() {
        return &ip4_route_flow_mgmt_tree_;
    }

private:
    // Handle Add/Change of a flow. Builds FlowMgmtKeyTree for all objects
    void AddFlow(FlowEntryPtr &flow);
    // Handle Delete of a flow. Updates FlowMgmtKeyTree for all objects
    void DeleteFlow(FlowEntryPtr &flow, const RevFlowDepParams &p);
    void UpdateFlowStats(FlowEntryPtr &flow, uint32_t bytes, uint32_t packets,
                         uint32_t oflow_bytes, const boost::uuids::uuid &u);

    // Add a FlowMgmtKey into the FlowMgmtKeyTree for an object
    // The FlowMgmtKeyTree for object is passed as argument
    void AddFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                        FlowMgmtKey *key, FlowMgmtKey *old_key);
    // Delete a FlowMgmtKey from FlowMgmtKeyTree for an object
    // The FlowMgmtKeyTree for object is passed as argument
    void DeleteFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                           FlowMgmtKey *key, FlowMgmtKeyNode *node);
    FlowEntryInfo *FindFlowEntryInfo(const FlowEntryPtr &flow);
    FlowEntryInfo *LocateFlowEntryInfo(FlowEntryPtr &flow);
    void DeleteFlowEntryInfo(FlowEntryPtr &flow);
    void MakeFlowMgmtKeyTree(FlowEntry *flow, FlowMgmtKeyTree *tree);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data,
                           int ace_id);
    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data,
                               const int last_count);
    void ControllerNotify(uint8_t index);

    Agent *agent_;
    uint16_t table_index_;
    AclFlowMgmtTree acl_flow_mgmt_tree_;
    InterfaceFlowMgmtTree interface_flow_mgmt_tree_;
    VnFlowMgmtTree vn_flow_mgmt_tree_;
    InetRouteFlowMgmtTree ip4_route_flow_mgmt_tree_;
    InetRouteFlowMgmtTree ip6_route_flow_mgmt_tree_;
    BridgeRouteFlowMgmtTree bridge_route_flow_mgmt_tree_;
    VrfFlowMgmtTree vrf_flow_mgmt_tree_;
    NhFlowMgmtTree nh_flow_mgmt_tree_;
    boost::scoped_ptr<BgpAsAServiceFlowMgmtTree> bgp_as_a_service_flow_mgmt_tree_[MAX_XMPP_SERVERS];
    std::auto_ptr<FlowMgmtDbClient> flow_mgmt_dbclient_;
    FlowMgmtQueue request_queue_;
    FlowMgmtQueue db_event_queue_;
    static FlowMgmtQueue *log_queue_;
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtManager);
};

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif  // __AGENT_FLOW_TABLE_MGMT_H__
