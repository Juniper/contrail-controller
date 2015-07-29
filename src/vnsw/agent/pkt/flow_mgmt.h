/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_TABLE_MGMT_H__
#define __AGENT_FLOW_TABLE_MGMT_H__

#include "pkt/flow_table.h"

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
// significant latency. The flow management module runs from a work-queuex
// (flow-manager queue) to avoid the latency.
//
// The Flow Management module is responsible only to track the dependency
// between flows and operational entries. It does not *own* the flow entries.
// When flow management module identifies that a flow has to be revaluated
// it will enqueue a message to flow-table. FlowTable module will process
// messages and do appropirate action on the flows
//
// Workflow for flow manager is given below,
// 1. FlowTable module will enqueue message to Flow Management queue on
//    add/delete/change of a flow
// 2. Flow Management module builds the following tracking information
//    - Operational entry to list of dependent flows
//    - Flow entry to list of operational-entries it is dependent on
// 3. FlowTable module registers to DBTables of interest and tracks changes to
//    operational-db entries 
// 4. Flow Table module will enqueue a message to Flow Management queue on
//    add/delete/change of operational entries
// 5. The action in flow-management module for operational entry events will
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
// 1. All message between flow-management and flow-table module will hold
//    object references. This will ensure ref-count for object dont drop till
//    messages are processed.
// 2. Every flow seen by flow-management module is stored in flow_tree_. Key
//    for the tree will FlowEntryPtr which holds reference for the flow entry
//    All other data structures will refer to flow pointer directly.
//
//    The entry from flow_tree_ is removed last after all data structures are
//    cleaned up
//
// Per FlowEntry mutex is used to synchronize access to same Flow between
// FlowTable and Flow Management module
//
// DBEntry reference
// -----------------
// Flow Management module will rely on DBState set by FlowTable module to
// ensure a DBEntry is not deleted till all its flows are deleted.
//
// Flow Table Module                      Flow Management module
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
// When a VRF add is seen, the FlowTable module will register for Inet and
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
////////////////////////////////////////////////////////////////////////////

// Forward declaration
class FlowMgmtManager;

////////////////////////////////////////////////////////////////////////////
// Format of request to the Flow Management module
////////////////////////////////////////////////////////////////////////////
class FlowMgmtRequest {
public:
    enum Event {
        INVALID,

        ADD_FLOW,
        DELETE_FLOW,

        ADD_ACL,
        DELETE_ACL,

        ADD_INTERFACE,
        DELETE_INTERFACE,

        ADD_NH,
        DELETE_NH,

        ADD_VN,
        DELETE_VN,

        ADD_INET4_ROUTE,
        CHANGE_INET4_ROUTE,
        DELETE_INET4_ROUTE,

        ADD_INET6_ROUTE,
        CHANGE_INET6_ROUTE,
        DELETE_INET6_ROUTE,

        ADD_BRIDGE_ROUTE,
        DELETE_BRIDGE_ROUTE,

        ADD_VRF,
        DELETE_VRF,
        RETRY_DELETE_VRF,
        END
    };

    FlowMgmtRequest(Event event, FlowEntryPtr &flow) :
        event_(event), flow_(flow), db_entry_(NULL), vrf_id_(0) {
        if (event == RETRY_DELETE_VRF)
            assert(vrf_id_);
    }

    FlowMgmtRequest(Event event, const DBEntry *db_entry) :
        event_(event), flow_(NULL), db_entry_(db_entry), vrf_id_(0) {
        if (event == RETRY_DELETE_VRF) {
            const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(db_entry);
            assert(vrf);
            vrf_id_ = vrf->vrf_id();
        }
    }

    virtual ~FlowMgmtRequest() { }

