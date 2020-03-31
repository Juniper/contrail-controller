/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include "oper/oper_db.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/vn.h"
#include "oper/sg.h"
#include "oper/tag.h"
#include "oper/interface_common.h"
#include "oper/health_check.h"
#include "oper/vrf.h"
#include "oper/vm.h"
#include "oper/physical_device.h"
#include "filter/acl.h"
#include "oper/qos_queue.h"
#include "oper/forwarding_class.h"
#include "oper/qos_config.h"
#include "oper/config_manager.h"
#include "oper/vrouter.h"
#include "oper/bgp_router.h"
#include "oper/global_qos_config.h"
#include "oper/global_system_config.h"
#include "oper/global_vrouter.h"
#include "oper/bridge_domain.h"
#include "filter/policy_set.h"
#include "cfg/cfg_init.h"
#include "oper/security_logging_object.h"
#include "oper/multicast_policy.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include "base/task.h"
#include "base/task_trigger.h"
#include "db/db.h"
#include "db/db_table_partition.h"
#include "db/db_entry.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_dependency_tracker.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace boost::assign;
using namespace std;
typedef IFMapDependencyTracker::ReactionMap ReactionMap;
typedef IFMapDependencyTracker::NodeEventPolicy NodeEventPolicy;
typedef IFMapDependencyTracker::PropagateList PropagateList;

void intrusive_ptr_add_ref(IFMapNodeState *state) {
    ++state->refcount_;
}

void intrusive_ptr_release(IFMapNodeState *state) {
    if (--state->refcount_ ==  0) {
        assert(state->object_ == NULL);
        state->manager_->IFMapNodeReset(state->node_);
        delete state;
    }
}

IFMapDependencyManager::IFMapDependencyManager(DB *database, DBGraph *graph)
        : database_(database),
          graph_(graph) {
    tracker_.reset(
        new IFMapDependencyTracker(
            database, graph,
            boost::bind(&IFMapDependencyManager::ChangeListAdd, this, _1)));
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset(
        new TaskTrigger(
            boost::bind(&IFMapDependencyManager::ProcessChangeList, this),
            task_id, 0));
}

IFMapDependencyManager::~IFMapDependencyManager() {
    // TODO: Unregister from all tables.
}

