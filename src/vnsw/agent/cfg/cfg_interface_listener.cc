/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <cmn/agent_db.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <cfg/cfg_interface_listener.h>
#include <cfg/cfg_interface.h>

#include <oper/agent_types.h>
#include <oper/interface_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

void InterfaceCfgClient::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    CfgIntEntry *entry = static_cast<CfgIntEntry *>(e);
    Agent *agent = Agent::GetInstance();

    if (entry->IsDeleted()) {
        VmInterface::Delete(agent->interface_table(),
                             entry->GetUuid());
    } else {
        uint16_t vlan_id = VmInterface::kInvalidVlanId;
        string port = Agent::NullString();
        if (agent->params()->isVmwareMode()) {
            vlan_id = entry->vlan_id();
            port = agent->params()->vmware_physical_port();
        }

        VmInterface::Add(agent->interface_table(),
                         entry->GetUuid(), entry->GetIfname(),
                         entry->ip_addr().to_v4(), entry->GetMacAddr(),
                         entry->vm_name(), entry->vm_project_uuid(),
                         vlan_id, port, entry->ip6_addr());
        IFMapNode *node = UuidToIFNode(entry->GetUuid());
        if (node != NULL) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            if (agent_cfg_->agent()->interface_table()->IFNodeToReq(node, req)) {
                agent_cfg_->agent()->interface_table()->Enqueue(&req);
            }
        }
    }
}

void InterfaceCfgClient::RouteTableNotify(DBTablePartBase *partition, 
                                          DBEntryBase *e) {
    IFMapNode *node = static_cast<IFMapNode *>(e);
    if (node->IsDeleted()) {
       return;
    } 

    //Trigger change on all interface entries
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == 
            Agent::GetInstance()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            if (agent_cfg_->agent()->interface_table()->IFNodeToReq(adj_node, req)) {
                agent_cfg_->agent()->interface_table()->Enqueue(&req);
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
    DBTableBase *table = agent_cfg_->agent()->interface_config_table();
    table->Register(boost::bind(&InterfaceCfgClient::Notify, this, _1, _2));

    // Register with config DB table for vm-port UUID to IFNode mapping
    DBTableBase *cfg_db = IFMapTable::FindTable(agent_cfg_->agent()->db(), 
                                                "virtual-machine-interface");
    assert(cfg_db);
    cfg_listener_id_ = cfg_db->Register
        (boost::bind(&InterfaceCfgClient::CfgNotify, this, _1, _2));

    // Register with config DB table for static route table changes
    DBTableBase *cfg_route_db = IFMapTable::FindTable(agent_cfg_->agent()->db(), 
                                                      "interface-route-table");
    assert(cfg_route_db);
    cfg_route_table_listener_id_ = cfg_route_db->Register
        (boost::bind(&InterfaceCfgClient::RouteTableNotify, this, _1, _2));
}

void InterfaceCfgClient::Shutdown() {
    IFMapTable *cfg_db = IFMapTable::FindTable(agent_cfg_->agent()->db(), 
                                                "virtual-machine-interface");
    DBTable::DBStateClear(cfg_db, cfg_listener_id_);
    cfg_db->Unregister(cfg_listener_id_);

    DBTableBase *cfg_route_db = IFMapTable::FindTable(agent_cfg_->agent()->db(), 
                                                      "interface-route-table");
    cfg_route_db->Unregister(cfg_route_table_listener_id_);
}
