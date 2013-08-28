/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/interface_cfg_listener.h>
#include <cfg/interface_cfg.h>
#include <oper/agent_types.h>
#include <oper/interface.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

InterfaceCfgClient *InterfaceCfgClient::singleton_;

void InterfaceCfgClient::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    CfgIntEntry *entry = static_cast<CfgIntEntry *>(e);

    if (entry->IsDeleted()) {
        VmPortInterface::NovaDel(entry->GetUuid());
    } else {
        VmPortInterface::NovaMsg(entry->GetUuid(), entry->GetIfname(),
                                 entry->GetIpAddr().to_v4(),
                                 entry->GetMacAddr(),
                                 entry->GetVmName());
        IFMapNode *node = UuidToIFNode(entry->GetUuid());
        if (node != NULL) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            if (Agent::GetInterfaceTable()->IFNodeToReq(node, req)) {
                Agent::GetInterfaceTable()->Enqueue(&req);
            }
        }
    }
}

void InterfaceCfgClient::CfgNotify(DBTablePartBase *partition, DBEntryBase *e) {
    IFMapNode *node = static_cast<IFMapNode *>(e);
    CfgState *state = static_cast<CfgState *>(e->GetState(partition->parent(), cfg_listener_id_));

    if (node->IsDeleted()) {
        if (state) {
            VirtualMachineInterface *cfg = 
                static_cast <VirtualMachineInterface *> (node->GetObject());
            assert(cfg);
            autogen::IdPermsType id_perms = cfg->id_perms();
            boost::uuids::uuid u;
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
            uuid_ifnode_tree_.erase(u);
            node->ClearState(partition->parent(), cfg_listener_id_);
            delete state;
        }
    } else {
        VirtualMachineInterface *cfg = 
            static_cast <VirtualMachineInterface *> (node->GetObject());
        if (cfg == NULL) {
            return;
        }
        autogen::IdPermsType id_perms = cfg->id_perms();
        boost::uuids::uuid u;
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
        // We have observed that control node gives a node withoug uuid first
        // followed by subsequent changes that give uuid.
        // Ignore if UUID is not yet present
        if (u == nil_uuid()) {
            return;
        }

        if (state == NULL) {
            uuid_ifnode_tree_.insert(UuidIFNodePair(u, node));
            state = new CfgState();
            state->seen_ = true;
            node->SetState(partition->parent(), cfg_listener_id_, state);
        } 
    }
}

IFMapNode *InterfaceCfgClient::UuidToIFNode(const uuid &u) {
    UuidToIFNodeTree::const_iterator it;

    it = uuid_ifnode_tree_.find(u);
    if (it == uuid_ifnode_tree_.end()) {
        return NULL;
    }

    return it->second;
}

void InterfaceCfgClient::Init() {
    singleton_ = new InterfaceCfgClient();
    DBTableBase *table = Agent::GetIntfCfgTable();
    table->Register(boost::bind(&InterfaceCfgClient::Notify, singleton_, _1, _2));

    // Register with config DB table for vm-port UUID to IFNode mapping
    DBTableBase *cfg_db = IFMapTable::FindTable(Agent::GetDB(), 
                                                "virtual-machine-interface");
    assert(cfg_db);
    singleton_->cfg_listener_id_ = cfg_db->Register
        (boost::bind(&InterfaceCfgClient::CfgNotify, singleton_, _1, _2));
}

void InterfaceCfgClient::Shutdown() {
    DBTableBase *cfg_db = IFMapTable::FindTable(Agent::GetDB(), 
                                                "virtual-machine-interface");
    cfg_db->Unregister(singleton_->cfg_listener_id_);
    delete singleton_;
    singleton_ = NULL;
}
