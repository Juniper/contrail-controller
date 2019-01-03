/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_TABLE_MGMT_H__
#define __AGENT_FLOW_TABLE_MGMT_H__

#include <boost/scoped_ptr.hpp>
#include "pkt/flow_table.h"
#include <pkt/flow_mgmt/flow_mgmt_dbclient.h>
#include <pkt/flow_mgmt/flow_mgmt_tree.h>
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
    static bool ProcessEvent(FlowMgmtRequest *req, FlowMgmtKey *key,
                             FlowMgmtTree *tree);

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
    void RouteNHChangeEvent(const DBEntry *entry, uint32_t gen_id);
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
    void BgpAsAServiceHealthCheckNotify(const boost::uuids::uuid &vm_uuid,
                                        uint32_t source_port,
                                        const boost::uuids::uuid &hc_uuid,
                                        bool add);
    void EnqueueUveAddEvent(const FlowEntry *flow) const;
    void EnqueueUveDeleteEvent(const FlowEntry *flow) const;

    void FlowUpdateQueueDisable(bool val);
    size_t FlowUpdateQueueLength();
    size_t FlowDBQueueLength();
    InetRouteFlowMgmtTree* ip4_route_flow_mgmt_tree() {
        return &ip4_route_flow_mgmt_tree_;
    }

    BgpAsAServiceFlowMgmtKey *FindBgpAsAServiceInfo(
                                FlowEntry *flow,
                                BgpAsAServiceFlowMgmtKey &key);

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
                           const std::string &ace_id);
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
} while (false)

#endif // __AGENT_FLOW_TABLE_MGMT_H__
