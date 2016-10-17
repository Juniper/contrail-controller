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
#include <cfg/cfg_interface_listener.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>

#include <oper/agent_types.h>
#include <oper/interface_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/config_manager.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::NotifyUuidAdd(Agent *agent, IFMapNode *node,
                                       const boost::uuids::uuid &u) const {
    if (node == NULL)
        return;

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    if (agent->interface_table()->IFNodeToReq(node, req,
                                              const_cast<uuid&>(u))) {
        agent->interface_table()->Enqueue(&req);
    }
}

void InterfaceCfgClient::NotifyUuidDel(Agent *agent,
                                       const boost::uuids::uuid &u) const {
    if (u == nil_uuid())
        return;

    VmInterface::Delete(agent->interface_table(), u, VmInterface::INSTANCE_MSG);
}

IFMapNode *InterfaceCfgClient::UuidToIFNode(const uuid &u) const {
    UuidToIFNodeTree::const_iterator it;

    it = uuid_ifnode_tree_.find(u);
    if (it == uuid_ifnode_tree_.end()) {
        return NULL;
    }

    return it->second;
}

bool InterfaceCfgClient::VmiToLabels(const VmInterface *vmi, string *vm_label,
                                     string *nw_label) const {
    *vm_label = "";
    *nw_label = "";
    const VmEntry *vm = vmi->vm();
    const VnEntry *vn = vmi->vn();

    if (vn == NULL || vm == NULL)
        return false;

    vector<string> name_list;
    boost::split(name_list, vm->GetCfgName(), boost::is_any_of(":"));
    if (name_list.size())
        *vm_label = name_list[name_list.size() - 1];

    boost::split(name_list, vn->GetName(), boost::is_any_of(":"));
    if (name_list.size())
        *nw_label = name_list[name_list.size() - 1];

    return true;
}

void InterfaceCfgClient::TryAddVmiFromLabel(const LabelKey &key) const {
    VmiLabelTreeConstIterator it = vmi_label_tree_.find(key);
    if (it == vmi_label_tree_.end())
        return;

    if (it->second == NULL)
        return;
    Agent *agent = agent_cfg_->agent();
    InterfaceConfigTable *table = agent->interface_config_table();

    InterfaceConfigLabelKey cfg_key(key.vm_label_, key.network_label_);
    InterfaceConfigLabelEntry *label =
        dynamic_cast<InterfaceConfigLabelEntry *>
        (table->FindActiveEntry(&cfg_key));
    if (label == NULL)
        return;

    VmInterface::SetIfNameReq(agent->interface_table(),
                              it->second->GetUuid(), label->tap_name());
}

void InterfaceCfgClient::DeleteVmiFromLabel(const LabelKey &key) const {
    VmiLabelTreeConstIterator it = vmi_label_tree_.find(key);
    if (it == vmi_label_tree_.end())
        return;

    if (it->second == NULL)
        return;
    Agent *agent = agent_cfg_->agent();
    InterfaceConfigTable *table = agent->interface_config_table();

    InterfaceConfigLabelKey cfg_key(key.vm_label_, key.network_label_);
    InterfaceConfigLabelEntry *label =
        dynamic_cast<InterfaceConfigLabelEntry *>
        (table->Find(&cfg_key, true));
    if (label == NULL)
        return;

    VmInterface::DeleteIfNameReq(agent->interface_table(),
                              it->second->GetUuid());
}

const VmInterface *InterfaceCfgClient::LabelToVmi
(const std::string &vm_label, const std::string &vn_label) const {
    LabelKey key(vm_label, vn_label);
    VmiLabelTreeConstIterator it = vmi_label_tree_.find(key);
    if (it == vmi_label_tree_.end())
        return NULL;

    return it->second;
}

