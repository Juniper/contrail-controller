/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include "loadbalancer_pool_info.h"
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>
#include <oper/oper_db.h>
#include "loadbalancer.h"

class LoadbalancerData : public AgentOperDBData {
  public:
    LoadbalancerData(Agent *agent, IFMapNode *node)
        : AgentOperDBData(agent, node) {
    }
};

static bool IsListenerPropsEqual(const autogen::LoadbalancerListenerType &lhs,
                                 const autogen::LoadbalancerListenerType &rhs) {
    if (lhs.protocol != rhs.protocol) {
        return false;
    }
    if (lhs.protocol_port != rhs.protocol_port) {
        return false;
    }
    if (lhs.admin_state != rhs.admin_state) {
        return false;
    }
    return true;
}

static bool IsListenerEqual(const Loadbalancer::ListenerInfo &lhs,
                            const Loadbalancer::ListenerInfo &rhs) {
    if (!IsListenerPropsEqual(lhs.properties, rhs.properties)) {
        return false;
    }
    if (lhs.pools != rhs.pools) {
        return false;
    }
    return true;
}

static void FetchPoolList(DBGraph *graph, IFMapNode *node,
                          Loadbalancer::PoolSet *pool_list) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        const char *adjtype = adj->table()->Typename();
        if (strcmp(adjtype, "loadbalancer-pool") == 0) {
            autogen::LoadbalancerPool *pool =
                static_cast<autogen::LoadbalancerPool *>(adj->GetObject());
            boost::uuids::uuid uuid;
            const autogen::IdPermsType &id = pool->id_perms();
            CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
            pool_list->insert(uuid);
        }
    }
}

static void CalculateProperties(DBGraph *graph, IFMapNode *node,
                                autogen::LoadbalancerType *lb_info,
                                Loadbalancer::ListenerMap *list) {
    autogen::Loadbalancer *cfg_lb =
            static_cast<autogen::Loadbalancer *>(node->GetObject());
    if (cfg_lb->IsPropertySet(autogen::Loadbalancer::PROPERTIES)) {
        *lb_info = cfg_lb->properties();
    }

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        const char *adjtype = adj->table()->Typename();
        if (strcmp(adjtype, "loadbalancer-listener") == 0) {
            autogen::LoadbalancerListener *listener =
                static_cast<autogen::LoadbalancerListener *>(adj->GetObject());
            boost::uuids::uuid uuid;
            Loadbalancer::PoolSet pool_list;
            const autogen::IdPermsType &id = listener->id_perms();
            CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
            FetchPoolList(graph, adj, &pool_list);
            Loadbalancer::ListenerInfo info(listener->properties(), pool_list);
            list->insert(std::make_pair(uuid, info));
        }
    }
}

Loadbalancer::Loadbalancer() : lb_info_(), listeners_() {
}

Loadbalancer::~Loadbalancer() {
}

bool Loadbalancer::IsLess(const DBEntry &rhs) const {
    const Loadbalancer &lb = static_cast<const Loadbalancer &>(rhs);
    return uuid_ < lb.uuid_;
}

std::string Loadbalancer::ToString() const {
    return UuidToString(uuid_);
}

void Loadbalancer::SetKey(const DBRequestKey *key) {
    const LoadbalancerKey *lbkey = static_cast<const LoadbalancerKey *>(key);
    uuid_ = lbkey->instance_id();
}

DBEntryBase::KeyPtr Loadbalancer::GetDBRequestKey() const {
    LoadbalancerKey *key = new LoadbalancerKey(uuid_);
    return KeyPtr(key);
}

bool Loadbalancer::IsLBInfoEqual(const autogen::LoadbalancerType &rhs) {
    if (lb_info_.status != rhs.status) {
        return false;
    }
    if (lb_info_.provisioning_status != rhs.provisioning_status) {
        return false;
    }
    if (lb_info_.operating_status != rhs.operating_status) {
        return false;
    }
    if (lb_info_.vip_subnet_id != rhs.vip_subnet_id) {
        return false;
    }
    if (lb_info_.vip_address != rhs.vip_address) {
        return false;
    }
    if (lb_info_.admin_state != rhs.admin_state) {
        return false;
    }
    return true;
}

bool Loadbalancer::IsListenerMapEqual(const ListenerMap &rhs) {
    if (listeners_.size() != rhs.size()) {
        return false;
    }
    ListenerMap::const_iterator it1, it2;
    it1 = listeners_.begin();
    it2 = rhs.begin();
    while (it1 != listeners_.end() && it2 != rhs.end()) {
        if (it1->first != it2->first) {
            return false;
        }
        if (!IsListenerEqual(it1->second, it2->second)) {
            return false;
        }
        ++it1;
        ++it2;
    }

    return true;
}

