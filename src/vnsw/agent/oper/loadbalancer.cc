/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include "lbpool_info.h"
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

static bool IsListenerEqual(const autogen::LoadbalancerListenerType &lhs,
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
                                Loadbalancer::ListenerMap *list,
                                Loadbalancer::PoolSet *pool_list) {
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
            const autogen::IdPermsType &id = listener->id_perms();
            CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
            list->insert(std::make_pair(uuid, listener->properties()));
            FetchPoolList(graph, adj, pool_list);
        }
    }
}

Loadbalancer::Loadbalancer() : lb_info_(), listeners_(), pools_() {
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

    std::vector<std::string> listener_list;
    ListenerMap::const_iterator it = listeners_.begin();
    while (it != listeners_.end()) {
        listener_list.push_back(UuidToString(it->first));
        ++it;
    }
    data.set_listener_list(listener_list);

    std::vector<std::string> pool_list;
    PoolSet::const_iterator pit = pools_.begin();
    while (pit != pools_.end()) {
        pool_list.push_back(UuidToString(*pit));
        ++pit;
    }
    data.set_pool_list(pool_list);

    std::vector<LoadBalancerV2SandeshData> &list =
            const_cast<std::vector<LoadBalancerV2SandeshData>&>
            (resp->get_load_balancer_list());
    list.push_back(data);

    return true;
}

void Loadbalancer::PostAdd() {
    IFMapNode *node = ifmap_node();
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    CalculateProperties(table->GetGraph(), node, &lb_info_, &listeners_,
                        &pools_);
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
    Loadbalancer *loadbalancer = new Loadbalancer();
    loadbalancer->SetKey(request->key.get());
    return loadbalancer;
}

bool LoadbalancerTable::OperDBDelete(DBEntry *entry, const DBRequest *request) {
    return true;
}

bool LoadbalancerTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);
    Loadbalancer::PoolSet pool_list;

    LoadbalancerData *data = static_cast<LoadbalancerData *>(req->data.get());

    autogen::LoadbalancerType lb_info;
    Loadbalancer::ListenerMap listener_list;
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(data->ifmap_node()
                                                            ->table());
    CalculateProperties(table->GetGraph(), data->ifmap_node(), &lb_info,
                        &listener_list, &pool_list);
    if (!loadbalancer->IsLBInfoEqual(lb_info)) {
        loadbalancer->set_lb_info(lb_info);
    }
    if (!loadbalancer->IsListenerMapEqual(listener_list)) {
        loadbalancer->set_listeners(listener_list);
    }
    if (loadbalancer->pools() != pool_list) {
        loadbalancer->set_pools(pool_list);
    }
    loadbalancer->SetKey(req->key.get());
    return true;
}

bool LoadbalancerTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::LoadbalancerPool *pool =
        static_cast<autogen::LoadbalancerPool *>(node->GetObject());
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

DBTableBase *LoadbalancerTable::CreateTable(DB *db, const std::string &name) {
    LoadbalancerTable *table = new LoadbalancerTable(db, name);
    table->Init();
    return table;
}
