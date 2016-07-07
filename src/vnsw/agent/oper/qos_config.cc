/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <oper/agent_sandesh.h>
#include <cfg/cfg_init.h>
#include <oper_db.h>
#include <oper/config_manager.h>
#include <oper/interface_common.h>
#include <forwarding_class.h>
#include <qos_config.h>

using namespace autogen;
AgentQosConfig::AgentQosConfig(const boost::uuids::uuid uuid):
    uuid_(uuid), id_(AgentQosConfigTable::kInvalidIndex) {
}

AgentQosConfig::~AgentQosConfig() {
    if (id_ != AgentQosConfigTable::kInvalidIndex) {
        static_cast<AgentQosConfigTable *>(get_table())->ReleaseIndex(this);
    }
}

DBEntryBase::KeyPtr AgentQosConfig::GetDBRequestKey() const {
    AgentQosConfigKey *key = new AgentQosConfigKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

std::string AgentQosConfig::ToString() const {
    std::ostringstream buffer;
    buffer << uuid_;
    return buffer.str();
}

bool AgentQosConfig::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    AgentQosConfigSandeshResp *resp =
        static_cast<AgentQosConfigSandeshResp *>(sresp);
    AgentQosConfigSandeshData data;

    data.set_uuid(UuidToString(uuid_));
    data.set_name(name_);
    data.set_id(id_);
    if (type_ == VHOST) {
        data.set_type("vhost");
    } else if (type_ == FABRIC) {
        data.set_type("fabric");
    } else {
        data.set_type("default");
    }

    std::vector<QosForwardingClassSandeshPair> dscp_list;
    AgentQosConfig::QosIdForwardingClassMap::const_iterator it =
        dscp_map_.begin();
    for(; it != dscp_map_.end(); it++) {
        QosForwardingClassSandeshPair pair;
        pair.set_qos_value(it->first);
        pair.set_forwarding_class_id(it->second);
        dscp_list.push_back(pair);
    }

    std::vector<QosForwardingClassSandeshPair> vlan_priority_list;
    it = vlan_priority_map_.begin();
    for(; it != vlan_priority_map_.end(); it++) {
        QosForwardingClassSandeshPair pair;
        pair.set_qos_value(it->first);
        pair.set_forwarding_class_id(it->second);
        vlan_priority_list.push_back(pair);
    }

    std::vector<QosForwardingClassSandeshPair> mpls_exp_list;
    it = mpls_exp_map_.begin();
    for(; it != mpls_exp_map_.end(); it++) {
        QosForwardingClassSandeshPair pair;
        pair.set_qos_value(it->first);
        pair.set_forwarding_class_id(it->second);
        mpls_exp_list.push_back(pair);
    }

    data.set_dscp_list(dscp_list);
    data.set_vlan_priority_list(vlan_priority_list);
    data.set_mpls_exp_list(mpls_exp_list);
    data.set_default_forwarding_class(default_forwarding_class_);

    std::vector<AgentQosConfigSandeshData> &list =
        const_cast<std::vector<AgentQosConfigSandeshData>&>(resp->get_qc_list());
    list.push_back(data);
    return true;
}

bool AgentQosConfig::IsLess(const DBEntry &rhs) const {
    const AgentQosConfig &fc = static_cast<const AgentQosConfig &>(rhs);
    return (uuid_ < fc.uuid_);
}

bool AgentQosConfig::HandleQosForwardingMapChange(const Agent *agent,
                                                  QosIdForwardingClassMap &map,
                                                  const
                                                  AgentQosIdForwardingClassMap
                                                  &data_map) {

    QosIdForwardingClassMap new_map;
    AgentQosIdForwardingClassMap::const_iterator it = data_map.begin();
    for (; it != data_map.end(); it++) {
        new_map.insert(QosIdForwardingClassPair(it->first, it->second));
    }

    if (map != new_map) {
        map = new_map;
        return true;
    }

    return false;
}

bool AgentQosConfig::VerifyLinkToGlobalQosConfig(const Agent *agent,
                                                 const AgentQosConfigData *data) {
    IFMapNode *node = data->ifmap_node();
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());

    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); iter != node->end(table->GetGraph());
         ++iter) {
        const IFMapNode *adj_node = static_cast<const IFMapNode *>(iter.operator->());
        if (adj_node->table() == agent->cfg()->cfg_global_qos_table()) {
            return true;
        }
    }
    return false;
}