bool Loadbalancer::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    LoadBalancerV2Resp *resp = static_cast<LoadBalancerV2Resp*> (sresp);

    std::string str_uuid = UuidToString(uuid_);
    if (!name.empty() && str_uuid != name) {
        return false;
    }

    LoadBalancerV2SandeshData data;
    data.set_uuid(str_uuid);
    data.set_status(lb_info_.status);
    data.set_provisioning_status(lb_info_.provisioning_status);
    data.set_operating_status(lb_info_.operating_status);
    data.set_vip_subnet(lb_info_.vip_subnet_id);
    data.set_vip_address(lb_info_.vip_address);
    data.set_admin_state(lb_info_.admin_state);

    std::vector<SandeshLoadBalancerListener> listener_list;
    ListenerMap::const_iterator it = listeners_.begin();
    while (it != listeners_.end()) {
        const Loadbalancer::ListenerInfo &info = it->second;
        SandeshLoadBalancerListener entry;
        entry.set_uuid(UuidToString(it->first));
        entry.set_protocol(info.properties.protocol);
        entry.set_port(info.properties.protocol_port);
        entry.set_admin_state(info.properties.admin_state);

        std::vector<std::string> pool_list;
        PoolSet::const_iterator pit = info.pools.begin();
        while (pit != info.pools.end()) {
            pool_list.push_back(UuidToString(*pit));
            ++pit;
        }
        entry.set_pool_list(pool_list);
        listener_list.push_back(entry);
        ++it;
    }
    data.set_listener_list(listener_list);

    std::vector<LoadBalancerV2SandeshData> &list =
            const_cast<std::vector<LoadBalancerV2SandeshData>&>
            (resp->get_load_balancer_list());
    list.push_back(data);

    return true;
}

void LoadBalancerV2Req::HandleRequest() const {
    AgentSandeshPtr sand(new AgentLoadBalancerV2Sandesh(context(), get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr LoadbalancerTable::GetAgentSandesh
    (const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr
        (new AgentLoadBalancerV2Sandesh(context, args->GetString("name")));
}

/*
 * LoadbalancerTable class
 */
LoadbalancerTable::LoadbalancerTable(DB *db, const std::string &name)
        : AgentOperDBTable(db, name) {
}

LoadbalancerTable::~LoadbalancerTable() {
}

std::auto_ptr<DBEntry> LoadbalancerTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new Loadbalancer());
    entry->SetKey(key);
    return entry;
}

DBEntry *LoadbalancerTable::OperDBAdd(const DBRequest *request) {
    autogen::LoadbalancerType lb_info;
    Loadbalancer::ListenerMap listeners;

    Loadbalancer *loadbalancer = new Loadbalancer();
    LoadbalancerData *data =
            static_cast<LoadbalancerData *>(request->data.get());
    CalculateProperties(graph_, data->ifmap_node(), &lb_info, &listeners);
    loadbalancer->set_lb_info(lb_info);
    loadbalancer->set_listeners(listeners);
    loadbalancer->SetKey(request->key.get());
    return loadbalancer;
}

bool LoadbalancerTable::OperDBDelete(DBEntry *entry, const DBRequest *request) {
    return true;
}

bool LoadbalancerTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);

    LoadbalancerData *data = static_cast<LoadbalancerData *>(req->data.get());

    /* Ignore change notifications if the entry is marked for delete */
    if (data->ifmap_node()->IsDeleted()) {
        return false;
    }
    autogen::LoadbalancerType lb_info;
    Loadbalancer::ListenerMap listener_list;
    CalculateProperties(graph_, data->ifmap_node(), &lb_info, &listener_list);
    if (!loadbalancer->IsLBInfoEqual(lb_info)) {
        loadbalancer->set_lb_info(lb_info);
    }
    if (!loadbalancer->IsListenerMapEqual(listener_list)) {
        loadbalancer->set_listeners(listener_list);
    }
    loadbalancer->SetKey(req->key.get());
    return true;
}

bool LoadbalancerTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::Loadbalancer *pool =
        static_cast<autogen::Loadbalancer *>(node->GetObject());
    const autogen::IdPermsType &id = pool->id_perms();
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, u);
    return true;
}

bool LoadbalancerTable::IFNodeToReq(IFMapNode *node, DBRequest &request,
        const boost::uuids::uuid &id) {


    assert(!id.is_nil());
    request.key.reset(new LoadbalancerKey(id));

    if (request.oper == DBRequest::DB_ENTRY_DELETE || node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    request.data.reset(new LoadbalancerData(agent(), node));
    return true;
}

void LoadbalancerTable::Initialize(DBGraph *graph) {
    graph_ = graph;
}

DBTableBase *LoadbalancerTable::CreateTable(DB *db, const std::string &name) {
    LoadbalancerTable *table = new LoadbalancerTable(db, name);
    table->Init();
    return table;
}
