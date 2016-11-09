/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <bgp_schema_types.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <oper/interface_common.h>
#include <controller/controller_init.h>
#include "port_subscribe_table.h"
#include "port_ipc_handler.h"

using namespace autogen;

/////////////////////////////////////////////////////////////////////////////
// PortSubscribeEntry routines
/////////////////////////////////////////////////////////////////////////////
PortSubscribeEntry::PortSubscribeEntry(Type type, const std::string &ifname,
                                       int32_t version) :
    type_(type), ifname_(ifname), version_(version) {
}

PortSubscribeEntry::~PortSubscribeEntry() {
}

// Only version number is modifiable
void PortSubscribeEntry::Update(const PortSubscribeEntry *rhs) {
    version_ = rhs->version_;
}

const char *PortSubscribeEntry::TypeToString(Type type) {
    switch (type) {
    case VMPORT:
        return "VM Port";
        break;

    case NAMESPACE:
        return "Namespace Port";
        break;

    case REMOTE_PORT:
        return "Remote Port";
        break;

    default:
        break;
    }

    return "Invalid";
}

/////////////////////////////////////////////////////////////////////////////
// PortSubscribeEntry routines
/////////////////////////////////////////////////////////////////////////////
VmiSubscribeEntry::VmiSubscribeEntry(PortSubscribeEntry::Type type,
                                     const std::string &ifname,
                                     uint32_t version,
                                     const boost::uuids::uuid &vmi_uuid,
                                     const boost::uuids::uuid vm_uuid,
                                     const std::string &vm_name,
                                     const boost::uuids::uuid &vn_uuid,
                                     const boost::uuids::uuid &project_uuid,
                                     const Ip4Address &ip4_addr,
                                     const Ip6Address &ip6_addr,
                                     const std::string &mac_addr,
                                     uint16_t tx_vlan_id, uint16_t rx_vlan_id) :
    PortSubscribeEntry(type, ifname, version), vmi_uuid_(vmi_uuid),
    vm_uuid_(vm_uuid), vm_name_(vm_name), vn_uuid_(vn_uuid),
    project_uuid_(project_uuid), ip4_addr_(ip4_addr), ip6_addr_(ip6_addr),
    mac_addr_(mac_addr), tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id) {
}

VmiSubscribeEntry::~VmiSubscribeEntry() {
}

void VmiSubscribeEntry::Update(const PortSubscribeEntry *rhs) {
    PortSubscribeEntry::Update(rhs);
}

void VmiSubscribeEntry::OnAdd(Agent *agent, PortSubscribeTable *table) const {
    uint16_t tx_vlan_id_p = VmInterface::kInvalidVlanId;
    uint16_t rx_vlan_id_p = VmInterface::kInvalidVlanId;
    string port = Agent::NullString();
    Interface::Transport transport = Interface::TRANSPORT_ETHERNET;
    if (agent->params()->isVmwareMode()) {
        tx_vlan_id_p = tx_vlan_id_;
        rx_vlan_id_p = rx_vlan_id_;
        port = agent->params()->vmware_physical_port();
        transport = Interface::TRANSPORT_VIRTUAL;
    }

    if ((agent->vrouter_on_nic_mode() == true ||
         agent->vrouter_on_host_dpdk() == true) &&
        type_ == PortSubscribeEntry::VMPORT) {
        transport = Interface::TRANSPORT_PMD;
    }

    // Add the interface
    VmInterface::NovaAdd(agent->interface_table(), vmi_uuid_, ifname_,
                         ip4_addr_, mac_addr_, vm_name_, project_uuid_,
                         tx_vlan_id_p, rx_vlan_id_p, port, ip6_addr_,
                         transport);

    // Notify controller module about new port
    if (type_ == PortSubscribeEntry::NAMESPACE)
        return;

    VNController::ControllerWorkQueueDataType
        data(new ControllerVmiSubscribeData(false, vmi_uuid_, vm_uuid_));
    agent->controller()->Enqueue(data);
}

void VmiSubscribeEntry::OnDelete(Agent *agent, PortSubscribeTable *table)
    const {
    VmInterface::Delete(agent->interface_table(), vmi_uuid_,
                        VmInterface::INSTANCE_MSG);
    if (type_ == PortSubscribeEntry::NAMESPACE)
        return;

    VNController::ControllerWorkQueueDataType
        data(new ControllerVmiSubscribeData(true, vmi_uuid_, vm_uuid_));
    agent->controller()->Enqueue(data);
}

bool VmiSubscribeEntry::MatchVn(const boost::uuids::uuid &u) const {
    return vn_uuid_ == u;
}

