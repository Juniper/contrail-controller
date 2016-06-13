/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <agent_types.h>
#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper_db.h>
#include <oper/config_manager.h>
#include <qos_queue.h>

QosQueue::QosQueue(const boost::uuids::uuid &uuid):
    uuid_(uuid), id_(QosQueueTable::kInvalidIndex) {
}

QosQueue::~QosQueue() {
    if (id_ != QosQueueTable::kInvalidIndex) {
        static_cast<QosQueueTable *>(get_table())->ReleaseIndex(this);
    }
}

DBEntryBase::KeyPtr QosQueue::GetDBRequestKey() const {
    QosQueueKey *key = new QosQueueKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

std::string QosQueue::ToString() const {
    std::ostringstream buffer;
    buffer << uuid_;
    return buffer.str();
}

bool QosQueue::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    QosQueueSandeshResp *resp = static_cast<QosQueueSandeshResp *>(sresp);
    QosQueueSandeshData data;
    data.set_uuid(UuidToString(uuid_));
    data.set_id(id_);
    data.set_name(name_);

    std::vector<QosQueueSandeshData> &list =
        const_cast<std::vector<QosQueueSandeshData>&>(resp->get_qos_queue_list());
    list.push_back(data);
    return true;
}

bool QosQueue::IsLess(const DBEntry &rhs) const {
    const QosQueue &qos_q = static_cast<const QosQueue &>(rhs);
    return (uuid_ < qos_q.uuid_);
}

bool QosQueue::Change(const DBRequest *req) {
    const QosQueueData *data = static_cast<const QosQueueData *>(req->data.get());

    if (name_ != data->name_) {
        name_ = data->name_;
    }
    return false;
}

void QosQueue::Delete(const DBRequest *req) {
}

void QosQueue::SetKey(const DBRequestKey *key) {
    const QosQueueKey *qos_q_key =
        static_cast<const QosQueueKey *>(key);
    uuid_ = qos_q_key->uuid_;
}

QosQueueTable::QosQueueTable(Agent *agent,
                             DB *db, const std::string &name):
    AgentOperDBTable(db, name) {
    set_agent(agent);
}

QosQueueTable::~QosQueueTable() {
}

DBTableBase*
QosQueueTable::CreateTable(Agent *agent, DB *db, const std::string &name) {
    QosQueueTable *qos_q_table = new QosQueueTable(agent, db, name);
    (static_cast<DBTable *>(qos_q_table))->Init();
    return qos_q_table;
}

std::auto_ptr<DBEntry>
QosQueueTable::AllocEntry(const DBRequestKey *k) const {
    const QosQueueKey *key =
        static_cast<const QosQueueKey *>(k);
    QosQueue *qos_q = new QosQueue(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(qos_q));
}

DBEntry* QosQueueTable::OperDBAdd(const DBRequest *req) {
    const QosQueueKey *key =
        static_cast<const QosQueueKey *>(req->key.get());
    QosQueue *qos_q = new QosQueue(key->uuid_);
    qos_q->set_id(index_table_.Insert(qos_q));
    qos_q->Change(req);
    return static_cast<DBEntry *>(qos_q);
}

bool QosQueueTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    QosQueue *qos_q = static_cast<QosQueue *>(entry);
    return qos_q->Change(req);
}

bool QosQueueTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool QosQueueTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    return true;
}

bool QosQueueTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                      const boost::uuids::uuid &u) {
    assert(!u.is_nil());
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new QosQueueKey(u));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddQosQueueNode(node);
    return false;
}

bool QosQueueTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::QosQueue *cfg = static_cast <autogen::QosQueue *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}


bool QosQueueTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                  const boost::uuids::uuid &u) {
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new QosQueueKey(u));
    req.data.reset(new QosQueueData(agent(), node, node->name()));
    Enqueue(&req);
    return false;
}

void QosQueueTable::ReleaseIndex(QosQueue *qos_q) {
    if (qos_q->id() != kInvalidIndex) {
        index_table_.Remove(qos_q->id());
    }
}

AgentSandeshPtr
QosQueueTable::GetAgentSandesh(const AgentSandeshArguments *args,
                               const std::string &context) {
    return AgentSandeshPtr(new QosQueueSandesh(context,
                args->GetString("uuid"), args->GetString("name"),
                args->GetString("id")));
}

void QosQueueSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new QosQueueSandesh(context(), get_uuid(),
                                             get_name(), get_id()));
    sand->DoSandesh(sand);
}

void AddQosQueue::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());
    resp->set_resp("Success");

    DBTable *table = Agent::GetInstance()->qos_queue_table();
    DBRequest req;
    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new QosQueueKey(u1));
    req.data.reset(new QosQueueData(NULL, NULL, get_name()));
    table->Enqueue(&req);
    resp->Response();
}

void DeleteQosQueue::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());
    resp->set_resp("Success");

    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", get_uuid());
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    DBTable *table = Agent::GetInstance()->qos_queue_table();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new QosQueueKey(u1));
    req.data.reset(NULL);
    table->Enqueue(&req);
    resp->Response();
}
