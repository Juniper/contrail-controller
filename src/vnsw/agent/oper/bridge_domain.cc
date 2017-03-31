/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>
#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <oper/ifmap_dependency_manager.h>
#include <bgp_schema_types.h>
#include <oper/config_manager.h>
#include <oper/agent_sandesh.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include "oper/bridge_domain.h"

BridgeDomainEntry::BridgeDomainEntry(const BridgeDomainTable *table,
                                     const boost::uuids::uuid &id) :
    AgentOperDBEntry(), table_(table), uuid_(id), isid_(0), bmac_vrf_name_(""),
    learning_enabled_(false), pbb_etree_enabled_(false), layer2_control_word_(false) {
}

bool BridgeDomainEntry::IsLess(const DBEntry &rhs) const {
    const BridgeDomainEntry &bd =
        static_cast<const BridgeDomainEntry &>(rhs);
    return (uuid_ < bd.uuid_);
}

std::string BridgeDomainEntry::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr BridgeDomainEntry::GetDBRequestKey() const {
    BridgeDomainKey *key = new BridgeDomainKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void BridgeDomainEntry::SetKey(const DBRequestKey *key) {
    const BridgeDomainKey *k =
        static_cast<const BridgeDomainKey *>(key);
    uuid_ = k->uuid_;
}

bool BridgeDomainEntry::DBEntrySandesh(Sandesh *sresp,
                                       std::string &name) const {
    BridgeDomainSandeshResp *resp =
        static_cast<BridgeDomainSandeshResp *>(sresp);

    BridgeDomainSandeshData data;
    data.set_uuid(UuidToString(uuid_));
    data.set_name(name_);
    data.set_isid(isid_);
    if (vn_.get()) {
        data.set_vn(UuidToString(vn_->GetUuid()));
    }

    if (vrf_.get()) {
        data.set_vrf(vrf_->GetName());
    }

    if (learning_enabled_) {
        data.set_learning_enabled("True");
    } else {
        data.set_learning_enabled("False");
    }

    if (pbb_etree_enabled_) {
        data.set_pbb_etree_enabled("True");
    } else {
        data.set_pbb_etree_enabled("False");
    }

    std::vector<BridgeDomainSandeshData> &list =
        const_cast<std::vector<BridgeDomainSandeshData>&>(resp->get_bd_list());
    list.push_back(data);
    return true;
}

void BridgeDomainEntry::UpdateVrf(const BridgeDomainData *data) {
    std::ostringstream str;
    str << data->bmac_vrf_name_ << ":" << UuidToString(uuid_);

    bmac_vrf_name_ = data->bmac_vrf_name_;

    VrfKey key(str.str());
    vrf_ = static_cast<VrfEntry *>(table_->agent()->
                                   vrf_table()->Find(&key, true));
    if (vrf_ && vrf_->IsDeleted()) {
        vrf_ = NULL;
        return;
    }

    table_->agent()->vrf_table()->CreateVrf(str.str(), data->vn_uuid_,
                                            VrfData::PbbVrf, isid_,
                                            data->bmac_vrf_name_,
                                            data->mac_aging_time_,
                                            data->learning_enabled_);

    vrf_ = static_cast<VrfEntry *>(table_->agent()->vrf_table()->
            FindActiveEntry(&key));
    assert(vrf_);

    vrf_->CreateTableLabel(data->learning_enabled_, true,
                           vn_->flood_unknown_unicast(),
                           vn_->layer2_control_word());
    mac_aging_time_ = data->mac_aging_time_;
    layer2_control_word_ = vn_->layer2_control_word();
}

bool BridgeDomainEntry::Change(const BridgeDomainTable *table,
                               const BridgeDomainData *data) {
    bool ret = false;
    bool update_vrf = false;

    name_ = data->name_;
    VnEntry *vn = table_->agent()->vn_table()->Find(data->vn_uuid_);
    if (vn_ != vn) {
        vn_ = vn;
        update_vrf = true;
        ret = true;
    }

    if (isid_ != data->isid_) {
        isid_ = data->isid_;
        update_vrf = true;
        ret = true;
    }

    if (isid_ == 0) {
        OPER_TRACE_ENTRY(BridgeDomain, table,
                         "Ignoring bridge-domain update with ISID 0",
                         UuidToString(uuid_), isid_);
        return ret;
    }

    if (mac_aging_time_ != data->mac_aging_time_) {
        mac_aging_time_ = data->mac_aging_time_;
        update_vrf = true;
        ret = true;
    }

    if (learning_enabled_ != data->learning_enabled_) {
        learning_enabled_ = data->learning_enabled_;
        update_vrf = true;
        ret = true;
    }

    if (vn && layer2_control_word_ != vn->layer2_control_word()) {
        layer2_control_word_ = vn->layer2_control_word();
        update_vrf = true;
        ret = true;
    }

    if (pbb_etree_enabled_ != data->pbb_etree_enabled_) {
        pbb_etree_enabled_ = data->pbb_etree_enabled_;
        ret = true;
    }

    if (bmac_vrf_name_ != data->bmac_vrf_name_) {
        bmac_vrf_name_ = data->bmac_vrf_name_;
        update_vrf = true;
        ret = true;
    }

    if (vrf_ == NULL) {
        update_vrf = true;
    }

    if (vn_ && data->bmac_vrf_name_ != Agent::NullString() && update_vrf) {
        OPER_TRACE_ENTRY(BridgeDomain, table, "Creating C-VRF",
                         UuidToString(uuid_), isid_);
        UpdateVrf(data);
    }

    return ret;
}

void BridgeDomainEntry::Delete() {
    BridgeDomainTable *table = static_cast<BridgeDomainTable *>(get_table());
    OPER_TRACE_ENTRY(BridgeDomain, table, "Deleting bridge-domain",
                     UuidToString(uuid_), isid_);
    if (vrf_.get()) {
        table_->agent()->vrf_table()->DeleteVrf(vrf_->GetName(),
                                                VrfData::PbbVrf);
        vrf_.reset();
    }
}

BridgeDomainTable::BridgeDomainTable(Agent *agent, DB *db,
                                     const std::string &name) :
    AgentOperDBTable(db, name) {
    set_agent(agent);
}

BridgeDomainTable::~BridgeDomainTable() {
}

DBTableBase *BridgeDomainTable::CreateTable(Agent *agent, DB *db,
                                            const std::string &name) {
    BridgeDomainTable *table = new BridgeDomainTable(agent, db, name);
    table->Init();
    return table;
}

std::auto_ptr<DBEntry>
BridgeDomainTable::AllocEntry(const DBRequestKey *k) const {
    const BridgeDomainKey *key = static_cast<const BridgeDomainKey *>(k);
    BridgeDomainEntry *bd = new BridgeDomainEntry(this, key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(bd));
}

DBEntry *BridgeDomainTable::OperDBAdd(const DBRequest *req) {
    BridgeDomainKey *key = static_cast<BridgeDomainKey *>(req->key.get());
    BridgeDomainData *data = static_cast<BridgeDomainData *>(req->data.get());
    BridgeDomainEntry *bd = new BridgeDomainEntry(this, key->uuid_);
    bd->Change(this, data);
    return bd;
}

bool BridgeDomainTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    BridgeDomainEntry *bd = static_cast<BridgeDomainEntry *>(entry);
    BridgeDomainData *data = dynamic_cast<BridgeDomainData *>(req->data.get());
    bool ret = bd->Change(this, data);
    return ret;
}

