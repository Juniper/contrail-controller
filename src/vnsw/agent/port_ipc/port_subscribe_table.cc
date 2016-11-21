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
// VmiSubscribeEntry routines
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
// VmVnPortSubscribeEntry routines
/////////////////////////////////////////////////////////////////////////////
VmVnPortSubscribeEntry::VmVnPortSubscribeEntry
(PortSubscribeEntry::Type type, const std::string &ifname, uint32_t version,
 const boost::uuids::uuid &vm_uuid, const std::string &vm_name,
 const std::string &vm_identifier, const std::string &vm_ifname,
 const std::string &vm_namespace) :
    PortSubscribeEntry(type, ifname, version), vm_uuid_(vm_uuid),
    vn_uuid_(), vm_name_(vm_name), vm_identifier_(vm_identifier),
    vm_ifname_(vm_ifname), vm_namespace_(vm_namespace), vmi_uuid_() {
}

VmVnPortSubscribeEntry::~VmVnPortSubscribeEntry() {
}

void VmVnPortSubscribeEntry::Update(const PortSubscribeEntry *rhs) {
    PortSubscribeEntry::Update(rhs);
}

void VmVnPortSubscribeEntry::OnAdd(Agent *agent, PortSubscribeTable *table)
    const {
    VmInterface::SetIfNameReq(agent->interface_table(), vmi_uuid_, ifname_);

}

void VmVnPortSubscribeEntry::OnDelete(Agent *agent, PortSubscribeTable *table)
    const {
    VmInterface::DeleteIfNameReq(agent->interface_table(), vmi_uuid_);
}

bool VmVnPortSubscribeEntry::MatchVn(const boost::uuids::uuid &u) const {
    if (vn_uuid_ == nil_uuid())
        return true;

    return vn_uuid_ == u;
}

bool VmVnPortSubscribeEntry::MatchVm(const boost::uuids::uuid &u) const {
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

void PortSubscribeTable::AddVmi(const boost::uuids::uuid &u,
                             PortSubscribeEntryPtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    std::pair<VmiTree::iterator, bool> ret =
        vmi_tree_.insert(std::make_pair(u, entry));
    if (ret.second == false) {
        ret.first->second->Update(entry.get());
    }

    ret.first->second->OnAdd(agent_, this);
}

void PortSubscribeTable::DeleteVmi(const boost::uuids::uuid &u) {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::iterator it = vmi_tree_.find(u);
    if (it == vmi_tree_.end())
        return;

    it->second->OnDelete(agent_, this);
    vmi_tree_.erase(it);
}

PortSubscribeEntryPtr PortSubscribeTable::GetVmi(const boost::uuids::uuid &u)
    const {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::const_iterator it = vmi_tree_.find(u);
    if (it == vmi_tree_.end())
        return PortSubscribeEntryPtr();

    return it->second;
}

/*
 * Process add of vm-vn subscribe entry.
 * Add an entry to vmvn_subscribe_tree_
 * If VMI config was already received for port then
 *   - find vmi-uuid from * vmvn_to_vmi_tree_
 *   - ifmap config resync will be done once vmi is added
 */
void PortSubscribeTable::AddVmVnPort(const boost::uuids::uuid &vm_uuid,
                                     PortSubscribeEntryPtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    std::pair<VmVnTree::iterator, bool> ret = vmvn_subscribe_tree_.insert
        (make_pair(VmVnUuidEntry(vm_uuid, nil_uuid()),entry));
    if (ret.second == false) {
        ret.first->second->Update(entry.get());
    }

    // Find VMI for the vm-vn
    boost::uuids::uuid vmi_uuid = VmVnToVmiNoLock(vm_uuid);
    if (vmi_uuid.is_nil())
        return;

    // If entry is found, it means IFNode for VMI already present
    // Enqueue vm-add request
    VmVnPortSubscribeEntry *vmvn_entry =
        static_cast<VmVnPortSubscribeEntry *>(entry.get());
    vmvn_entry->set_vmi_uuid(vmi_uuid);
    vmvn_entry->OnAdd(agent_, this);
}

void PortSubscribeTable::DeleteVmVnPort(const boost::uuids::uuid &vm_uuid) {
    tbb::mutex::scoped_lock lock(mutex_);
    VmVnTree::iterator it =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, nil_uuid()));
    if (it == vmvn_subscribe_tree_.end())
        return;

    it->second->OnDelete(agent_, this);
    vmvn_subscribe_tree_.erase(it);
}