void AgentQosConfig::HandleVhostQosConfig(const Agent *agent,
        const AgentQosConfigData *data, bool deleted) {
    if (deleted == false) {
        //Verify that link to Global QoS config exists
        if (VerifyLinkToGlobalQosConfig(agent, data) == false) {
            deleted = true;
        }
    }

    AgentQosConfigTable *table =
        static_cast<AgentQosConfigTable *>(agent->qos_config_table());

    if (deleted) {
        table->EraseVhostQosConfig(uuid());
    } else {
        table->InsertVhostQosConfig(uuid());
    }

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    InetInterfaceKey *key =
        new InetInterfaceKey(agent->vhost_interface_name());
    key->sub_op_ = AgentKey::RESYNC;
    boost::uuids::uuid qos_config_uuid = table->GetActiveVhostQosConfig();
    InterfaceQosConfigData *qos_data =
        new InterfaceQosConfigData(agent, NULL, qos_config_uuid);
    req.key.reset(key);
    req.data.reset(qos_data);
    agent->interface_table()->Enqueue(&req);
}

void AgentQosConfig::HandleFabricQosConfig(const Agent *agent,
        const AgentQosConfigData *data, bool deleted) {

    if (deleted == false) {
        //Verify that link to Global QoS config exists
        if (VerifyLinkToGlobalQosConfig(agent, data) == false) {
            deleted = true;
        }
    }

    AgentQosConfigTable *table =
        static_cast<AgentQosConfigTable *>(agent->qos_config_table());

    if (deleted) {
        table->EraseFabricQosConfig(uuid());
    } else {
        table->InsertFabricQosConfig(uuid());
    }

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    PhysicalInterfaceKey *key =
        new PhysicalInterfaceKey(agent->fabric_interface_name());
    key->sub_op_ = AgentKey::RESYNC;

    boost::uuids::uuid qos_config_uuid = table->GetActiveFabricQosConfig();
    InterfaceQosConfigData *qos_data =
        new InterfaceQosConfigData(agent, NULL, qos_config_uuid);
    req.key.reset(key);
    req.data.reset(qos_data);
    agent->interface_table()->Enqueue(&req);
}

void AgentQosConfig::HandleGlobalQosConfig(const AgentQosConfigData *data) {
    if (data->ifmap_node() == NULL) {
        return;
    }
    if (type_ == VHOST) {
        HandleVhostQosConfig(data->agent(), data, false);
    } else {
        HandleFabricQosConfig(data->agent(), data, false);
    }
}

bool AgentQosConfig::Change(const DBRequest *req) {
    bool ret = false;
    const AgentQosConfigData *data =
        static_cast<const AgentQosConfigData *>(req->data.get());

    if (name_ != data->name_) {
        name_ = data->name_;
        ret = true;
    }

    if (HandleQosForwardingMapChange(data->agent(), dscp_map_, data->dscp_map_)) {
        ret = true;
    }

    if (HandleQosForwardingMapChange(data->agent(), vlan_priority_map_,
                                     data->vlan_priority_map_)) {
        ret = true;
    }

    if (HandleQosForwardingMapChange(data->agent(), mpls_exp_map_,
                                     data->mpls_exp_map_)) {
        ret = true;
    }

    if (type_ != data->type_) {
        type_ = data->type_;
        ret = true;
    }

    if (type_ != DEFAULT) {
        HandleGlobalQosConfig(data);
    }

    if (default_forwarding_class_ != data->default_forwarding_class_) {
        default_forwarding_class_ = data->default_forwarding_class_;
        ret = true;
    }

    return ret;
}

void AgentQosConfig::Delete(const DBRequest *req) {
    Agent *agent = (static_cast<AgentOperDBTable *>(get_table())->agent());
    if (type_ == VHOST) {
        HandleVhostQosConfig(agent, NULL, true);
    } else if (type_ == FABRIC) {
        HandleFabricQosConfig(agent, NULL, true);
    }
}

void AgentQosConfig::SetKey(const DBRequestKey *key) {
    const AgentQosConfigKey *fc_key =
        static_cast<const AgentQosConfigKey *>(key);
    uuid_ = fc_key->uuid_;
}

AgentQosConfigTable::AgentQosConfigTable(Agent *agent,
                                         DB *db, const std::string &name):
    AgentOperDBTable(db, name) {
    set_agent(agent);
}

AgentQosConfigTable::~AgentQosConfigTable() {
}

DBTableBase*
AgentQosConfigTable::CreateTable(Agent *agent, DB *db, const std::string &name) {
    AgentQosConfigTable *qos_table = new AgentQosConfigTable(agent, db, name);
    (static_cast<DBTable *>(qos_table))->Init();
    return qos_table;
}

std::auto_ptr<DBEntry>
AgentQosConfigTable::AllocEntry(const DBRequestKey *k) const {
    const AgentQosConfigKey *key =
        static_cast<const AgentQosConfigKey *>(k);
    AgentQosConfig *fc = new AgentQosConfig(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(fc));
}