void IFMapDependencyManager::Initialize(Agent *agent) {
    agent_ = agent;
    static const char *ifmap_types[] = {
        "access-control-list",
        "address-group",
        "alias-ip",
        "alias-ip-pool",
        "application-policy-set",
        "application-policy-set-firewall-policy",
        "bgp-as-a-service",
        "bgpaas-control-node-zone",
        "bgp-router",
        "control-node-zone",
        "firewall-policy",
        "firewall-rule",
        "floating-ip",
        "floating-ip-pool",
        "forwarding-class",
        "global-qos-config",
        "global-system-config",
        "instance-ip",
        "logical-interface",
        "logical-router",
        "network-ipam",
        "physical-interface",
        "physical-router",
        "policy-management",
        "project",
        "qos-config",
        "qos-queue",
        "routing-instance",
        "security-group",
        "security-logging-object",
        "service-group",
        "service-health-check",
        "service-instance",
        "service-template",
        "subnet",
        "tag",
        "virtual-ip",
        "virtual-machine",
        "virtual-machine-interface",
        "virtual-machine-interface-routing-instance",
        "virtual-network",
        "virtual-network-network-ipam",
        "virtual-port-group",
        "virtual-port-group-physical-interface",
        "virtual-DNS",
        "global-vrouter-config",
        "virtual-router",
        "interface-route-table",
        "bridge-domain",
        "virtual-machine-interface-bridge-domain",
        "firewall-policy-firewall-rule",
        "port-tuple",
        "multicast-policy"
    };

    // Link table
    DBTable *link_table = static_cast<DBTable *>(
        database_->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&IFMapDependencyManager::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    // Identifier tables
    const int n_types = sizeof(ifmap_types) / sizeof(const char *);
    for (int i = 0; i < n_types; i++) {
        const char *id_typename = ifmap_types[i];
        IFMapTable *table = IFMapTable::FindTable(database_, id_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
            boost::bind(&IFMapDependencyManager::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }

    // Policy definition
    IFMapDependencyTracker::NodeEventPolicy *policy = tracker_->policy_map();

    ReactionMap react_si = map_list_of<string, PropagateList>
            ("service-instance-service-template", list_of("self")
                .convert_to_container<PropagateList>())
            ("virtual-machine-service-instance", list_of("self")
                .convert_to_container<PropagateList>())
            ("self", list_of("self").convert_to_container<PropagateList>());
    policy->insert(make_pair("service-instance", react_si));

    ReactionMap react_tmpl = map_list_of<string, PropagateList>
            ("self", list_of("service-instance-service-template"));
    policy->insert(make_pair("service-template", react_tmpl));

    ReactionMap react_vm = map_list_of<string, PropagateList>
            ("self", list_of("virtual-machine-service-instance")
                .convert_to_container<PropagateList>())
            ("virtual-machine-virtual-machine-interface",
             list_of("virtual-machine-service-instance")
                 .convert_to_container<PropagateList>())
            ("virtual-machine-interface-virtual-machine",
             list_of("virtual-machine-service-instance")
                 .convert_to_container<PropagateList>());
    policy->insert(make_pair("virtual-machine", react_vm));

    ReactionMap react_vmi = map_list_of<string, PropagateList>
            ("self", list_of("virtual-machine-interface-virtual-machine")
                            ("logical-interface-virtual-machine-interface")
                                .convert_to_container<PropagateList>())
            ("instance-ip-virtual-machine-interface",
             list_of("virtual-machine-interface-virtual-machine")
                 .convert_to_container<PropagateList>())
            ("virtual-machine-interface-virtual-network",
             list_of("virtual-machine-interface-virtual-machine")
                    ("logical-interface-virtual-machine-interface")
                        .convert_to_container<PropagateList>());
    policy->insert(make_pair("virtual-machine-interface", react_vmi));

    ReactionMap react_ip = map_list_of<string, PropagateList>
            ("self", list_of("instance-ip-virtual-machine-interface"));
    policy->insert(make_pair("instance-ip", react_ip));

    ReactionMap react_subnet = map_list_of<string, PropagateList>
            ("self", list_of("virtual-network-network-ipam")
                .convert_to_container<PropagateList>())
            ("virtual-network-network-ipam", list_of("nil")
                .convert_to_container<PropagateList>());
    policy->insert(make_pair("virtual-network-network-ipam", react_subnet));

    ReactionMap react_ipam = map_list_of<string, PropagateList>
            ("virtual-network-network-ipam", list_of("nil"));
    policy->insert(make_pair("network-ipam", react_ipam));

    ReactionMap react_bgpaas = map_list_of<string, PropagateList>
            ("bgpaas-control-node-zone",
            list_of("bgpaas-virtual-machine-interface"));
    policy->insert(make_pair("bgp-as-a-service", react_bgpaas));

    InitializeDependencyRules(agent);
}

void IFMapDependencyManager::RegisterReactionMap
(const char *node_name, const IFMapDependencyTracker::ReactionMap &react) {
    // Register to the IFMap table
    IFMapTable *table = IFMapTable::FindTable(database_, node_name);
    assert(table);
    DBTable::ListenerId id = table->Register
        (boost::bind(&IFMapDependencyManager::NodeObserver, this, _1, _2));
    table_map_.insert(make_pair(table->name(), id));

    // Add Policy
    tracker_->policy_map()->insert(make_pair(node_name, react));
}

void IFMapDependencyManager::Terminate() {
    for (TableMap::iterator iter = table_map_.begin();
         iter != table_map_.end(); ++iter) {
        DBTable *table = static_cast<DBTable *>(
            database_->FindTable(iter->first));
        table->Unregister(iter->second);
    }
    table_map_.clear();
    event_map_.clear();
}

bool IFMapDependencyManager::ProcessChangeList() {
    tracker_->PropagateChanges();
    tracker_->Clear();

    for (ChangeList::iterator iter = change_list_.begin();
         iter != change_list_.end(); ++iter) {
        IFMapNodeState *state = iter->get();
        IFMapTable *table = state->node()->table();
        EventMap::iterator loc = event_map_.find(table->Typename());
        if (loc == event_map_.end()) {
            continue;
        }

        if (state->notify() == true) {
            loc->second(state->node(), state->object());
        }
    }
    change_list_.clear();
    return true;
}

void IFMapDependencyManager::NodeObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {

    IFMapNode *node = static_cast<IFMapNode *>(db_entry);
    tracker_->NodeEvent(node);
    trigger_->Set();
}

void IFMapDependencyManager::PropogateNodeChange(IFMapNode *node) {
    tracker_->NodeEvent(node, false);
    trigger_->Set();
}

void IFMapDependencyManager::PropogateNodeAndLinkChange(IFMapNode *node) {

    tracker_->NodeEvent(node);

    for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph_);
                                iter != node->edge_list_end(graph_); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        IFMapNode *target = static_cast<IFMapNode *>(iter.target());
        tracker_->LinkEvent(link->metadata(), node, target);
    }

    trigger_->Set();
}

void IFMapDependencyManager::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database_);
    IFMapNode *right = link->RightNode(database_);
    bool set = false;
    if (left) {
        EventMap::iterator loc = event_map_.find(left->table()->Typename());
        if (loc != event_map_.end()) {
            ChangeListAdd(left);
            set = true;
        }
    }

    if (right) {
        EventMap::iterator loc = event_map_.find(right->table()->Typename());
        if (loc != event_map_.end()) {
            ChangeListAdd(right);
            set = true;
        }
    }

    set |= tracker_->LinkEvent(link->metadata(), left, right);
    if (set) {
        trigger_->Set();
    }
}

