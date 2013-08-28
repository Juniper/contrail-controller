/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "bgp/bgp_config_listener.h"
#include "bgp/routing-instance/routing_instance.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"

using namespace std;

const char *BgpConfigManager::kMasterInstance =
        "default-domain:default-project:ip-fabric:__default__";
const int BgpConfigManager::kDefaultPort = 179;
const as_t BgpConfigManager::kDefaultAutonomousSystem = 64512;

BgpNeighborConfig::AddressFamilyList
    BgpNeighborConfig::default_addr_family_list_;

void BgpNeighborConfig::Initialize() {
    default_addr_family_list_.push_back("inet");
}

MODULE_INITIALIZER(BgpNeighborConfig::Initialize);

static string IdentifierParent(const string &identifier) {
    string parent;
    size_t last;
    last = identifier.rfind(':');
    if (last == 0 || last == string::npos) {
        return parent;
    }
    parent = identifier.substr(0, last);
    return parent;
}

BgpConfigDelta::BgpConfigDelta() {
}

BgpConfigDelta::BgpConfigDelta(const BgpConfigDelta &rhs)
    : id_type(rhs.id_type), id_name(rhs.id_name),
      node(rhs.node), obj(rhs.obj) {
}

BgpConfigDelta::~BgpConfigDelta() {
}

template<>
void BgpConfigManager::Notify<BgpInstanceConfig>(
        const BgpInstanceConfig *config, EventType event) {
    if (obs_.instance) {
        (obs_.instance)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpProtocolConfig>(
        const BgpProtocolConfig *config, EventType event) {
    if (obs_.protocol) {
        (obs_.protocol)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpNeighborConfig>(
        const BgpNeighborConfig *config, EventType event) {
    if (obs_.neighbor) {
        (obs_.neighbor)(config, event);
    }
}

void BgpPeeringConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
        name_ = node_proxy_.node()->name();
    }
}

void BgpPeeringConfig::BuildNeighbors(BgpConfigManager *manager,
        const string &peername, const autogen::BgpRouter *rt_config,
        const autogen::BgpPeering *peering, NeighborMap *map) {
    const autogen::BgpPeeringAttributes &attr = peering->data();
    for (autogen::BgpPeeringAttributes::const_iterator iter = attr.begin();
            iter != attr.end(); ++iter) {
        BgpNeighborConfig *neighbor =
                new BgpNeighborConfig(instance_, peername,
                        manager->localname(), rt_config,
                        iter.operator->());
        map->insert(make_pair(neighbor->name(), neighbor));
    }

    // When no sessions are present, create a single neighbor with no
    // per-session configuration.
    if (map->empty()) {
        BgpNeighborConfig *neighbor =
                new BgpNeighborConfig(instance_, peername,
                        manager->localname(),
                        rt_config, NULL);
        map->insert(make_pair(neighbor->name(), neighbor));
    }
}

void BgpPeeringConfig::Update(BgpConfigManager *manager,
        const autogen::BgpPeering *peering) {
    IFMapNode *node = node_proxy_.node();
    assert(node != NULL);
    bgp_peering_.reset(peering);

    NeighborMap future;
    pair<IFMapNode *, IFMapNode *> routers;
    if (!node->IsDeleted() &&
            GetRouterPair(manager->graph(), manager->localname(), node,
                    &routers)) {
        const autogen::BgpRouter *rt_config =
                static_cast<const autogen::BgpRouter *>(
                        routers.second->GetObject());
        if (rt_config != NULL &&
                rt_config->IsPropertySet(autogen::BgpRouter::PARAMETERS)) {
            BuildNeighbors(manager, routers.second->name(), rt_config,
                    peering, &future);
        }
    }

    NeighborMap current;
    current.swap(neighbors_);
    NeighborMap::iterator it1 = current.begin();
    NeighborMap::iterator it2 = future.begin();

    while (it1 != current.end() && it2 != future.end()) {
        if (it1->first < it2->first) {
            BgpNeighborConfig *prev = it1->second;
            instance_->NeighborDelete(manager, prev);
            ++it1;
        } else if (it1->first > it2->first) {
            BgpNeighborConfig *neighbor = it2->second;
            instance_->NeighborAdd(manager, neighbor);
            neighbors_.insert(*it2);
            it2->second = NULL;
            ++it2;
        } else {
            BgpNeighborConfig *neighbor = it1->second;
            BgpNeighborConfig *update = it2->second;
            if (*neighbor != *update) {
                neighbor->Update(update);
                instance_->NeighborChange(manager, neighbor);
            }
            neighbors_.insert(*it1);
            it1->second = NULL;
            ++it1;
            ++it2;
        }
    }
    for (; it1 != current.end(); ++it1) {
        BgpNeighborConfig *prev = it1->second;
        instance_->NeighborDelete(manager, prev);
    }
    for (; it2 != future.end(); ++it2) {
        BgpNeighborConfig *neighbor = it2->second;
        instance_->NeighborAdd(manager, neighbor);
        neighbors_.insert(*it2);
        it2->second = NULL;
    }
    STLDeleteElements(&current);
    STLDeleteElements(&future);
}

void BgpPeeringConfig::Delete(BgpConfigManager *manager) {
    NeighborMap current;
    current.swap(neighbors_);
    for (NeighborMap::iterator iter = current.begin();
            iter != current.end(); ++iter) {
        instance_->NeighborDelete(manager, iter->second);
    }
    STLDeleteElements(&current);
    bgp_peering_.reset();
}

bool BgpPeeringConfig::GetRouterPair(DBGraph *db_graph,
        const string  &localname, IFMapNode *node,
        pair<IFMapNode *, IFMapNode *> *pair) {
    IFMapNode *local = NULL;
    IFMapNode *remote = NULL;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(db_graph);
            iter != node->end(db_graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "bgp-router") != 0)
            continue;
        string instance_name(IdentifierParent(adj->name()));
        string name = adj->name().substr(instance_name.size() + 1);
        if (name == localname) {
            local = adj;
        } else {
            remote = adj;
        }
    }
    if (local == NULL || remote == NULL) {
        return false;
    }
    pair->first = local;
    pair->second = remote;
    return true;
}