PortSubscribeEntryPtr PortSubscribeTable::GetVmVnPortNoLock
(const boost::uuids::uuid &vm_uuid) {
    VmVnTree::iterator it =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, nil_uuid()));
    if (it == vmvn_subscribe_tree_.end())
        return PortSubscribeEntryPtr();

    return it->second;
}

PortSubscribeEntryPtr PortSubscribeTable::GetVmVnPort
(const boost::uuids::uuid &vm_uuid) {
    tbb::mutex::scoped_lock lock(mutex_);
    return GetVmVnPortNoLock(vm_uuid);
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

/*
 * Update config tables built from VMI IFnode
 * - Adds entry into vmi_to_vmvn_tree_
 * - Adds entry to vmvn_to_vmi_tree_
 */
void PortSubscribeTable::UpdateVmiIfnodeInfo(const boost::uuids::uuid &vmi_uuid,
                                             const boost::uuids::uuid &vm_uuid,
                                             const boost::uuids::uuid &vn_uuid){
    // Find entry from vmi to vm-vn tree first
    VmiToVmVnTree::iterator it;
    it = vmi_to_vmvn_tree_.find(vmi_uuid);

    if (it != vmi_to_vmvn_tree_.end()) {
        // Nothing to do if entry already present and vm/vn match
        if (it->second.vm_uuid_ == vm_uuid &&
            it->second.vn_uuid_ == vn_uuid) {
            return;
        }

        // If an entry already present and VM/VN are different, remove the
        // reverse entry first and then update with new entry
        vmvn_to_vmi_tree_.erase(it->second);
        it->second.vm_uuid_ = vm_uuid;
        it->second.vn_uuid_ = vn_uuid;
        VmVnUuidEntry entry(vm_uuid, vn_uuid);
        vmvn_to_vmi_tree_.insert(std::make_pair(entry, vmi_uuid));
    } else {
        // Entry not present add to vmi_to_vmvn_tree_ and vmvn_to_vmi_tree_
        VmVnUuidEntry entry(vm_uuid, vn_uuid);
        vmi_to_vmvn_tree_.insert(std::make_pair(vmi_uuid, entry));
        vmvn_to_vmi_tree_.insert(std::make_pair(entry, vmi_uuid));
    }

}

/*
 * VMI IFNode config added. Build vmi_to_vmvn_tree_ and vmvn_to_vmi_tree from
 * interface configuration. It will be used for vm-vn based subscriptions
 *
 * If the vm-vn port subscription is already received, the message would have
 * been ignored since vmi-uuid was not known. Process the vm-vn port
 * subscription now
 */
void PortSubscribeTable::HandleVmiIfnodeAdd(const boost::uuids::uuid &vmi_uuid,
                                            const VmInterfaceConfigData *data) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdateVmiIfnodeInfo(vmi_uuid, data->vm_uuid_, data->vn_uuid_);
    // Add vm-interface if possible
    PortSubscribeEntryPtr entry_ref = GetVmVnPortNoLock(data->vm_uuid_);
    if (entry_ref.get() == NULL)
        return;

    VmVnPortSubscribeEntry *entry =
        dynamic_cast<VmVnPortSubscribeEntry *>(entry_ref.get());
    // Nothing to do if vmi-uuid doesnt change
    if (entry->vmi_uuid() == vmi_uuid) {
        return;
    }

    // vmi-uuid for vmvn-port entry changed. Delete old VMI and add new VMI
    if (entry->vmi_uuid().is_nil() == false) {
        entry->OnDelete(agent_, this);
    }
    entry->set_vmi_uuid(vmi_uuid);
    if (entry->vmi_uuid().is_nil() == false) {
        entry->OnAdd(agent_, this);
    }
}