void IFMapDependencyManager::ChangeListAdd(IFMapNode *node) {
    IFMapNodeState *state = IFMapNodeGet(node);
    if (state == NULL) {
        if (node->IsDeleted() == false)
            change_list_.push_back(SetState(node));
    } else {
        change_list_.push_back(IFMapNodePtr(state));
    }
}

IFMapNodeState *
IFMapDependencyManager::IFMapNodeGet(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    if (loc == table_map_.end()) {
        return NULL;
    }
    IFMapNodeState *state =
            static_cast<IFMapNodeState *>(node->GetState(table, loc->second));
    return state;
}

void IFMapDependencyManager::IFMapNodeReset(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    assert(loc != table_map_.end());
    node->ClearState(node->table(), loc->second);
}

/*
 * Associate an IFMapNode with an object in the operational database.
 *
 * IFMapNodes that do not have an object mapping do not receive notifications.
 * This method checks whether the DBState exists and delegates the
 * responsibility to create a new mapping to IFMapNodeSet (private).
 */
void IFMapDependencyManager::SetObject(IFMapNode *node, DBEntry *entry) {

    IFMapNodeState *state = IFMapNodeGet(node);
    assert(state);

    DBEntry *old_entry = state->object();

    if (old_entry)
        state->clear_object();

    if (entry) {
        state->set_object(entry);
        tracker_->NodeEvent(node);
        trigger_->Set();
    }
}

void IFMapDependencyManager::SetNotify(IFMapNode *node, bool notify_flag) {

    IFMapNodeState *state = IFMapNodeGet(node);
    assert(state);

    state->set_notify(notify_flag);
}

void IFMapDependencyManager::SetRequestEnqueued(IFMapNode *node,
        bool oper_db_request_enqueued) {

    IFMapNodeState *state = IFMapNodeGet(node);
    assert(state);

    state->set_oper_db_request_enqueued(oper_db_request_enqueued);
}

IFMapDependencyManager::IFMapNodePtr
IFMapDependencyManager::SetState(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    if (loc == table_map_.end())
        return NULL;

    IFMapNodeState *state =
            static_cast<IFMapNodeState *>(node->GetState(table, loc->second));

    if (!state) {
        state = new IFMapNodeState(this, node);
        node->SetState(table, loc->second, state);
    }
    return IFMapNodePtr(state);
}

DBEntry *IFMapDependencyManager::GetObject(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    if (loc == table_map_.end())
        return NULL;

    IFMapNodeState *state =
        static_cast<IFMapNodeState *>(node->GetState(table, loc->second));
    if (state == NULL)
        return NULL;

    return state->object();
}

/*
 * Register a notification callback.
 */
void IFMapDependencyManager::Register(
    const string &type, ChangeEventHandler handler) {
    event_map_.insert(std::make_pair(type, handler));
}

/*
 * Unregister a notification callback.
 */
void IFMapDependencyManager::Unregister(const string &type) {
    event_map_.erase(type);
}

// Check if a IFMapNode type is registerd with dependency manager
bool IFMapDependencyManager::IsRegistered(const IFMapNode *node) {
    EventMap::iterator it = event_map_.find(node->table()->Typename());
    return (it != event_map_.end());
}

bool IFMapDependencyManager::IsNodeIdentifiedByUuid(const IFMapNode *node) {
    return (strcmp(node->table()->Typename(), "routing-instance") != 0);
}