BgpNeighborConfig::BgpNeighborConfig(const BgpInstanceConfig *instance,
                                     const string &remote_name,
                                     const string &local_name,
                                     const autogen::BgpRouter *router,
                                     const autogen::BgpSession *session)
        : instance_(instance) {
    const autogen::BgpRouterParams &params = router->parameters();
    if (session && !session->uuid.empty()) {
        uuid_ = session->uuid;
        name_ = remote_name + ":" + session->uuid;
    } else {
        name_ = remote_name;
    }

    peer_config_.Copy(params);
    attributes_.Clear();
    if (session != NULL) {
        SetSessionAttributes(local_name, session);
    }
}

BgpNeighborConfig::~BgpNeighborConfig() {
}

void BgpNeighborConfig::SetSessionAttributes(const string &localname,
        const autogen::BgpSession *session) {
    typedef std::vector<autogen::BgpSessionAttributes> AttributeVec;
    const autogen::BgpSessionAttributes *common = NULL;
    const autogen::BgpSessionAttributes *local = NULL;
    for (AttributeVec::const_iterator iter =
                 session->attributes.begin();
         iter != session->attributes.end(); ++iter) {
        const autogen::BgpSessionAttributes *attr = iter.operator->();
        if (attr->bgp_router.empty()) {
            common = attr;
        } else if (attr->bgp_router == localname) {
            local = attr;
        }
    }
    // TODO: local should override rather than replace common.
    if (common != NULL) {
        attributes_ = *common;
    }
    if (local != NULL) {
        attributes_ = *local;
    }
}

const BgpNeighborConfig::AddressFamilyList &
BgpNeighborConfig::address_families() const {
    const autogen::BgpSessionAttributes &attr = session_attributes();
    if (!attr.address_families.family.empty()) {
        return attr.address_families.family;
    }

    const BgpProtocolConfig *bgp_config = instance_->bgp_config();
    if (bgp_config && bgp_config->bgp_router()) {
        const autogen::BgpRouterParams &params = bgp_config->router_params();
        if (!params.address_families.family.empty()) {
            return params.address_families.family;
        }
    }

    return default_addr_family_list_;
}

bool BgpNeighborConfig::operator!=(const BgpNeighborConfig &rhs) const {
    // TODO: compare peer_config_ and attributes_
    return true;
}

void BgpNeighborConfig::Update(const BgpNeighborConfig *rhs) {
    peer_config_ = rhs->peer_config_;
    attributes_ = rhs->attributes_;
}

string BgpNeighborConfig::InstanceName() const {
    return instance_->name();
}

BgpProtocolConfig::BgpProtocolConfig(BgpInstanceConfig *instance)
        : instance_(instance) {
}

BgpProtocolConfig::~BgpProtocolConfig() {
}

void BgpProtocolConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

void BgpProtocolConfig::Update(BgpConfigManager *manager,
                               const autogen::BgpRouter *router) {
    bgp_router_.reset(router);
}

void BgpProtocolConfig::Delete(BgpConfigManager *manager) {
    manager->Notify(this, BgpConfigManager::CFG_DELETE);
    bgp_router_.reset();
}

const string &BgpProtocolConfig::InstanceName() const {
    return instance_->name();
}

BgpInstanceConfig::BgpInstanceConfig(const string &name)
    : name_(name), bgp_router_(NULL) {
}

BgpInstanceConfig::~BgpInstanceConfig() {
}

void BgpInstanceConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

BgpProtocolConfig *BgpInstanceConfig::BgpConfigLocate() {
    if (bgp_router_.get() == NULL) {
        bgp_router_.reset(new BgpProtocolConfig(this));
    }
    return bgp_router_.get();
}

void BgpInstanceConfig::BgpConfigReset() {
    bgp_router_.reset();
}

static bool GetInstanceTargetRouteTarget(DBGraph *graph, IFMapNode *node,
        string *target) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "route-target") == 0) {
            *target = adj->name();
            return true;
        }
    }
    return false;
}

static void GetRoutingInstanceExportTargets(DBGraph *graph, IFMapNode *node,
        vector<string> *target_list) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        string target;
        if ((strcmp(adj->table()->Typename(), "instance-target") == 0) &&
            (GetInstanceTargetRouteTarget(graph, adj, &target))) {
            const autogen::InstanceTarget *itarget =
                    dynamic_cast<autogen::InstanceTarget *>(adj->GetObject());
            assert(itarget);
            const autogen::InstanceTargetType &itt = itarget->data();
            if (itt.import_export != "import")
                target_list->push_back(target);
        }
    }
}

void BgpInstanceConfig::Update(BgpConfigManager *manager,
                               const autogen::RoutingInstance *config) {
    import_list_.clear();
    export_list_.clear();

    DBGraph *graph = manager->graph();
    IFMapNode *node = node_proxy_.node();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "instance-target") == 0) {
            string target;
            if (GetInstanceTargetRouteTarget(graph, adj, &target)) {
                const autogen::InstanceTarget *itarget =
                    dynamic_cast<autogen::InstanceTarget *>(adj->GetObject());
                assert(itarget);
                const autogen::InstanceTargetType &itt = itarget->data();
                if (itt.import_export == "import") {
                    import_list_.insert(target);
                } else if (itt.import_export == "export") {
                    export_list_.insert(target);
                } else {
                    import_list_.insert(target);
                    export_list_.insert(target);
                }
            }
        } else if (strcmp(adj->table()->Typename(), "routing-instance") == 0) {
            vector<string> target_list;
            GetRoutingInstanceExportTargets(graph, adj, &target_list);
            BOOST_FOREACH(string target, target_list) {
                import_list_.insert(target);
            }
        } else if (strcmp(adj->table()->Typename(), "virtual-network") == 0) {
            virtual_network_ = adj->name();
        }
    }

    instance_config_.reset(config);
}

void BgpInstanceConfig::ResetConfig() {
    instance_config_.reset();
    node_proxy_.Clear();
}

bool BgpInstanceConfig::DeleteIfEmpty(BgpConfigManager *manager) {
    if (name_ == BgpConfigManager::kMasterInstance) {
        return false;
    }
    if (node() == NULL && bgp_router_.get() == NULL && neighbors_.empty()) {
        manager->Notify(this, BgpConfigManager::CFG_DELETE);
        return true;
    }
    return false;
}

void BgpInstanceConfig::NeighborAdd(BgpConfigManager *manager,
                                    BgpNeighborConfig *neighbor) {
    neighbors_.insert(make_pair(neighbor->name(), neighbor));
    manager->Notify(neighbor, BgpConfigManager::CFG_ADD);
}
void BgpInstanceConfig::NeighborChange(BgpConfigManager *manager,
                                       BgpNeighborConfig *neighbor) {
    manager->Notify(neighbor, BgpConfigManager::CFG_CHANGE);
}
void BgpInstanceConfig::NeighborDelete(BgpConfigManager *manager,
                                       BgpNeighborConfig *neighbor) {
    manager->Notify(neighbor, BgpConfigManager::CFG_DELETE);
    neighbors_.erase(neighbor->name());
}

BgpConfigData::BgpConfigData() {
}

