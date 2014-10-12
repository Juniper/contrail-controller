/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "loadbalancer.h"

#include "ifmap/ifmap_node.h"
#include "oper/ifmap_dependency_manager.h"
#include "loadbalancer_properties.h"
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>

class LoadbalancerData : public AgentData {
  public:
    typedef LoadbalancerProperties Properties;

    LoadbalancerData(IFMapNode *node)
        : node_(node) {
    }

    LoadbalancerData(const Properties &properties)
         : node_(NULL), properties_(properties) {
    }

    IFMapNode *node() { return node_; }

    const Properties &properties() { return properties_; }

  private:
    IFMapNode *node_;
    Properties properties_;
};

Loadbalancer::Loadbalancer() {
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

void Loadbalancer::set_properties(const Properties &properties) {
    properties_.reset(new Properties(properties));
}

const Loadbalancer::Properties *Loadbalancer::properties() const {
    return properties_.get();
}

static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static void PropertiesAddMember(
    IFMapNode *node, LoadbalancerProperties *properties) {
    autogen::LoadbalancerMember *member =
            static_cast<autogen::LoadbalancerMember *>(node->GetObject());
    properties->members()->insert(
        std::make_pair(IdPermsGetUuid(member->id_perms()),
                       member->properties()));
}

static void PropertiesAddHealthmonitor(
    IFMapNode *node, LoadbalancerProperties *properties) {
    autogen::LoadbalancerHealthmonitor *healthmon =
            static_cast<autogen::LoadbalancerHealthmonitor *>(
                node->GetObject());
    properties->healthmonitors()->insert(
        std::make_pair(IdPermsGetUuid(healthmon->id_perms()),
                       healthmon->properties()));
}

void Loadbalancer::CalculateProperties(DBGraph *graph, Properties *properties) {
    autogen::LoadbalancerPool *pool =
            static_cast<autogen::LoadbalancerPool *>(node_->GetObject());
    properties->set_pool_properties(pool->properties());

    for (DBGraphVertex::adjacency_iterator iter = node_->begin(graph);
         iter != node_->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        const char *adjtype = adj->table()->Typename();
        if (strcmp(adjtype, "virtual-ip") == 0) {
            autogen::VirtualIp *vip =
                    static_cast<autogen::VirtualIp *>(adj->GetObject());
            properties->set_vip_uuid(IdPermsGetUuid(vip->id_perms()));
            properties->set_vip_properties(vip->properties());
        } else if (strcmp(adjtype, "loadbalancer-member") == 0) {
            PropertiesAddMember(adj, properties);
        } else if (strcmp(adjtype, "loadbalancer-healthmonitor") == 0) {
            PropertiesAddHealthmonitor(adj, properties);
        }
    }
}

bool Loadbalancer::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    LoadBalancerResp *resp = static_cast<LoadBalancerResp*> (sresp);

    std::string str_uuid = UuidToString(uuid_);
    if (! name.empty() && str_uuid != name) {
        return false;
    }

    LoadBalancerSandeshData data;
    data.set_uuid(str_uuid);
    const autogen::VirtualIpType &vip =
        properties_.get()->vip_properties();
    data.set_vip_address(vip.address);
    data.set_port(vip.protocol_port);

    const autogen::LoadbalancerPoolType &pool = properties()->pool_properties();
    data.set_mode(pool.protocol);
    data.set_balance(pool.loadbalancer_method);

    if (properties()->healthmonitors().size()) {
         const autogen::LoadbalancerHealthmonitorType &hm =
                 properties()->healthmonitors().begin()->second;
        data.set_inter(hm.timeout * 1000);
        data.set_fall(hm.max_retries);
        data.set_rise(1);
        if (!hm.expected_codes.empty()) {
            data.set_expected_codes(hm.expected_codes);
            if (!hm.http_method.empty()) {
                data.set_http_method(hm.http_method);
            }
            if (!hm.url_path.empty()) {
                data.set_url_path(hm.url_path);
            }
        }
    }

    std::vector<std::string> members;
    for (LoadbalancerProperties::MemberMap::const_iterator iter =
                 properties()->members().begin();
         iter != properties()->members().end(); ++iter) {
        autogen::LoadbalancerMemberType member = iter->second;
        members.push_back(member.address);
    }
    data.set_member_list(members);

    std::vector<LoadBalancerSandeshData> &list =
            const_cast<std::vector<LoadBalancerSandeshData>&>
            (resp->get_load_balancer_list());
    list.push_back(data);

    return true;
}