/////////////////////////////////////////////////////////////////////////////
// Oper Interface notification handler
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::OperVmiNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    VmInterface *vmi = dynamic_cast<VmInterface *>(e);
    if (vmi == NULL)
        return;

    OperVmiState *state = static_cast<OperVmiState *>
        (e->GetState(partition->parent(), oper_interface_listener_id_));
    if (e->IsDeleted()) {
        if (state == NULL)
            return;

        LabelKey key1(state->vm_label_, "");
        VmiLabelTreeIterator it = vmi_label_tree_.find(key1);
        if (it != vmi_label_tree_.end() && it->second == vmi) {
            DeleteVmiFromLabel(key1);
            vmi_label_tree_.erase(it);
        }

        LabelKey key2(state->vm_label_, state->network_label_);
        it = vmi_label_tree_.find(key2);
        if (it != vmi_label_tree_.end() && it->second == vmi) {
            DeleteVmiFromLabel(key2);
            vmi_label_tree_.erase(it);
        }

        e->ClearState(partition->parent(), oper_interface_listener_id_);
        delete state;
        return;
    }

    string vm_label;
    string nw_label;
    VmiToLabels(vmi, &vm_label, &nw_label);
    // TODO : handle label-change

    if (state == NULL) {
        if (vm_label == "" || nw_label == "") {
            return;
        }

        state = new OperVmiState(vm_label, nw_label);
        e->SetState(partition->parent(), oper_interface_listener_id_, state);

        LabelKey key1(state->vm_label_, "");
        VmiLabelTreeConstIterator it = vmi_label_tree_.find(key1);
        if (it == vmi_label_tree_.end()) {
            vmi_label_tree_.insert(std::make_pair(key1, vmi));
        }
        TryAddVmiFromLabel(key1);

        LabelKey key2(state->vm_label_, state->network_label_);
        it = vmi_label_tree_.find(key2);
        if (it == vmi_label_tree_.end()) {
            vmi_label_tree_.insert(std::make_pair(key2, vmi));
        }
        TryAddVmiFromLabel(key2);
    }
}

/////////////////////////////////////////////////////////////////////////////
// DB Notification handler for Interface-Config table
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::InterfaceConfigNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    InterfaceConfigVmiEntry *vmi = dynamic_cast<InterfaceConfigVmiEntry *>(e);
    InterfaceConfigLabelEntry *label =
        dynamic_cast<InterfaceConfigLabelEntry *>(e);
    InterfaceConfigTable *table =
        static_cast<InterfaceConfigTable *>(partition->parent());
    Agent *agent = table->agent();
    InterfaceConfigState *state =static_cast<InterfaceConfigState *>
        (e->GetState(partition->parent(), intf_cfg_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            if (vmi) {
                NotifyUuidDel(agent, state->vmi_uuid_);
            }

            if (label) {
                DeleteVmiFromLabel(LabelKey(state->vm_label_,
                                            state->nw_label_));
            }

            e->ClearState(partition->parent(), intf_cfg_listener_id_);
            delete state;
        }
        return;

    }

    if (vmi) {
        if (state == NULL) {
            state = new InterfaceConfigState(vmi->vmi_uuid());
            e->SetState(partition->parent(), intf_cfg_listener_id_,
                        state);
        } else {
            state->vmi_uuid_ = vmi->vmi_uuid();
        }

        uint16_t tx_vlan_id = VmInterface::kInvalidVlanId;
        uint16_t rx_vlan_id = VmInterface::kInvalidVlanId;
        string port = Agent::NullString();
        Interface::Transport transport = Interface::TRANSPORT_ETHERNET;
        if (agent->params()->isVmwareMode()) {
            tx_vlan_id = vmi->tx_vlan_id();
            rx_vlan_id = vmi->rx_vlan_id();
            port = agent->params()->vmware_physical_port();
            transport = Interface::TRANSPORT_VIRTUAL;
        }

        if ((agent->vrouter_on_nic_mode() == true ||
             agent->vrouter_on_host_dpdk() == true) &&
            vmi->vmi_type() == InterfaceConfigVmiEntry::VM_INTERFACE) {
            transport = Interface::TRANSPORT_PMD;
        }

        // Call NovaAdd to add the interface
        VmInterface::NovaAdd(agent->interface_table(), vmi->vmi_uuid(),
                             vmi->tap_name(), vmi->ip4_addr(), vmi->mac_addr(),
                             vmi->vm_name(), vmi->project_uuid(), tx_vlan_id,
                             rx_vlan_id, port, vmi->ip6_addr(), transport);

        // Call IFNodeToReq for the VMI if corresponding IFMap Node was seen
        IFMapNode *node = UuidToIFNode(state->vmi_uuid_);
        if (node) {
            NotifyUuidAdd(agent, node, state->vmi_uuid_);
        }
        return;
    }

    if (label) {
        if (state == NULL) {
            state = new InterfaceConfigState(label->vm_label(),
                                             label->network_label());
            e->SetState(partition->parent(), intf_cfg_listener_id_,
                        state);
        } else {
            state->vm_label_ = label->vm_label();
            state->nw_label_ = label->network_label();
        }
        TryAddVmiFromLabel(LabelKey(state->vm_label_, state->nw_label_));
    }

}