    FlowTableRequest::Event ToFlowTableEvent() const {
        FlowTableRequest::Event table_event = FlowTableRequest::INVALID;
        switch (event_) {
        case ADD_INTERFACE:
            table_event = FlowTableRequest::REVALUATE_INTERFACE;
            break;
        case DELETE_INTERFACE:
            table_event = FlowTableRequest::DELETE_INTERFACE;
            break;
        case ADD_VN:
            table_event = FlowTableRequest::REVALUATE_VN;
            break;
        case DELETE_VN:
            table_event = FlowTableRequest::DELETE_VN;
            break;
        case ADD_NH:
            table_event = FlowTableRequest::REVALUATE_NH;
            break;
        case DELETE_NH:
            table_event = FlowTableRequest::DELETE_NH;
            break;
        case ADD_INET4_ROUTE:
        case ADD_INET6_ROUTE:
        case DELETE_INET4_ROUTE:
        case DELETE_INET6_ROUTE:
            table_event = FlowTableRequest::REVALUATE_FLOW;
            break;
        case CHANGE_INET4_ROUTE:
            table_event = FlowTableRequest::REVALUATE_INET4_ROUTE;
            break;

        case CHANGE_INET6_ROUTE:
            table_event = FlowTableRequest::REVALUATE_INET6_ROUTE;
            break;

        case ADD_BRIDGE_ROUTE:
            table_event = FlowTableRequest::REVALUATE_FLOW;
            break;
        case DELETE_BRIDGE_ROUTE:
            table_event = FlowTableRequest::REVALUATE_FLOW;

        case ADD_ACL:
            table_event = FlowTableRequest::REVALUATE_ACL;
            break;
        case DELETE_ACL:
            table_event = FlowTableRequest::DELETE_ACL;
            break;

        default:
            break;
        }

        return table_event;
    }

    Event event() const { return event_; }
    FlowEntryPtr &flow() { return flow_; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t vrf_id() const { return vrf_id_; }

private:
    Event event_;
    // FlowEntry pointer to hold flow reference till message is processed
    FlowEntryPtr flow_;
    // DBEntry pointer. The DBState from FlowTable module ensures DBEntry is
    // not deleted while message holds pointer
    const DBEntry *db_entry_;
    uint32_t vrf_id_;

    DISALLOW_COPY_AND_ASSIGN(FlowMgmtRequest);
};

////////////////////////////////////////////////////////////////////////////
// Flow Management module maintains following data structures
//
// - FlowEntryTree : Tree of all flow entries
//   FlowEntryPtr  : Key for the tree. Holds reference to FlowEntry
//   FlowEntryInfo : Data for the tree. Contains list of DBEntries the flow is
//                   dependent on.
//
// - FlowMgmtTree  : Per operational entry tree. Tracks flow entries dependent
//                   on the operational entry
//
//                   An entry into the tree can be added when,
//                   1. Flow is added/changed and it refers to the DBEntry
//                   or
//                   2. DBEntry add/change message is got from FlowTable
//
//                   Entry from the tree is removed,
//                   1. All flows dependent on the entry are deleted
//                   AND
//                   2. DBEntry delete message is got from FlowTable
//
//   FlowEntryKey  : Key for the tree. Contains DBEntry pointer as key
//   FlowMgmtEntry : Data fot the tree. Contains tree of flow-entries dependent
//                   on the DBEntry
//
// - AclFlowMgmtTree        : FlowMgmtTree for ACL
// - AceIdFlowMgmtTree      : FlowMgmtTree for ACE.
//                            Used to track per ACE-ID counters
// - InterfaceFlowMgmtTree  : FlowMgmtTree for VM-Interfaces
// - VnFlowMgmtTree         : FlowMgmtTree for VN
// - VmFlowMgmtTree         : FlowMgmtTree for VM
// - InetRouteFlowMgmtTree  : FlowMgmtTree for IPv4 routes
// - InetRouteFlowMgmtTree  : FlowMgmtTree for IPv6 routes
// - BridgeRouteFlowMgmtTree: FlowMgmtTree for Bridge routes
// - NhFlowMgmtTree         : FlowMgmtTree for NH
////////////////////////////////////////////////////////////////////////////
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
        END
    };

    FlowMgmtKey(Type type, const DBEntry *db_entry) :
        type_(type), db_entry_(db_entry) {
    }
    virtual ~FlowMgmtKey() { }

    // Clone the key
    virtual FlowMgmtKey *Clone() = 0;

    // Convert from FlowMgmtKey to FlowTableRequest
    virtual void KeyToFlowRequest(FlowTableRequest *req) {
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

    FlowTableRequest::Event FreeFlowTableEvent() const {
        FlowTableRequest::Event event = FlowTableRequest::INVALID;
        switch (type_) {
        case INTERFACE:
            event = FlowTableRequest::DELETE_OBJECT_INTERFACE;
            break;

        case ACL:
            event = FlowTableRequest::DELETE_OBJECT_ACL;
            break;

        case ACE_ID:
            event = FlowTableRequest::INVALID;
            break;

        case VN:
            event = FlowTableRequest::DELETE_OBJECT_VN;
            break;

        case VM:
            event = FlowTableRequest::INVALID;
            break;

        case INET4:
        case INET6:
        case BRIDGE:
            event = FlowTableRequest::DELETE_OBJECT_ROUTE;
            break;

        case NH:
            event = FlowTableRequest::DELETE_OBJECT_NH;
            break;

        default:
            assert(0);
        }
        return event;
    }

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

typedef std::set<FlowMgmtKey *, FlowMgmtKeyCmp> FlowMgmtKeyTree;

class FlowMgmtEntry {
public:
    // Flow management state of the entry
    enum State {
        INVALID,
        OPER_NOT_SEEN,
        OPER_ADD_SEEN,
        OPER_DEL_SEEN,
    };

    typedef std::set<FlowEntry *> Tree;

    FlowMgmtEntry() : oper_state_(OPER_NOT_SEEN) { }
    virtual ~FlowMgmtEntry() {
        assert(tree_.size() == 0);
    }

    uint32_t Size() const { return tree_.size(); }
    // Make flow dependent on the DBEntry
    virtual bool Add(FlowEntry *flow);
    // Remove flow frrm dependency tree
    virtual bool Delete(FlowEntry *flow);

    // Handle Add/Change event for DBEntry
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key,
                              FlowTable *table);
    virtual bool OperEntryChange(const FlowMgmtRequest *req, FlowMgmtKey *key,
                                 FlowTable *table);
    // Handle Delete event for DBEntry
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key,
                                 FlowTable *table);
    // Can the entry be deleted?
    virtual bool CanDelete() const;

    void set_oper_state(State state) { oper_state_ = state; }
    State oper_state() const { return oper_state_; }