/*
 * Update config tables built from VMI IFnode
 * - Deletes entry from vmi_to_vmvn_tree_
 * - Deletes entry from vmvn_to_vmi_tree_
 */
void PortSubscribeTable::DeleteVmiIfnodeInfo
(const boost::uuids::uuid &vmi_uuid) {
    VmiToVmVnTree::iterator it;
    it = vmi_to_vmvn_tree_.find(vmi_uuid);
    if (it == vmi_to_vmvn_tree_.end()) {
        return;
    }

    // If an entry already present in vm-vn entry, delete entries from
    // vmi_to_vmvn_tree_ and vmvn_to_vmi_tree_
    vmvn_to_vmi_tree_.erase(it->second);
    vmi_to_vmvn_tree_.erase(it);
}

/*
 * VMI IFNode config deleted. Remove entries from vmi_to_vmvn_tree_ and
 * vmvn_to_vmi_tree from interface configuration.
 *
 * VMI deletion happens only on deletion of vm-vn port subscription. So, dont
 * enqueue any VMI delete from here
 */
void PortSubscribeTable::HandleVmiIfnodeDelete
(const boost::uuids::uuid &vmi_uuid) {
    tbb::mutex::scoped_lock lock(mutex_);
    DeleteVmiIfnodeInfo(vmi_uuid);
}

void PortSubscribeTable::StaleWalk(uint64_t version) {
    // TODO : The tree can be modified in parallel. Must ensure synchronization
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (!pih) {
        return;
    }
    VmiTree::iterator it = vmi_tree_.begin();
    while (it != vmi_tree_.end()) {
        VmiSubscribeEntry *entry =
            dynamic_cast<VmiSubscribeEntry *>(it->second.get());
        it++;
        if (entry->type() == PortSubscribeEntry::NAMESPACE)
            continue;

        if (entry->version() >= version) {
            continue;
        }

        std::string msg;
        pih->DeleteVmiUuidEntry(entry->vmi_uuid(), msg);
    }

    // Audit vm-vn port subscription entries
    VmVnTree::iterator vmvn_it = vmvn_subscribe_tree_.begin();
    while (vmvn_it != vmvn_subscribe_tree_.end()) {
        VmVnPortSubscribeEntry *entry =
            dynamic_cast<VmVnPortSubscribeEntry *>(vmvn_it->second.get());
        vmvn_it++;
        if (entry->type() == PortSubscribeEntry::NAMESPACE)
            continue;

        if (entry->version() >= version) {
            continue;
        }

        std::string msg;
        pih->DeleteVmVnPort(entry->vm_uuid(), msg);
    }
}

// Get port-subscribe-entry for a VMI
// First looks at VMI subscription table. If not found, looks into
// vmvn-port-subscribe table
PortSubscribeEntryPtr PortSubscribeTable::Get
(const boost::uuids::uuid &vmi_uuid, const boost::uuids::uuid &vm_uuid) const {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::const_iterator it = vmi_tree_.find(vmi_uuid);
    if (it != vmi_tree_.end())
        return it->second;

    VmVnTree::const_iterator it1 =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, nil_uuid()));
    if (it1 != vmvn_subscribe_tree_.end())
        return it1->second;

    return PortSubscribeEntryPtr();
}

boost::uuids::uuid PortSubscribeTable::VmVnToVmiNoLock
(const boost::uuids::uuid &vm_uuid) const {
    VmVnToVmiTree::const_iterator it =
        vmvn_to_vmi_tree_.lower_bound(VmVnUuidEntry(vm_uuid, nil_uuid()));
    if (it == vmvn_to_vmi_tree_.end())
        return nil_uuid();

    if (it->first.vm_uuid_ != vm_uuid)
        return nil_uuid();

    return it->second;
}

boost::uuids::uuid PortSubscribeTable::VmVnToVmi
(const boost::uuids::uuid &vm_uuid) const {
    tbb::mutex::scoped_lock lock(mutex_);
    return VmVnToVmiNoLock(vm_uuid);
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