DBEntry* AgentQosConfigTable::OperDBAdd(const DBRequest *req) {
    const AgentQosConfigKey *key =
        static_cast<const AgentQosConfigKey *>(req->key.get());
    AgentQosConfig *qc = new AgentQosConfig(key->uuid_);
    qc->set_id(index_table_.Insert(qc));
    qc->Change(req);
    name_map_.insert(AgentQosConfigNamePair(qc->name(), qc));
    return static_cast<DBEntry *>(qc);
}

bool AgentQosConfigTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    AgentQosConfig *qc = static_cast<AgentQosConfig *>(entry);
    return qc->Change(req);
}

bool AgentQosConfigTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool AgentQosConfigTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    AgentQosConfig *qc = static_cast<AgentQosConfig *>(entry);
    qc->Delete(req);
    name_map_.erase(qc->name());
    return true;
}

void AgentQosConfigTable::ReleaseIndex(AgentQosConfig *qc) {
    if (qc->id() != kInvalidIndex) {
        index_table_.Remove(qc->id());
    }
}

bool AgentQosConfigTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                      const boost::uuids::uuid &u) {
    assert(!u.is_nil());

    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new AgentQosConfigKey(u));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddQosConfigNode(node);
    return false;
}

bool AgentQosConfigTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::QosConfig *cfg = static_cast <autogen::QosConfig *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool AgentQosConfigTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                        const boost::uuids::uuid &u) {
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new AgentQosConfigKey(u));
    req.data.reset(BuildData(node));
    Enqueue(&req);
    return false;
}

AgentQosConfigData*
AgentQosConfigTable::BuildData(IFMapNode *node) {
    AgentQosConfigData *qcd = new AgentQosConfigData(agent(), node);
    autogen::QosConfig *cfg = static_cast <autogen::QosConfig *> (node->GetObject());

    std::vector<QosIdForwardingClassPair>::const_iterator it =
        cfg->dscp_entries().begin();
    for(; it != cfg->dscp_entries().end(); it++) {
        qcd->dscp_map_[it->key] = it->forwarding_class_id;
    }

    for(it = cfg->vlan_priority_entries().begin();
        it != cfg->vlan_priority_entries().end(); it++) {
        qcd->vlan_priority_map_[it->key] = it->forwarding_class_id;
    }

    for(it = cfg->mpls_exp_entries().begin();
        it != cfg->mpls_exp_entries().end(); it++) {
        qcd->mpls_exp_map_[it->key] = it->forwarding_class_id;
    }

    qcd->name_ = node->name();
    if (cfg->type() == "vhost") {
        qcd->type_ = AgentQosConfig::VHOST;
    } else if (cfg->type() == "fabric") {
        qcd->type_ = AgentQosConfig::FABRIC;
    } else {
        qcd->type_ = AgentQosConfig::DEFAULT;
    }

    qcd->default_forwarding_class_ = cfg->default_forwarding_class_id();

    return qcd;
}

AgentSandeshPtr
AgentQosConfigTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                     const std::string &context) {
    return AgentSandeshPtr(new AgentQosConfigSandesh(context,
                args->GetString("uuid"), args->GetString("name"),
                args->GetString("id")));
}

void AgentQosConfigSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentQosConfigSandesh(context(), get_uuid(),
                                                   get_name(), get_id()));
    sand->DoSandesh(sand);
}

void AddQosConfig::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    AgentQosConfigData *data = new AgentQosConfigData(Agent::GetInstance(), NULL);

    data->dscp_map_.insert(AgentQosIdForwardingClassPair(get_dscp(),
                          get_dscp_forwarding_class_id()));
    data->vlan_priority_map_.insert(AgentQosIdForwardingClassPair(get_vlan_priority(),
                             get_vlan_priority_forwarding_class_id()));
    data->mpls_exp_map_.insert(AgentQosIdForwardingClassPair(get_mpls_exp(),
                        get_mpls_exp_forwarding_class_id()));
    data->name_ = get_name();

    if (get_type() == "vhost") {
        data->type_ = AgentQosConfig::VHOST;
    } else if (get_type() == "fabric") {
        data->type_ = AgentQosConfig::FABRIC;
    } else {
        data->type_ = AgentQosConfig::DEFAULT;
    }


    DBTable *table = Agent::GetInstance()->qos_config_table();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new AgentQosConfigKey(u1));
    req.data.reset(data);
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}

void DeleteQosConfig::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    DBTable *table = Agent::GetInstance()->qos_config_table();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new AgentQosConfigKey(u1));
    req.data.reset(NULL);
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}