protected:
    // Add seen from OperDB entry
    State oper_state_;
    Tree tree_;
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
    virtual bool Add(FlowMgmtKey *key, FlowEntry *flow);
    // Delete a flow from dependency tree for an object
    // Entry is deleted after all flows dependent on the entry are deleted
    // and DBEntry delete message is got from FlowTable
    virtual bool Delete(FlowMgmtKey *key, FlowEntry *flow);

    // Handle DBEntry add
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle DBEntry change
    virtual bool OperEntryChange(const FlowMgmtRequest *req, FlowMgmtKey *key);
    // Handle DBEntry delete
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);

    // Get all Keys relavent for the tree and store them into FlowMgmtKeyTree
    virtual void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) = 0;
    // Allocate a FlowMgmtEntry for the tree
    virtual FlowMgmtEntry *Allocate() = 0;

    // Called just before entry is deleted. Used to implement cleanup operations
    void DeleteNotify(FlowMgmtKey *key);
    FlowMgmtEntry *Locate(FlowMgmtKey *key);
    FlowMgmtEntry *Find(FlowMgmtKey *key);
    FlowMgmtKey *UpperBound(FlowMgmtKey *key);
    Tree &tree() { return tree_; }
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
    AclFlowMgmtKey(const AclDBEntry *acl) :
        FlowMgmtKey(FlowMgmtKey::ACL, acl) {
    }
    virtual ~AclFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new AclFlowMgmtKey(static_cast<const AclDBEntry *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtKey);
};

class AclFlowMgmtEntry : public FlowMgmtEntry {
public:
    AclFlowMgmtEntry() : FlowMgmtEntry() { }
    ~AclFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtEntry);
};