IFMapDependencyManager::Path MakePath
(const char *link1,        const char *node1,        bool interest1,
 const char *link2 = NULL, const char *node2 = NULL, bool interest2 = false,
 const char *link3 = NULL, const char *node3 = NULL, bool interest3 = false,
 const char *link4 = NULL, const char *node4 = NULL, bool interest4 = false,
 const char *link5 = NULL, const char *node5 = NULL, bool interest5 = false,
 const char *link6 = NULL, const char *node6 = NULL, bool interest6 = false) {
    if (link2) assert(node2);
    if (link3) assert(node3);
    if (link4) assert(node4);
    if (link5) assert(node5);
    if (link6) assert(node6);

    IFMapDependencyManager::Path path;
    path.push_back(IFMapDependencyManager::Link(link1, node1, interest1));
    if (link2)
        path.push_back(IFMapDependencyManager::Link(link2, node2, interest2));
    if (link3)
        path.push_back(IFMapDependencyManager::Link(link3, node3, interest3));
    if (link4)
        path.push_back(IFMapDependencyManager::Link(link4, node4, interest4));
    if (link5)
        path.push_back(IFMapDependencyManager::Link(link5, node5, interest5));
    if (link6)
        path.push_back(IFMapDependencyManager::Link(link6, node6, interest6));
    return path;
}

static NodeEventPolicy::iterator LocateNodeEventPolicy(NodeEventPolicy *policy,
                                                       const string &node) {
    NodeEventPolicy::iterator it = policy->find(node);
    if (it != policy->end()) {
        return it;
    }
    ReactionMap react = {{"self", PropagateList()}};
    policy->insert(make_pair(node, react));
    return policy->find(node);
}

static ReactionMap::iterator LocateReactionMap(ReactionMap *react,
                                               const string &event) {
    ReactionMap::iterator it = react->find(event);
    if (it != react->end()) {
        return it;
    }
    react->insert(make_pair(event, PropagateList()));
    return react->find(event);
}

////////////////////////////////////////////////////////////////////////
// The IFMap dependency tracker accepts rules in the form or "Reactor Map"
// Reactor map is not natural way of defining references between IFMap nodes
// in agent.
//
// When agent builds data for an oper-db entry, the IFNodeToReq API will keep
// the IFMapNode for oper-db entry as starting node and traverses the graph
// to build all realvent data for the entry. So, it is more natural in agent
// to specify relation between nodes in a way IFMapNodeToReq traverses the
// graph
//
// The routine AddDependencyPath takes the graph path between two vertices
// and then creates "Reactor Map" for them.
//
// The API takes IFMapNode for object being built as "node" and then a graph
// path to reach another IFMapNode of interest.
//
// Example: VMI refers to RI with following path to support floating-ip
//     VMI <----> FIP <----> FIP-POOL <----> VN <----> RI
//
//     node is specified as "virtual-machine-interface"
//     path is specified as,
//          <string(link-metadata), string(vertex), bool(vertex-attr-interest)>
//
//          vertex specifies name of the adjacent node in the path
//          link-metadata specifies metadata of the link connecting the vertex
//          vertex-attr-interest
//               If "node" is interested in any attribute of vertex, this value
//               is set to TRUE else its set to FALSE
//               When value is set to TRUE, it will generate an event of type
//               "self" in the reactor-map
//
//          The path from virtual-machine-interface to "routing-instance" above
//          is given as below,
//
//          <"virtual-machine-interface-floating-ip", "floating-ip", true>
//          <"floating-ip-floating-ip-pool", "floating-ip-pool", false>
//          <"floating-ip-pool-virtual-network", "virtual-network", true>
//          <"virtual-network-routing-instance", "routing-instance", true>
//
// AddDependencyPath will "APPEND" following Reactor rules,
//      node(virtual-machine-interface)
//             event(virtual-machine-floating-ip) => Rectors(self)
//
//      node(routing-instance)
//             event(self) => Reactors(virtual-network-routing-instance)
//
//      node(virtual-network)
//             event(virtual-network-routing-instance) => Reactors(floating-ip-pool-virtual-network)
//
//      node(floating-ip-pool)
//             event(self) => Reactors(virtual-machine-interface-virtual-network)
//             event(floating-ip-pool-virtual-network) => Reactors(floating-ip-floating-ip-pool)
//
//      node(floating-ip)
//             event(self) => Reactors(virtual-machine-interface-floating-ip)
//             event(floating-ip-floating-ip-pool) => Reactors(virtual-machine-interface-floating-ip)
//
////////////////////////////////////////////////////////////////////////
void IFMapDependencyManager::AddDependencyPath(const std::string &node,
                                               Path path) {
    NodeEventPolicy *policy = tracker_->policy_map();
    assert(policy);

    NodeEventPolicy::iterator node_it;
    node_it = LocateNodeEventPolicy(policy, node);
    ReactionMap::iterator react_it = LocateReactionMap(&node_it->second,
                                                       path[0].edge_);
    LOG(DEBUG, "Updating dependency tacker rules for " << node);
    react_it->second.insert("self");
    LOG(DEBUG, "Adding ReactorMap " << node_it->first << " : " <<
                react_it->first << " -> " << "self");

    for (size_t i = 0; i < path.size(); i++) {
        node_it = LocateNodeEventPolicy(policy, path[i].vertex_);
        if (path[i].vertex_interest_) {
            react_it = LocateReactionMap(&node_it->second, "self");
            react_it->second.insert(path[i].edge_);
            LOG(DEBUG, "Adding ReactorMap " << node_it->first << " : " <<
                react_it->first << " -> " << path[i].edge_);
        }

        if (i < (path.size() - 1)) {
            react_it = LocateReactionMap(&node_it->second, path[i+1].edge_);
            react_it->second.insert(path[i].edge_);
            LOG(DEBUG, "Adding ReactorMap " << node_it->first << " : " <<
                react_it->first << " -> " << path[i].edge_);
        } else {
            react_it = LocateReactionMap(&node_it->second, path[i].edge_);
            react_it->second.insert("nil");
            LOG(DEBUG, "Adding ReactorMap " << node_it->first << " : " <<
                react_it->first << " -> " << "nil");
        }
    }

    return;
}