bool VmiSubscribeEntry::MatchVm(const boost::uuids::uuid &u) const {
    return vm_uuid_ == u;
}

/////////////////////////////////////////////////////////////////////////////
// PortSubscribeTable routines
/////////////////////////////////////////////////////////////////////////////
PortSubscribeTable::PortSubscribeTable(Agent *agent) :
    agent_(agent), interface_table_(agent->interface_table()),
    controller_(agent->controller()),
    vmi_config_table_(NULL), vmi_config_listener_id_(DBTableBase::kInvalidId) {
}

PortSubscribeTable::~PortSubscribeTable() {
    assert(vmi_tree_.size() == 0);
}

void PortSubscribeTable::Add(const boost::uuids::uuid &u,
                             VmiSubscribeEntryPtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    std::pair<VmiTree::iterator, bool> ret =
        vmi_tree_.insert(std::make_pair(u, entry));
    if (ret.second == false) {
        ret.first->second->Update(entry.get());
    }

    ret.first->second->OnAdd(agent_, this);
}

void PortSubscribeTable::Delete(const boost::uuids::uuid &u) {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::iterator it = vmi_tree_.find(u);
    if (it == vmi_tree_.end())
        return;

    it->second->OnDelete(agent_, this);
    vmi_tree_.erase(it);
}

VmiSubscribeEntryPtr PortSubscribeTable::Get(const boost::uuids::uuid &u)
    const {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::const_iterator it = vmi_tree_.find(u);
    if (it == vmi_tree_.end())
        return VmiSubscribeEntryPtr();

    return it->second;
}

/////////////////////////////////////////////////////////////////////////////
// virtual-machine-interface table handler
/////////////////////////////////////////////////////////////////////////////
void PortSubscribeTable::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    IFMapNode *node = static_cast<IFMapNode *>(e);
    State *state = static_cast<State *>(e->GetState(partition->parent(),
                                                    vmi_config_listener_id_));
    if (node->IsDeleted()) {
        if (state == NULL)
            return;
        if (state->uuid_ != nil_uuid())
            uuid_ifnode_tree_.erase(state->uuid_);
        node->ClearState(partition->parent(), vmi_config_listener_id_);
        delete state;
        return;
    } 

    // Allocate DBState
    if (state == NULL) {
        state = new State();
        node->SetState(partition->parent(), vmi_config_listener_id_, state);
    }

    boost::uuids::uuid u;
    VirtualMachineInterface *cfg = 
        static_cast <VirtualMachineInterface *> (node->GetObject());
    if (cfg != NULL) {
        autogen::IdPermsType id_perms = cfg->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    }

    // Update UUID tree
    if (state->uuid_ != u) {
        // Remove old-uuid
        if (state->uuid_ != nil_uuid()) {
            uuid_ifnode_tree_.erase(state->uuid_);
            state->uuid_ = nil_uuid();
        }

        // Set new-uuid
        if (u != nil_uuid()) {
            state->uuid_ = u;
            uuid_ifnode_tree_.insert(std::make_pair(u, node));
        }
    }
}

IFMapNode *PortSubscribeTable::UuidToIFNode(const uuid &u) const {
    UuidToIFNodeTree::const_iterator it;
    it = uuid_ifnode_tree_.find(u);
    if (it == uuid_ifnode_tree_.end()) {
        return NULL;
    }
    return it->second;
}

void PortSubscribeTable::StaleWalk(uint64_t version) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (!pih) {
        return;
    }
    VmiTree::iterator it = vmi_tree_.begin();
    while (it != vmi_tree_.end()) {
        VmiSubscribeEntry *entry = it->second.get();
        it++;
        if (entry->type() == PortSubscribeEntry::NAMESPACE)
            continue;

        if (entry->version() >= version) {
            continue;
        }

        std::string msg;
        pih->DeleteVmiUuidEntry(entry->vmi_uuid(), msg);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Init/Shutdown routines
/////////////////////////////////////////////////////////////////////////////
void PortSubscribeTable::InitDone() {
    // Register with config DB table for vm-port UUID to IFNode mapping
    vmi_config_table_ = (static_cast<IFMapAgentTable *>
                         (IFMapTable::FindTable(agent_->db(),
                                                "virtual-machine-interface")));
    vmi_config_listener_id_ = vmi_config_table_->Register
        (boost::bind(&PortSubscribeTable::Notify, this, _1, _2));
}

void PortSubscribeTable::Shutdown() {
    DBTable::DBStateClear(vmi_config_table_, vmi_config_listener_id_);
    vmi_config_table_->Unregister(vmi_config_listener_id_);
}