class AclFlowMgmtTree : public FlowMgmtTree {
public:
    AclFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~AclFlowMgmtTree() { }

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const MatchAclParamsList *acl_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();

private:
    uint32_t flow_miss_;
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtTree);
};

////////////////////////////////////////////////////////////////////////////
// ACE-ID based tree. Stored only to copute #flows per ace-id
////////////////////////////////////////////////////////////////////////////
class AceIdFlowMgmtKey : public FlowMgmtKey {
public:
    AceIdFlowMgmtKey(uint32_t id) :
        FlowMgmtKey(FlowMgmtKey::ACE_ID, NULL), ace_id_(id) {
    }
    virtual ~AceIdFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() { return new AceIdFlowMgmtKey(ace_id_); }
    // We dont enqueue request from ACL-ID to flow-table
    virtual void KeyToFlowRequest(FlowTableRequest *req) { assert(0); }
    bool Compare(const FlowMgmtKey *rhs) const {
        return ace_id_ < (static_cast<const AceIdFlowMgmtKey *>(rhs))->ace_id_;
    }

private:
    uint32_t ace_id_;
    DISALLOW_COPY_AND_ASSIGN(AceIdFlowMgmtKey);
};

class AceIdFlowMgmtEntry : public FlowMgmtEntry {
public:
    AceIdFlowMgmtEntry() : FlowMgmtEntry() { }
    ~AceIdFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(AceIdFlowMgmtEntry);
};

class AceIdFlowMgmtTree : public FlowMgmtTree {
public:
    AceIdFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~AceIdFlowMgmtTree() { }

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const AclEntryIDList *ace_id_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const MatchAclParamsList *acl_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();

private:
    DISALLOW_COPY_AND_ASSIGN(AceIdFlowMgmtTree);
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
    ~VnFlowMgmtEntry() { }

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
    ~VnFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();

    bool Add(FlowMgmtKey *key, FlowEntry *flow);
    bool Delete(FlowMgmtKey *key, FlowEntry *flow);
    bool OperEntryAdd(FlowMgmtRequest *req, FlowMgmtKey *key);
    bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    void VnFlowCounters(const VnEntry *vn,
                        uint32_t *ingress_flow_count,
                        uint32_t *egress_flow_count);
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
    InterfaceFlowMgmtEntry() : FlowMgmtEntry() { }
    ~InterfaceFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtEntry);
};

class InterfaceFlowMgmtTree : public FlowMgmtTree {
public:
    InterfaceFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    ~InterfaceFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();
private:
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtTree);
};

class VmFlowMgmtKey : public FlowMgmtKey {
public:
    VmFlowMgmtKey(const VmEntry *vm) : FlowMgmtKey(FlowMgmtKey::VM, vm) { }
    virtual ~VmFlowMgmtKey() { }
    virtual FlowMgmtKey *Clone() {
        return new VmFlowMgmtKey(static_cast<const VmEntry *>(db_entry()));
    }
private:
    DISALLOW_COPY_AND_ASSIGN(VmFlowMgmtKey);
};

class VmFlowMgmtEntry : public FlowMgmtEntry {
public:
    VmFlowMgmtEntry() : FlowMgmtEntry() { }
    ~VmFlowMgmtEntry() { }

private:
    uint32_t linklocal_flow_count;
    DISALLOW_COPY_AND_ASSIGN(VmFlowMgmtEntry);
};

class VmFlowMgmtTree : public FlowMgmtTree {
public:
    VmFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    ~VmFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();
private:
    DISALLOW_COPY_AND_ASSIGN(VmFlowMgmtTree);
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
    ~NhFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtEntry);
};

class NhFlowMgmtTree : public FlowMgmtTree {
public:
    NhFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) {}
    ~NhFlowMgmtTree() {}

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();
private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtTree);
};

#include <pkt/flow_mgmt_route.h>

struct FlowEntryRefCmp {
    bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
        FlowEntry *lhs = l.get();
        FlowEntry *rhs = r.get();

        return (lhs < rhs);
    }
};