void LoadBalancerReq::HandleRequest() const {
    AgentLoadBalancerSandesh *sand = new AgentLoadBalancerSandesh(context(), get_uuid());
    sand->DoSandesh();
}


/*
 * LoadbalancerTable class
 */
LoadbalancerTable::LoadbalancerTable(DB *db, const std::string &name)
        : AgentDBTable(db, name),
          graph_(NULL), dependency_manager_(NULL) {
}

std::auto_ptr<DBEntry> LoadbalancerTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new Loadbalancer());
    entry->SetKey(key);
    return entry;
}

DBEntry *LoadbalancerTable::Add(const DBRequest *request) {
    Loadbalancer *loadbalancer = new Loadbalancer();
    loadbalancer->SetKey(request->key.get());
    LoadbalancerData *data =
            static_cast<LoadbalancerData *>(request->data.get());
    loadbalancer->set_node(data->node());
    assert(dependency_manager_);
    dependency_manager_->SetObject(data->node(), loadbalancer);

    return loadbalancer;
}

void LoadbalancerTable::Delete(DBEntry *entry, const DBRequest *request) {
    Loadbalancer *loadbalancer  = static_cast<Loadbalancer *>(entry);
    assert(dependency_manager_);
    dependency_manager_->ResetObject(loadbalancer->node());
}

bool LoadbalancerTable::OnChange(DBEntry *entry, const DBRequest *request) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);

    LoadbalancerData *data = static_cast<LoadbalancerData *>(
        request->data.get());
    if (data->node() == NULL) {
        loadbalancer->set_properties(data->properties());
        return true;
    }
    return false;
}

void LoadbalancerTable::Initialize(
    DBGraph *graph, IFMapDependencyManager *dependency_manager) {

    graph_ = graph;
    dependency_manager_ = dependency_manager;

    dependency_manager_->Register(
        "loadbalancer-pool",
        boost::bind(&LoadbalancerTable::ChangeEventHandler, this, _1));
}

bool LoadbalancerTable::IFNodeToReq(IFMapNode *node, DBRequest &request) {
    autogen::LoadbalancerPool *pool =
            static_cast<autogen::LoadbalancerPool *>(node->GetObject());
    const autogen::IdPermsType &id = pool->id_perms();
    request.key.reset(new LoadbalancerKey(IdPermsGetUuid(id)));
    if (!node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.data.reset(new LoadbalancerData(node));
    } else {
        request.oper = DBRequest::DB_ENTRY_DELETE;
    }
    return true;
}

void LoadbalancerTable::ChangeEventHandler(DBEntry *entry) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);
    /*
     * Do not enqueue an ADD_CHANGE operation after the DELETE generated
     * by IFNodeToReq.
     */
    if (loadbalancer->node()->IsDeleted()) {
        return;
    }

    assert(graph_);
    LoadbalancerProperties properties;
    loadbalancer->CalculateProperties(graph_, &properties);

    const LoadbalancerProperties *current = loadbalancer->properties();
    if (current == NULL || properties.CompareTo(*current) != 0) {
        LOG(DEBUG, "loadbalancer property change "
            << properties.DiffString(current));
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key = loadbalancer->GetDBRequestKey();
        request.data.reset(new LoadbalancerData(properties));
        Enqueue(&request);
    }
}

DBTableBase *LoadbalancerTable::CreateTable(
    DB *db, const std::string &name) {
    LoadbalancerTable *table = new LoadbalancerTable(db, name);
    table->Init();
    return table;
}