// Register callback for ifmap node not having corresponding oper-dbtable
static void RegisterConfigHandler(IFMapDependencyManager *dep,
                                  const char *name, OperIFMapTable *table) {
    if (table)
        dep->Register(name, boost::bind(&OperIFMapTable::ConfigEventHandler,
                                        table, _1, _2));
}

static void RegisterConfigHandler(IFMapDependencyManager *dep,
                                  const char *name, AgentOperDBTable *table) {
    if (table)
        dep->Register(name, boost::bind(&AgentOperDBTable::ConfigEventHandler,
                                        table, _1, _2));
}

void IFMapDependencyManager::InitializeDependencyRules(Agent *agent) {

    RegisterConfigHandler(this, "virtual-machine",
                          agent ? agent->vm_table() : NULL);

    RegisterConfigHandler(this, "access-control-list",
                          agent ? agent->acl_table() : NULL);

    RegisterConfigHandler(this, "multicast-policy",
                          agent ? agent->mp_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // VN <----> RI
    //    <----> ACL
    //    <----> VN-IPAM <----> IPAM
    //    <----> SecurityLoggingObject
    //    <----> Multicast Policy
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-routing-instance",
                               "routing-instance", true));
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-access-control-list",
                               "access-control-list", true));
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-network-ipam",
                               "virtual-network-network-ipam", true,
                               "virtual-network-network-ipam",
                               "network-ipam", false));
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-qos-config",
                               "qos-config", true));
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-security-logging-object",
                               "security-logging-object", true));
    AddDependencyPath("virtual-network",
                      MakePath("logical-router-virtual-network",
                               "logical-router-virtual-network", true,
                               "logical-router-virtual-network",
                               "logical-router", true));
    AddDependencyPath("virtual-network",
                      MakePath("virtual-network-multicast-policy",
                               "multicast-policy", true));
    RegisterConfigHandler(this, "virtual-network",
                          agent ? agent->vn_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // RI <----> VN
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("routing-instance",
                      MakePath("virtual-network-routing-instance",
                               "virtual-network", true,
                               "virtual-network-provider-network",
                               "virtual-network", true));
    RegisterConfigHandler(this, "routing-instance",
                          agent ? agent->vrf_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // SG <----> ACL
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("security-group",
                      MakePath("security-group-access-control-list",
                               "access-control-list", true));
    RegisterConfigHandler(this, "security-group",
                         agent ? agent->sg_table() : NULL);

    RegisterConfigHandler(this, "tag",
                         agent ? agent->tag_table() : NULL);
    AddDependencyPath("tag",
                      MakePath("application-policy-set-tag",
                               "application-policy-set", true,
                               "policy-management-application-policy-set",
                               "policy-management", false));
    ////////////////////////////////////////////////////////////////////////
    // VMI <----> VN
    //     <----> VM
    //     <----> VMI-RI <----> RI
    //     <----> SG
    //     //<----> FIP <----> FIP-POOL <----> VN <----> RI
    //     <----> FIP <----> FIP-POOL <----> VN
    //     <----> FIP <----> Instance-IP <----> VN
    //     <----> Instance-IP
    //     <----> interface-route-table
    //     <----> subnet
    //     //<----> vn <----> VN-IPAM <----> IPAM
    //     <----> LI <----> physical-interface <----> physical-router
    //     <----> SecurityLoggingObject
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-virtual-network",
                               "virtual-network", true,
                               "virtual-network-network-ipam",
                               "virtual-network-network-ipam", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-virtual-machine",
                               "virtual-machine", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-sub-interface",
                               "virtual-machine-interface", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-virtual-network",
                               "virtual-network", true,
                               "virtual-network-provider-network",
                               "virtual-network", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-routing-instance",
                               "virtual-machine-interface-routing-instance",
                               true,
                               "virtual-machine-interface-routing-instance",
                               "routing-instance", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-security-group",
                               "security-group", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("alias-ip-virtual-machine-interface",
                               "alias-ip", true,
                               "alias-ip-pool-alias-ip",
                               "alias-ip-pool", false,
                               "virtual-network-alias-ip-pool",
                               "virtual-network", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("floating-ip-virtual-machine-interface",
                               "floating-ip", true,
                               "floating-ip-pool-floating-ip",
                               "floating-ip-pool", false,
                               "virtual-network-floating-ip-pool",
                               "virtual-network", true,
                               "virtual-network-provider-network",
                               "virtual-network", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("floating-ip-virtual-machine-interface",
                               "floating-ip", true,
                               "floating-ip-pool-floating-ip",
                               "floating-ip-pool", false,
                               "virtual-network-floating-ip-pool",
                               "virtual-network", true,
                               "virtual-network-routing-instance",
                               "routing-instance", true));
     AddDependencyPath("virtual-machine-interface",
                       MakePath("floating-ip-virtual-machine-interface",
                                "floating-ip", true,
                                "instance-ip-floating-ip",
                                "instance-ip", false,
                                "instance-ip-virtual-network",
                                "virtual-network", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("instance-ip-virtual-machine-interface",
                               "instance-ip", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-route-table",
                               "interface-route-table", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("subnet-virtual-machine-interface",
                               "subnet", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("logical-interface-virtual-machine-interface",
                               "logical-interface", false,
                               "physical-interface-logical-interface",
                               "physical-interface", false,
                               "physical-router-physical-interface",
                               "physical-router", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-port-group-virtual-machine-interface",
                               "virtual-port-group", false,
                               "virtual-port-group-physical-interface",
                               "physical-interface", false,
                               "physical-router-physical-interface",
                               "physical-router", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("bgpaas-virtual-machine-interface",
                               "bgp-as-a-service", true,
                               "bgpaas-bgp-router",
                               "bgp-router", true,
                               "instance-bgp-router",
                               "routing-instance", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("bgpaas-virtual-machine-interface",
                               "bgp-as-a-service", true,
                               "bgpaas-health-check",
                               "service-health-check", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-qos-config",
                          "qos-config", true));
    AddDependencyPath("virtual-machine-interface",
                   MakePath("virtual-machine-interface-security-logging-object",
                            "security-logging-object", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-bridge-domain",
                               "virtual-machine-interface-bridge-domain",
                               true,
                               "virtual-machine-interface-bridge-domain",
                               "bridge-domain", true,
                               "virtual-network-bridge-domain",
                               "virtual-network", true));
    //Above rule should suffice for VMI to act on bridge domain
    //"virtual-machine-interface-bridge-domain" link is used to
    //link both VMI and bridge domain, so this link could notify
    //bridge-domain which doesnt have any reaction list as bridge-domain
    //is not intererested in "virtual-machine-interface-bridge-domain".
    //VMI ---> VMI-BD Link ---> VMI-BD node ----> VMI-BD link ---> BD
    //Note that link name is same in above graph
    //Add the below dummy rule so that VMI-BD link triggers
    //bridge-domain evaluation its ignored and avoids assert
    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-bridge-domain",
                               "virtual-machine-interface-bridge-domain",
                               true,
                               "virtual-machine-interface-bridge-domain",
                               "bridge-domain", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-tag",
                               "tag", true,
                               "application-policy-set-tag",
                               "application-policy-set", true,
                               "application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-virtual-network",
                               "virtual-network", true,
                               "virtual-network-tag", "tag", true,
                               "application-policy-set-tag",
                               "application-policy-set", true,
                               "application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));


    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-virtual-machine",
                               "virtual-machine", true,
                               "virtual-machine-tag", "tag", true,
                               "application-policy-set-tag",
                               "application-policy-set", true,
                               "application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("virtual-machine-interface-sub-interface",
                               "virtual-machine-interface", true,
                               "virtual-machine-interface-virtual-machine",
                               "virtual-machine", true,
                               "virtual-machine-tag", "tag", true,
                               "application-policy-set-tag",
                               "application-policy-set", true,
                               "application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));

    AddDependencyPath("virtual-machine-interface",
                      MakePath("project-virtual-machine-interface",
                               "project", true,
                               "project-tag", "tag", true,
                               "application-policy-set-tag",
                               "application-policy-set", true,
                               "application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));

    /* Trigger change on virtual-machine-interface, if there is change in
     * port-tuple configuration */
    AddDependencyPath("virtual-machine-interface",
                      MakePath("port-tuple-interface",
                               "port-tuple", true,
                               "port-tuple-interface",
                               "virtual-machine-interface", true));
    AddDependencyPath("virtual-machine-interface",
                      MakePath("logical-router-interface",
                               "logical-router", true));

    RegisterConfigHandler(this, "virtual-machine-interface",
                          agent ? agent->interface_table() : NULL);
    ////////////////////////////////////////////////////////////////////////
    // physical-interface <----> physical-router
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("physical-interface",
                      MakePath("physical-router-physical-interface",
                               "physical-router", true));
    ////////////////////////////////////////////////////////////////////////
    // physical-interface <----> virtual-port-group
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("physical-interface",
                    MakePath("virtual-port-group-physical-interface",
                             "virtual-port-group-physical-interface", true,
                             "virtual-port-group-physical-interface",
                             "virtual-port-group", true));
    RegisterConfigHandler(this, "physical-interface",
                          agent ? agent->interface_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // logical-interface <----> physical-interface <----> physical-router
    //                   <----> virtual-machine-interface <---> virtual-network
    //
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("logical-interface",
                      MakePath("physical-interface-logical-interface",
                               "physical-interface", true,
                               "physical-router-physical-interface",
                               "physical-router", true));
    AddDependencyPath("logical-interface",
                      MakePath("logical-interface-virtual-machine-interface",
                               "virtual-machine-interface", true));
    AddDependencyPath("logical-interface",
                      MakePath("physical-router-logical-interface",
                               "physical-router", true));
    RegisterConfigHandler(this, "logical-interface",
                          agent ? agent->interface_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // physical-router <----> physical-interface
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("physical-router",
                      MakePath("physical-router-physical-interface",
                               "physical-interface", true));
    RegisterConfigHandler(this, "physical-router",
                          agent ? agent->physical_device_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // virtual-machine-interface <----> service-health-check
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("service-health-check",
                      MakePath("service-port-health-check",
                               "virtual-machine-interface", true,
                               "virtual-machine-interface-virtual-network",
                               "virtual-network", false,
                               "virtual-network-network-ipam",
                               "virtual-network-network-ipam", true));
    AddDependencyPath("service-health-check",
                      MakePath("service-port-health-check",
                               "virtual-machine-interface", true,
                               "port-tuple-interface",
                               "port-tuple", true,
                               "port-tuple-interface",
                               "virtual-machine-interface", true,
                               "virtual-machine-interface-virtual-network",
                               "virtual-network", false,
                               "virtual-network-network-ipam",
                               "virtual-network-network-ipam", true));
    RegisterConfigHandler(this, "service-health-check",
                          agent ? agent->health_check_table() : NULL);

    AddDependencyPath("qos-config",
                       MakePath("global-qos-config-qos-config",
                                "global-qos-config", false));
    RegisterConfigHandler(this, "qos-config",
                          agent ? agent->qos_config_table() : NULL);

    RegisterConfigHandler(this, "qos-queue",
                          agent ? agent->qos_queue_table() : NULL);

    AddDependencyPath("forwarding-class",
                       MakePath("forwarding-class-qos-queue",
                                "qos-queue", true));
    RegisterConfigHandler(this, "forwarding-class",
                          agent ? agent->forwarding_class_table() : NULL);

    ////////////////////////////////////////////////////////////////////////
    // security-logging-object <----> network-policy
    //                         <----> security-group
    ////////////////////////////////////////////////////////////////////////
    AddDependencyPath("security-logging-object",
                       MakePath("security-logging-object-network-policy",
                                "network-policy", true));
    AddDependencyPath("security-logging-object",
                       MakePath("security-logging-object-security-group",
                                "security-group", true));
    AddDependencyPath("security-logging-object",
                      MakePath("firewall-policy-security-logging-object",
                               "firewall-policy-security-logging-object", true,
                               "firewall-policy-security-logging-object",
                                "firewall-policy", false));
    AddDependencyPath("security-logging-object",
                       MakePath("firewall-rule-security-logging-object",
                                "firewall-rule-security-logging-object", true,
                                "firewall-rule-security-logging-object",
                                "firewall-rule", false));
    RegisterConfigHandler(this, "security-logging-object",
                          agent ? agent->slo_table() : NULL);

    // Register callback for ifmap node not having corresponding oper-dbtable
    RegisterConfigHandler(this, "virtual-router", agent->oper_db()->vrouter());
    RegisterConfigHandler(this, "global-qos-config",
                          agent->oper_db()->global_qos_config());
    RegisterConfigHandler(this, "global-system-config",
                          agent->oper_db()->global_system_config());
    RegisterConfigHandler(this, "network-ipam",
                          agent->oper_db()->network_ipam());
    RegisterConfigHandler(this, "virtual-DNS",
                          agent->oper_db()->virtual_dns());
    RegisterConfigHandler(this, "global-vrouter-config",
                          agent->oper_db()->global_vrouter());
    RegisterConfigHandler(this, "bgp-router",
                          agent->oper_db()->bgp_router_config());
    AddDependencyPath("bridge-domain",
                      MakePath("virtual-network-bridge-domain",
                               "virtual-network", true,
                               "virtual-network-routing-instance",
                               "routing-instance", true));
    AddDependencyPath("bridge-domain",
                      MakePath("virtual-network-bridge-domain",
                      "virtual-network", true));

    RegisterConfigHandler(this, "bridge-domain",
                          agent ? agent->bridge_domain_table() : NULL);

    AddDependencyPath("application-policy-set",
                      MakePath("application-policy-set-firewall-policy",
                               "application-policy-set-firewall-policy", true,
                               "application-policy-set-firewall-policy",
                               "firewall-policy", true));
    RegisterConfigHandler(this, "application-policy-set",
                          agent ? agent->policy_set_table() : NULL);

    AddDependencyPath("firewall-policy",
                      MakePath("firewall-policy-firewall-rule",
                               "firewall-policy-firewall-rule", true,
                               "firewall-policy-firewall-rule",
                               "firewall-rule", true,
                               "firewall-rule-service-group",
                               "service-group", true));
    AddDependencyPath("firewall-policy",
                      MakePath("firewall-policy-firewall-rule",
                               "firewall-policy-firewall-rule", true,
                               "firewall-policy-firewall-rule",
                                "firewall-rule", true,
                                "firewall-rule-address-group",
                                "address-group", true,
                                "address-group-tag", "tag", "true"));

    AddDependencyPath("firewall-policy",
                      MakePath("firewall-policy-firewall-rule",
                               "firewall-policy-firewall-rule", true,
                               "firewall-policy-firewall-rule",
                               "firewall-rule", true,
                               "firewall-rule-tag",
                               "tag", true));

    AddDependencyPath("firewall-policy",
                      MakePath("firewall-policy-firewall-rule",
                               "firewall-policy-firewall-rule", true,
                               "firewall-policy-firewall-rule",
                               "firewall-rule", true));

    RegisterConfigHandler(this, "firewall-policy",
                          agent ? agent->acl_table() : NULL);
}

void IFMapNodePolicyReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    IFMapNodePolicyResp *resp = new IFMapNodePolicyResp();

    IFMapDependencyTracker::NodeEventPolicy *policy_list =
        agent->oper_db()->dependency_manager()->tracker()->policy_map();
    NodeEventPolicy::iterator it1 = policy_list->begin();
    std::vector<IFMapNodePolicy> resp_policy_list;
    while (it1 != policy_list->end()) {
        if (it1->first.find(get_node()) == string::npos) {
            it1++;
            continue;
        }

        ReactionMap::iterator it2 = it1->second.begin();
        vector<IFMapReactEvent> resp_event_list;
        while(it2 != it1->second.end()) {
            IFMapReactEvent resp_event;
            vector<string> resp_reactor_list;
            PropagateList::iterator it3 = it2->second.begin();
            while (it3 != it2->second.end()) {
                resp_reactor_list.push_back((*it3));
                it3++;
            }
            resp_event.set_event(it2->first);
            resp_event.set_reactors(resp_reactor_list);
            resp_event_list.push_back(resp_event);
            it2++;
        }

        IFMapNodePolicy policy;
        policy.set_node(it1->first);
        policy.set_events(resp_event_list);
        resp_policy_list.push_back(policy);
        it1++;
    }

    resp->set_policies(resp_policy_list);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
    return;
}