class FlowMgmtManager {
public:
    static const std::string kFlowMgmtTask;
    struct FlowEntryInfo {
        FlowMgmtKeyTree tree_;
        uint32_t count_; // Number of times tree modified
        bool ingress_;   // Ingress flow?
        bool local_flow_;

        FlowEntryInfo() { }
        virtual ~FlowEntryInfo() { assert(tree_.size() == 0); }
    };
    // We want flow to be valid till Flow Management task is complete. So,
    // use FlowEntryPtr as key and hold reference to flow till we are done
    typedef std::map<FlowEntryPtr, FlowEntryInfo, FlowEntryRefCmp>
        FlowEntryTree;

    FlowMgmtManager(Agent *agent);
    virtual ~FlowMgmtManager() { }

    void Init();
    void Shutdown();

    bool Run(boost::shared_ptr<FlowMgmtRequest> req);

    Agent *agent() const { return agent_; }
    void AddEvent(FlowEntry *low);
    void DeleteEvent(FlowEntry *flow);
    void AddEvent(const VmInterface *vm);
    void DeleteEvent(const VmInterface *vm);
    void AddEvent(const AclDBEntry *acl);
    void DeleteEvent(const AclDBEntry *acl);
    void AddEvent(const NextHop *nh);
    void DeleteEvent(const NextHop *nh);
    void AddEvent(const VnEntry *vn);
    void DeleteEvent(const VnEntry *vn);
    void AddEvent(const AgentRoute *rt);
    void ChangeEvent(const AgentRoute *rt);
    void DeleteEvent(const AgentRoute *rt);
    void AddEvent(const VrfEntry *vrf);
    void DeleteEvent(const VrfEntry *vrf);
    void RetryVrfDeleteEvent(const VrfEntry *vrf);
    void RetryVrfDelete(uint32_t vrf_id);

    void VnFlowCounters(const VnEntry *vn,
                        uint32_t *ingress_flow_count,
                        uint32_t *egress_flow_count);
    bool HasVrfFlows(uint32_t vrf);
private:
    // Handle Add/Change of a flow. Builds FlowMgmtKeyTree for all objects
    void AddFlow(FlowEntryPtr &flow);
    // Handle Delete of a flow. Updates FlowMgmtKeyTree for all objects
    void DeleteFlow(FlowEntryPtr &flow);

    // Add a FlowMgmtKey into the FlowMgmtKeyTree for an object
    // The FlowMgmtKeyTree for object is passed as argument
    void AddFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                        FlowMgmtKey *key);
    // Delete a FlowMgmtKey from FlowMgmtKeyTree for an object
    // The FlowMgmtKeyTree for object is passed as argument
    void DeleteFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                           FlowMgmtKey *key);
    FlowEntryInfo *FindFlowEntryInfo(const FlowEntryPtr &flow);
    FlowEntryInfo *LocateFlowEntryInfo(FlowEntryPtr &flow);
    void DeleteFlowEntryInfo(FlowEntryPtr &flow);
    void MakeFlowMgmtKeyTree(FlowEntry *flow, FlowMgmtKeyTree *tree);

    Agent *agent_;
    WorkQueue<boost::shared_ptr<FlowMgmtRequest> > event_queue_;
    AclFlowMgmtTree acl_flow_mgmt_tree_;
    AceIdFlowMgmtTree ace_id_flow_mgmt_tree_;
    InterfaceFlowMgmtTree interface_flow_mgmt_tree_;
    VnFlowMgmtTree vn_flow_mgmt_tree_;
    VmFlowMgmtTree vm_flow_mgmt_tree_;
    InetRouteFlowMgmtTree ip4_route_flow_mgmt_tree_;
    InetRouteFlowMgmtTree ip6_route_flow_mgmt_tree_;
    BridgeRouteFlowMgmtTree bridge_route_flow_mgmt_tree_;
    VrfFlowMgmtTree vrf_flow_mgmt_tree_;
    NhFlowMgmtTree nh_flow_mgmt_tree_;
    FlowEntryTree flow_tree_;
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtManager);
};

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\


#endif  // __AGENT_FLOW_TABLE_MGMT_H__