/////////////////////////////////////////////////////////////////////////////
// DB Notification handler for IFMap interface-route-table
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::IfMapInterfaceRouteNotify(DBTablePartBase *partition,
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
        if (Agent::GetInstance()->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == 
            Agent::GetInstance()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            boost::uuids::uuid id;
            agent_cfg_->agent()->interface_table()->IFNodeToUuid(adj_node, id);
            if (agent_cfg_->agent()->interface_table()->IFNodeToReq(adj_node,
                                                                     req, id)) {
                agent_cfg_->agent()->interface_table()->Enqueue(&req);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// DB Notification handler for IFMap virtual-machine-interface table
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::IfMapVmiNotify(DBTablePartBase *partition,
                                        DBEntryBase *e) {
    IFMapNode *node = static_cast<IFMapNode *>(e);
    VirtualMachineInterface *cfg = 
        dynamic_cast<VirtualMachineInterface *> (node->GetObject());
    IfMapVmiState *state =
        static_cast<IfMapVmiState *>(e->GetState(partition->parent(),
                                                 ifmap_vmi_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            uuid_ifnode_tree_.erase(state->uuid_);
            e->ClearState(partition->parent(), ifmap_vmi_listener_id_);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        autogen::IdPermsType id_perms = cfg->id_perms();
        boost::uuids::uuid u;
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
        // We have observed that control node gives a node withoug uuid first
        // followed by subsequent changes that give uuid.
        // Ignore if UUID is not yet present
        if (u == nil_uuid()) {
            return;
        }

        uuid_ifnode_tree_.insert(UuidIFNodePair(u, node));
        e->SetState(partition->parent(), ifmap_vmi_listener_id_,
                    new IfMapVmiState(u));
    }
}

void InterfaceCfgClient::Init() {
    DBTableBase *table = agent_cfg_->agent()->interface_config_table();
    intf_cfg_listener_id_ =
        table->Register(boost::bind(&InterfaceCfgClient::InterfaceConfigNotify,
                                    this, _1, _2));

    // Register with config DB table for vm-port UUID to IFNode mapping
    table = IFMapTable::FindTable(agent_cfg_->agent()->db(),
                                  "virtual-machine-interface");
    ifmap_vmi_listener_id_ = table->Register
        (boost::bind(&InterfaceCfgClient::IfMapVmiNotify, this, _1, _2));

    // Register with config DB table for static route table changes
    table = IFMapTable::FindTable(agent_cfg_->agent()->db(),
                                  "interface-route-table");
    ifmap_intf_route_listener_id_ = table->Register
        (boost::bind(&InterfaceCfgClient::IfMapInterfaceRouteNotify, this, _1,
                     _2));

    table = agent_cfg_->agent()->interface_table();
    oper_interface_listener_id_ = table->Register
        (boost::bind(&InterfaceCfgClient::OperVmiNotify, this, _1, _2));
}

void InterfaceCfgClient::Shutdown() {
    Agent *agent = agent_cfg_->agent();

    DBTable *table;
    table = IFMapTable::FindTable(agent->db(), "virtual-machine-interface");
    DBTable::DBStateClear(table, ifmap_vmi_listener_id_);
    table->Unregister(ifmap_vmi_listener_id_);

    table = IFMapTable::FindTable(agent->db(), "interface-route-table");
    DBTable::DBStateClear(table, ifmap_vmi_listener_id_);
    table->Unregister(ifmap_intf_route_listener_id_);

    agent->interface_config_table()->Unregister(intf_cfg_listener_id_);
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void InterfaceCfgClient::FillIntrospectData(SandeshVmiLabelTableResp *resp) {
    std::vector<SandeshVmiLabelEntry> list;
    VmiLabelTreeConstIterator it = vmi_label_tree_.begin();
    while (it != vmi_label_tree_.end()) {
        SandeshVmiLabelEntry entry;
        entry.set_vm_label(it->first.vm_label_);
        entry.set_vn_label(it->first.network_label_);
        if (it->second) {
            entry.set_vmi_uuid(UuidToString(it->second->GetUuid()));
            entry.set_tap_name(it->second->name());
        }
        list.push_back(entry);
        it++;
    }
    resp->set_entries(list);
}

class SandeshVmiLabelTask : public Task {
public:
    SandeshVmiLabelTask(Agent *agent, const std::string &context) :
        Task((agent->task_scheduler()->GetTaskId("http::RequestHandlerTask")),
             0), agent_(agent), context_(context) {
    }
    virtual ~SandeshVmiLabelTask() { }

    virtual bool Run();
    std::string Description() const { return "SandeshVmiLabelTask"; }

protected:
    Agent *agent_;
    std::string context_;

private:
    DISALLOW_COPY_AND_ASSIGN(SandeshVmiLabelTask);
};

bool SandeshVmiLabelTask::Run() {
    SandeshVmiLabelTableResp *resp = new SandeshVmiLabelTableResp();
    agent_->cfg()->cfg_interface_client()->FillIntrospectData(resp);
    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void SandeshVmiLabelTableReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(new SandeshVmiLabelTask(agent, context()));
    return;
}
