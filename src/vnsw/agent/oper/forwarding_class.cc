/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <oper_db.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <oper_db.h>
#include <oper/config_manager.h>
#include <oper/agent_sandesh.h>
#include <qos_queue.h>
#include <forwarding_class.h>
#include <oper/ifmap_dependency_manager.h>
#include <cfg/cfg_init.h>

ForwardingClass::ForwardingClass(const boost::uuids::uuid &uuid):
    uuid_(uuid) {
}

ForwardingClass::~ForwardingClass() {
}

DBEntryBase::KeyPtr ForwardingClass::GetDBRequestKey() const {
    ForwardingClassKey *key = new ForwardingClassKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

std::string ForwardingClass::ToString() const {
    std::ostringstream buffer;
    buffer << uuid_;
    buffer << "id : " << id_;
    return buffer.str();
}

bool ForwardingClass::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ForwardingClassSandeshResp *resp = static_cast<ForwardingClassSandeshResp *>(sresp);
    ForwardingClassSandeshData data;
    data.set_uuid(UuidToString(uuid_));
    data.set_id(id_);
    data.set_dscp(dscp_);
    data.set_vlan_priority(vlan_priority_);
    data.set_mpls_exp(mpls_exp_);
    if (qos_queue_ref_.get()) {
        data.set_qos_queue(qos_queue_ref_->id());
    }
    data.set_name(name_);

    std::vector<ForwardingClassSandeshData> &list =
        const_cast<std::vector<ForwardingClassSandeshData>&>(resp->get_fc_list());
    list.push_back(data);
    return true;
}

bool ForwardingClass::IsLess(const DBEntry &rhs) const {
    const ForwardingClass &fc = static_cast<const ForwardingClass &>(rhs);
    return (uuid_ < fc.uuid_);
}

bool ForwardingClass::Change(const DBRequest *req) {
    bool ret = false;
    const ForwardingClassData *data =
        static_cast<const ForwardingClassData *>(req->data.get());
    const Agent *agent = data->agent();

    if (id_ != data->id_) {
        id_ = data->id_;
        ret = true;
    }

    if (dscp_ != data->dscp_) {
        dscp_ = data->dscp_;
        ret = true;
    }

    if (vlan_priority_ != data->vlan_priority_) {
        vlan_priority_ = data->vlan_priority_;
        ret = true;
    }

    if (mpls_exp_ != data->mpls_exp_) {
        mpls_exp_ = data->mpls_exp_;
        ret = true;
    }

    if (name_ != data->name_) {
        name_ = data->name_;
        ret = true;
    }

    const QosQueueKey key(data->qos_queue_uuid_);
    const QosQueue *queue = static_cast<const QosQueue *>(
            agent->qos_queue_table()->FindActiveEntry(&key));
    if (queue != qos_queue_ref_.get()) {
        qos_queue_ref_ = queue;
        ret = true;
    }

    return ret;
}

void ForwardingClass::Delete(const DBRequest *req) {
}

void ForwardingClass::SetKey(const DBRequestKey *key) {
    const ForwardingClassKey *fc_key =
        static_cast<const ForwardingClassKey *>(key);
    uuid_ = fc_key->uuid_;
}

ForwardingClassTable::ForwardingClassTable(Agent *agent,
                                           DB *db, const std::string &name):
    AgentOperDBTable(db, name) {
    set_agent(agent);
}

ForwardingClassTable::~ForwardingClassTable() {
}

DBTableBase*
ForwardingClassTable::CreateTable(Agent *agent, DB *db, const std::string &name) {
    ForwardingClassTable *fc_table = new ForwardingClassTable(agent, db, name);
    (static_cast<DBTable *>(fc_table))->Init();
    return fc_table;
}

std::auto_ptr<DBEntry>
ForwardingClassTable::AllocEntry(const DBRequestKey *k) const {
    const ForwardingClassKey *key =
        static_cast<const ForwardingClassKey *>(k);
    ForwardingClass *fc = new ForwardingClass(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(fc));
}

DBEntry* ForwardingClassTable::OperDBAdd(const DBRequest *req) {
    const ForwardingClassKey *key =
        static_cast<const ForwardingClassKey *>(req->key.get());
    ForwardingClass *fc = new ForwardingClass(key->uuid_);
    fc->Change(req);
    return static_cast<DBEntry *>(fc);
}

bool ForwardingClassTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    ForwardingClass *fc = static_cast<ForwardingClass *>(entry);
    return fc->Change(req);
}

bool ForwardingClassTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool ForwardingClassTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    return true;
}

bool ForwardingClassTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                      const boost::uuids::uuid &u) {
    assert(!u.is_nil());
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new ForwardingClassKey(u));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddForwardingClassNode(node);
    return false;
}

bool ForwardingClassTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::ForwardingClass *cfg =
        static_cast <autogen::ForwardingClass *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool ForwardingClassTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                        const boost::uuids::uuid &u) {
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new ForwardingClassKey(u));
    req.data.reset(BuildData(node));
    Enqueue(&req);
    return false;
}

ForwardingClassData*
ForwardingClassTable::BuildData(IFMapNode *node) {
    autogen::ForwardingClass *data =
        static_cast<autogen::ForwardingClass *>(node->GetObject());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());

    boost::uuids::uuid qos_queue_uuid = boost::uuids::nil_uuid();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent()->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent()->cfg()->cfg_qos_queue_table()) {
            autogen::QosQueue *qos_queue =
                static_cast<autogen::QosQueue *>(adj_node->GetObject());
            autogen::IdPermsType id_perms = qos_queue->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong,
                    id_perms.uuid.uuid_lslong, qos_queue_uuid);
        }
    }

    ForwardingClassData *fc_data =
        new ForwardingClassData(agent(), node, data->dscp(), data->vlan_priority(),
                                data->mpls_exp(), data->id(), qos_queue_uuid,
                                node->name());
    return fc_data;
}

AgentSandeshPtr
ForwardingClassTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                      const std::string &context) {
    return AgentSandeshPtr(new ForwardingClassSandesh(context,
                args->GetString("uuid"), args->GetString("id"),
                args->GetString("name")));
}

void ForwardingClassSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new ForwardingClassSandesh(context(), get_uuid(),
                                                    get_name(), get_id()));
    sand->DoSandesh(sand);
}

void AddForwardingClass::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    if (get_id() > 255 || get_dscp_value() > 63 || get_mpls_exp() > 7 ||
        get_vlan_priority() > 7) {
        resp->set_resp("Error");
        resp->Response();
        return;
    }

    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    char str1[50];
    sprintf(str1, "00000000-0000-0000-0000-00%010x", get_qos_queue_uuid());
    boost::uuids::uuid u2 = StringToUuid(std::string(str1));

    DBTable *table = Agent::GetInstance()->forwarding_class_table();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new ForwardingClassKey(u1));
    req.data.reset(new ForwardingClassData(Agent::GetInstance(), NULL,
                                           get_dscp_value(), get_vlan_priority(),
                                           get_mpls_exp(), get_id(),
                                           u2, get_name()));
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}

void DeleteForwardingClass::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    DBTable *table = Agent::GetInstance()->forwarding_class_table();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new ForwardingClassKey(u1));
    req.data.reset(NULL);
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}