BgpConfigData::~BgpConfigData() {
    STLDeleteElements(&peering_map_);
}

BgpInstanceConfig *BgpConfigData::Locate(const string &name) {
    BgpInstanceConfig *rti = Find(name);
    if (rti == NULL) {
        auto_ptr<BgpInstanceConfig> instance(new BgpInstanceConfig(name));
        rti = instance.get();
        instances_.insert(name, instance);
    }
    return rti;
}

void BgpConfigData::Delete(BgpInstanceConfig *rti) {
    string name(rti->name());
    instances_.erase(name);
}

BgpInstanceConfig *BgpConfigData::Find(const string &name) {
    BgpInstanceMap::iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

const BgpInstanceConfig *BgpConfigData::Find(const string &name) const {
    BgpInstanceMap::const_iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

BgpPeeringConfig *BgpConfigData::CreatePeering(BgpInstanceConfig *rti,
                                               IFMapNodeProxy *proxy) {
    BgpPeeringConfig *peer = new BgpPeeringConfig(rti);
    peer->SetNodeProxy(proxy);
    pair<BgpPeeringMap::iterator, bool> result =
            peering_map_.insert(make_pair(peer->node()->name(), peer));
    assert(result.second);
    return peer;
}

void BgpConfigData::DeletePeering(BgpPeeringConfig *peer) {
    peering_map_.erase(peer->node()->name());
    delete peer;
    return;
}

int BgpConfigManager::config_task_id_ = -1;
const int BgpConfigManager::kConfigTaskInstanceId;

BgpConfigManager::BgpConfigManager()
        : db_(NULL), db_graph_(NULL),
          trigger_(boost::bind(&BgpConfigManager::ConfigHandler, this),
                   TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0),
          listener_(new BgpConfigListener(this)),
          config_(new BgpConfigData()) {
    IdentifierMapInit();

    if (config_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        config_task_id_ = scheduler->GetTaskId("bgp::Config");
    }
}

BgpConfigManager::~BgpConfigManager() {
}

void BgpConfigManager::Initialize(DB *db, DBGraph *db_graph,
                                  const string &localname) {
    db_ = db;
    db_graph_ = db_graph;
    localname_ = localname;
    listener_->Initialize(db);
    DefaultConfig();
}

void BgpConfigManager::OnChange() {
    trigger_.Set();
}

void BgpConfigManager::DefaultBgpRouterParams(autogen::BgpRouterParams &param) {
    param.Clear();
    param.autonomous_system = BgpConfigManager::kDefaultAutonomousSystem;
    param.port = BgpConfigManager::kDefaultPort;
}

void BgpConfigManager::DefaultConfig() {
    BgpInstanceConfig *rti = config_->Locate(kMasterInstance);
    auto_ptr<autogen::BgpRouter> bgp_config(
        new autogen::BgpRouter());
    autogen::BgpRouterParams param;
    DefaultBgpRouterParams(param);
    bgp_config->SetProperty("bgp-router-parameters", &param);
    Notify(rti, BgpConfigManager::CFG_ADD);
    BgpProtocolConfig *localnode = rti->BgpConfigLocate();
    localnode->Update(this, bgp_config.release());
}

void BgpConfigManager::IdentifierMapInit() {
    id_map_.insert(make_pair("routing-instance",
            boost::bind(&BgpConfigManager::ProcessRoutingInstance, this, _1)));
    id_map_.insert(make_pair("bgp-router",
            boost::bind(&BgpConfigManager::ProcessBgpRouter, this, _1)));
    id_map_.insert(make_pair("bgp-peering",
            boost::bind(&BgpConfigManager::ProcessBgpPeering, this, _1)));
}

void BgpConfigManager::ProcessRoutingInstance(const BgpConfigDelta &delta) {
    string instance_name = delta.id_name;
    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    BgpInstanceConfig *rti = config_->Find(instance_name);
    if (rti == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted() ||
            !node->HasAdjacencies(db_graph_)) {
            return;
        }
        event = BgpConfigManager::CFG_ADD;
        rti = config_->Locate(instance_name);
        rti->SetNodeProxy(proxy);
    } else {
        IFMapNode *node = rti->node();
        if (node == NULL) {
            rti->SetNodeProxy(delta.node.get());
        } else if (node->IsDeleted() || !node->HasAdjacencies(db_graph_)) {
            rti->ResetConfig();
            if (rti->DeleteIfEmpty(this)) {
                config_->Delete(rti);
            }
            return;
        }
    }

    autogen::RoutingInstance *rti_config =
        static_cast<autogen::RoutingInstance *>(delta.obj.get());
    rti->Update(this, rti_config);
    Notify(rti, event);
}

void BgpConfigManager::ProcessBgpProtocol(const BgpConfigDelta &delta) {
    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;

    string instance_name(IdentifierParent(delta.id_name));
    BgpInstanceConfig *rti = config_->Find(instance_name);
    BgpProtocolConfig *bgp_config = NULL;
    if (rti != NULL) {
        bgp_config = rti->bgp_config_mutable();
    }

    if (bgp_config == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        // ignore identifier with no properties
        if (delta.obj.get() == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted() ||
            !node->HasAdjacencies(db_graph_)) {
            return;
        }
        event = BgpConfigManager::CFG_ADD;
        if (rti == NULL) {
            rti = config_->Locate(instance_name);
        }
        bgp_config = rti->BgpConfigLocate();
        bgp_config->SetNodeProxy(proxy);
    } else {
        IFMapNode *node = bgp_config->node();
        if (node == NULL) {
            // The master instance creates a BgpRouter node internally. Ignore
            // an update that doesn't specify any content.
            if (delta.obj.get() == NULL) {
                return;
            }
            bgp_config->SetNodeProxy(delta.node.get());
        } else if (node->IsDeleted() || !node->HasAdjacencies(db_graph_)) {
            bgp_config->Delete(this);
            rti->BgpConfigReset();
            if (rti->DeleteIfEmpty(this)) {
                config_->Delete(rti);
            }
            return;
        }
    }

    autogen::BgpRouter *rt_config =
        static_cast<autogen::BgpRouter *>(delta.obj.get());
    bgp_config->Update(this, rt_config);
    Notify(bgp_config, event);
}

void BgpConfigManager::ProcessBgpRouter(const BgpConfigDelta &delta) {
    string instance_name(IdentifierParent(delta.id_name));
    if (instance_name.empty()) {
        // TODO: error
        return;
    }

    string name = delta.id_name.substr(instance_name.size() + 1);
    if (name == localname_) {
        ProcessBgpProtocol(delta);
        return;
    }

    // BgpConfigListener will trigger a notification to any peering sessions
    // associated with this router. That will cause the BgpNeighborConfig
    // data structures to be re-generated.
}

// We are only interested in this node if it is adjacent to the local
// router node.
void BgpConfigManager::ProcessBgpPeering(const BgpConfigDelta &delta) {
    BgpPeeringConfig *peer = NULL;
    BgpConfigData::BgpPeeringMap *peering_map = config_->peerings_mutable();
    BgpConfigData::BgpPeeringMap::iterator loc =
            peering_map->find(delta.id_name);
    if (loc == peering_map->end()) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL) {
            return;
        }

        pair<IFMapNode *, IFMapNode *> routers;
        if (!BgpPeeringConfig::GetRouterPair(db_graph_, localname_, node,
                                             &routers)) {
            return;
        }

        string instance_name(IdentifierParent(routers.first->name()));
        BgpInstanceConfig *rti = config_->Find(instance_name);
        assert(rti != NULL);
        peer = config_->CreatePeering(rti, proxy);
    } else {
        peer = loc->second;
        const IFMapNode *node = peer->node();
        assert(node != NULL);
        if (node->IsDeleted() || !node->HasAdjacencies(db_graph_)) {
            BgpInstanceConfig *rti = peer->instance();
            peer->Delete(this);

            //
            // Delete peering configuration
            //
            config_->DeletePeering(peer);
            if (rti->DeleteIfEmpty(this)) {
                config_->Delete(rti);
            }
            return;
        }
    }
    autogen::BgpPeering *peering_config = static_cast<autogen::BgpPeering *>(
        delta.obj.get());
    peer->Update(this, peering_config);
}

void BgpConfigManager::ProcessChanges(const ChangeList &change_list) {
    for (ChangeList::const_iterator iter = change_list.begin();
         iter != change_list.end(); ++iter) {
        IdentifierMap::iterator loc = id_map_.find(iter->id_type);
        if (loc != id_map_.end()) {
            (loc->second)(*iter);
        }
    }
}

bool BgpConfigManager::ConfigHandler() {
    BgpConfigListener::ChangeList change_list;
    listener_->GetChangeList(&change_list);
    ProcessChanges(change_list);
    return true;
}

void BgpConfigManager::Terminate() {
    listener_->Terminate(db_);
    config_.reset();
}