bool BridgeDomainTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool BridgeDomainTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    BridgeDomainEntry *bd = static_cast<BridgeDomainEntry *>(entry);
    bd->Delete();
    return true;
}

static BridgeDomainKey *BuildKey(const boost::uuids::uuid &u) {
    return new BridgeDomainKey(u);
}

static void BuildVrfData (Agent *agent, IFMapNode *vn_node,
                          BridgeDomainData *data) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(vn_node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = vn_node->begin(graph);
            iter != vn_node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent->cfg()->cfg_vrf_table()) {
            autogen::RoutingInstance *vrf =
                static_cast<autogen::RoutingInstance *>(adj_node->GetObject());
            if(vrf->is_default()) {
                data->bmac_vrf_name_ = adj_node->name();
            }
        }
    }
}

static BridgeDomainData *BuildData(Agent *agent, IFMapNode *node,
                                   const autogen::BridgeDomain *bd) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();
    uuid vn_uuid = nil_uuid();
    BridgeDomainData *bdd = new BridgeDomainData(agent, node);

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
            iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent->cfg()->cfg_vn_table()) {
            autogen::VirtualNetwork *vn =
                static_cast<autogen::VirtualNetwork *>(adj_node->GetObject());
            autogen::IdPermsType id_perms = vn->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       vn_uuid);
            bdd->pbb_etree_enabled_ = vn->pbb_etree_enable();
            BuildVrfData(agent, adj_node, bdd);
        }
    }

    bdd->name_ = node->name();
    bdd->isid_ = bd->isid();
    bdd->vn_uuid_ = vn_uuid;
    bdd->learning_enabled_ = bd->mac_learning_enabled();
    bdd->mac_aging_time_ = bd->mac_aging_time();
    return bdd;
}

bool BridgeDomainTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                    const boost::uuids::uuid &u) {
    autogen::BridgeDomain *bd =
        static_cast<autogen::BridgeDomain *>(node->GetObject());
    assert(bd);

    assert(!u.is_nil());

    req.key.reset(BuildKey(u));
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddBridgeDomainNode(node);
    return false;
}

bool BridgeDomainTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                     const boost::uuids::uuid &u) {
    autogen::BridgeDomain *bd =
        static_cast <autogen::BridgeDomain *>(node->GetObject());
    assert(bd);

    req.key.reset(BuildKey(u));
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, bd));
    return true;
}

bool BridgeDomainTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::BridgeDomain *bd =
        static_cast<autogen::BridgeDomain *>(node->GetObject());
    autogen::IdPermsType id_perms = bd->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

BridgeDomainEntry *BridgeDomainTable::Find(const boost::uuids::uuid &u) {
    BridgeDomainKey key(u);
    return static_cast<BridgeDomainEntry *>(FindActiveEntry(&key));
}

AgentSandeshPtr
BridgeDomainTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                   const std::string &context) {
    return AgentSandeshPtr(new BridgeDomainSandesh(context,
                                                   args->GetString("uuid"),
                                                   args->GetString("name")));
}

void BridgeDomainSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new BridgeDomainSandesh(context(), get_uuid(),
                                                 get_name()));
    sand->DoSandesh(sand);
}
