/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <string>
#include <sandesh/sandesh_trace.h>
#include <port_ipc/port_ipc_types.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <oper/interface_common.h>
#include <controller/controller_init.h>
#include "port_subscribe_table.h"
#include "port_ipc_handler.h"

using namespace autogen;
using boost::uuids::nil_uuid;

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
                                     uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                                     uint8_t vhostuser_mode, uint8_t link_state) :
    PortSubscribeEntry(type, ifname, version), vmi_uuid_(vmi_uuid),
    vm_uuid_(vm_uuid), vm_name_(vm_name), vn_uuid_(vn_uuid),
    project_uuid_(project_uuid), ip4_addr_(ip4_addr), ip6_addr_(ip6_addr),
    mac_addr_(mac_addr), tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id),
    vhostuser_mode_(vhostuser_mode), link_state_(link_state) {
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
        if (tx_vlan_id_p != VmInterface::kInvalidVlanId ||
            rx_vlan_id_p != VmInterface::kInvalidVlanId) {
            // In case of netns instance transport mode and
            // parent interface shouldnt be set
            port = agent->params()->vmware_physical_port();
            transport = Interface::TRANSPORT_VIRTUAL;
        }
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
                         vhostuser_mode_, transport, link_state_);

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
 const boost::uuids::uuid &vm_uuid, const boost::uuids::uuid &vn_uuid,
 const boost::uuids::uuid &vmi_uuid, const std::string &vm_name,
 const std::string &vm_identifier, const std::string &vm_ifname,
 const std::string &vm_namespace) :
    PortSubscribeEntry(type, ifname, version), vm_uuid_(vm_uuid),
    vn_uuid_(vn_uuid), vm_name_(vm_name), vm_identifier_(vm_identifier),
    vm_ifname_(vm_ifname), vm_namespace_(vm_namespace), vmi_uuid_(vmi_uuid) {
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
        // Could be a port add for an exisiting VMI with a different VM.
        // If so need to handle as a del and add.
        VmiSubscribeEntry *new_entry =
        dynamic_cast<VmiSubscribeEntry *>(entry.get());
        VmiSubscribeEntry *old_entry =
            dynamic_cast<VmiSubscribeEntry *>(ret.first->second.get());
        if (old_entry->vm_uuid() != new_entry->vm_uuid()) {
            ret.first->second->OnDelete(agent_, this);
            vmi_tree_.erase(ret.first);
            ret = vmi_tree_.insert(std::make_pair(u, entry));
        } else {
            ret.first->second->Update(entry.get());
        }
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
                                     const boost::uuids::uuid &vn_uuid,
                                     const boost::uuids::uuid &vmi_uuid,
                                     PortSubscribeEntryPtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    std::pair<VmVnTree::iterator, bool> ret = vmvn_subscribe_tree_.insert
        (make_pair(VmVnUuidEntry(vm_uuid, vn_uuid, vmi_uuid),entry));
    if (ret.second == false) {
        ret.first->second->Update(entry.get());
    }

    // Find VMI for the vm-vn
    //boost::uuids::uuid vmi_uuid = VmVnToVmiNoLock(vm_uuid);
    if (vmi_uuid.is_nil())
        return;

    // If entry is found, it means IFNode for VMI already present
    // Enqueue vm-add request
    VmVnPortSubscribeEntry *vmvn_entry =
        static_cast<VmVnPortSubscribeEntry *>(entry.get());
    vmvn_entry->set_vmi_uuid(vmi_uuid);
    vmvn_entry->OnAdd(agent_, this);
}

void PortSubscribeTable::DeleteVmVnPort
(const boost::uuids::uuid &vm_uuid,
const boost::uuids::uuid &vn_uuid,
const boost::uuids::uuid &vmi_uuid) {
    tbb::mutex::scoped_lock lock(mutex_);
    VmVnTree::iterator it =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, vn_uuid, vmi_uuid));
    if (it == vmvn_subscribe_tree_.end())
        return;

    it->second->OnDelete(agent_, this);
    vmvn_subscribe_tree_.erase(it);
}

PortSubscribeEntryPtr PortSubscribeTable::GetVmVnPortNoLock
(const boost::uuids::uuid &vm_uuid,
 const boost::uuids::uuid &vn_uuid,
 const boost::uuids::uuid &vmi_uuid) {
    VmVnTree::iterator it =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, vn_uuid, vmi_uuid));
    if (it == vmvn_subscribe_tree_.end())
        return PortSubscribeEntryPtr();

    return it->second;
}

PortSubscribeEntryPtr PortSubscribeTable::GetVmVnPort
(const boost::uuids::uuid &vm_uuid,
 const boost::uuids::uuid &vn_uuid,
 const boost::uuids::uuid &vmi_uuid) {
    tbb::mutex::scoped_lock lock(mutex_);
    return GetVmVnPortNoLock(vm_uuid, vn_uuid, vmi_uuid);
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

IFMapNode *PortSubscribeTable::UuidToIFNode(const boost::uuids::uuid &u) const {
    UuidToIFNodeTree::const_iterator it;
    it = uuid_ifnode_tree_.find(u);
    if (it == uuid_ifnode_tree_.end()) {
        return NULL;
    }
    return it->second;
}

const PortSubscribeTable::VmiEntry *PortSubscribeTable::VmiToEntry
(const boost::uuids::uuid &vmi_uuid) const {
    VmiToVmVnTree::const_iterator it = vmi_to_vmvn_tree_.find(vmi_uuid);
    if (it == vmi_to_vmvn_tree_.end())
        return NULL;

    return &it->second;
}

static void CopyVmiConfigToInfo(PortSubscribeTable::VmiEntry *entry,
                                const VmInterfaceConfigData *data) {
    entry->vm_uuid_ = data->vm_uuid_;
    entry->vn_uuid_ = data->vn_uuid_;
    entry->sub_interface_ = (entry->parent_vmi_.is_nil() == false);
    entry->parent_vmi_ = data->parent_vmi_;
    entry->vlan_tag_ = data->rx_vlan_id_;
    entry->vhostuser_mode_ = data->vhostuser_mode_;
    entry->mac_ = data->vm_mac_;
    entry->vmi_cfg = data->GetVmiCfg();
}

/*
 * Update config tables built from VMI IFnode
 * - Adds entry into vmi_to_vmvn_tree_
 * - Adds entry to vmvn_to_vmi_tree_
 */
void PortSubscribeTable::UpdateVmiIfnodeInfo
(const boost::uuids::uuid &vmi_uuid, const VmInterfaceConfigData *data) {
    // Find entry from vmi to vm-vn tree first
    VmiToVmVnTree::iterator it;
    it = vmi_to_vmvn_tree_.find(vmi_uuid);

    if (it != vmi_to_vmvn_tree_.end()) {
        // Nothing to do if entry already present and vm/vn match
        if (it->second.vm_uuid_ != data->vm_uuid_ ||
            it->second.vn_uuid_ != data->vn_uuid_) {
            // If an entry already present and VM/VN are different, remove the
            // reverse entry first and then update with new entry
            vmvn_to_vmi_tree_.erase(VmVnUuidEntry(it->second.vm_uuid_,
                                                  it->second.vn_uuid_,
                                                  vmi_uuid));
            VmVnUuidEntry entry(data->vm_uuid_, data->vn_uuid_, vmi_uuid);
            vmvn_to_vmi_tree_.insert(std::make_pair(entry, vmi_uuid));
        }

        // Update data fields
        CopyVmiConfigToInfo(&it->second, data);
    } else {
        // Entry not present add to vmi_to_vmvn_tree_ and vmvn_to_vmi_tree_
        VmVnUuidEntry entry(data->vm_uuid_, data->vn_uuid_, vmi_uuid);
        vmvn_to_vmi_tree_.insert(std::make_pair(entry, vmi_uuid));
        VmiEntry vmi_entry;
        CopyVmiConfigToInfo(&vmi_entry, data);
        vmi_to_vmvn_tree_.insert(std::make_pair(vmi_uuid, vmi_entry));
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
    UpdateVmiIfnodeInfo(vmi_uuid, data);
    // Add vm-interface if possible
    PortSubscribeEntryPtr entry_ref =
        GetVmVnPortNoLock(data->vm_uuid_, data->vn_uuid_, vmi_uuid);
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
    vmvn_to_vmi_tree_.erase(VmVnUuidEntry(it->second.vm_uuid_,
                                          it->second.vn_uuid_,
                                          vmi_uuid));
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
(const boost::uuids::uuid &vmi_uuid,
 const boost::uuids::uuid &vm_uuid,
 const boost::uuids::uuid &vn_uuid) const {
    tbb::mutex::scoped_lock lock(mutex_);
    VmiTree::const_iterator it = vmi_tree_.find(vmi_uuid);
    if (it != vmi_tree_.end())
        return it->second;

    VmVnTree::const_iterator it1 =
        vmvn_subscribe_tree_.find(VmVnUuidEntry(vm_uuid, vn_uuid, vmi_uuid));
    if (it1 != vmvn_subscribe_tree_.end())
        return it1->second;

    return PortSubscribeEntryPtr();
}

bool PortSubscribeTable::VmVnToVmiSetNoLock
(const boost::uuids::uuid &vm_uuid,
 std::set<boost::uuids::uuid> &vmi_uuid_set) const {
    VmVnToVmiTree::const_iterator it =
        vmvn_to_vmi_tree_.lower_bound(
            VmVnUuidEntry(vm_uuid, nil_uuid(), nil_uuid()));

    while (it != vmvn_to_vmi_tree_.end()) {
        if (it->first.vm_uuid_ == vm_uuid)
            vmi_uuid_set.insert(it->second);
        it++;
    }

    if (vmi_uuid_set.empty())
        return false;

    return true;
}

bool PortSubscribeTable::VmVnToVmiSet
(const boost::uuids::uuid &vm_uuid,
 std::set<boost::uuids::uuid> &vmi_uuid_set) const {
    tbb::mutex::scoped_lock lock(mutex_);
    return VmVnToVmiSetNoLock(vm_uuid, vmi_uuid_set);
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
class SandeshPortSubscribeTask : public Task {
public:
    SandeshPortSubscribeTask(Agent *agent, const std::string &context) :
        Task(agent->task_scheduler()->GetTaskId(kTaskHttpRequstHandler), 0),
        agent_(agent),
        table_(agent->port_ipc_handler()->port_subscribe_table()),
        context_(context) {
    }
    virtual ~SandeshPortSubscribeTask() { }

protected:
    Agent *agent_;
    const PortSubscribeTable *table_;
    std::string context_;
    DISALLOW_COPY_AND_ASSIGN(SandeshPortSubscribeTask);
};

// vmi_tree_ routes
class SandeshVmiPortSubscribeTask : public SandeshPortSubscribeTask {
public:
    SandeshVmiPortSubscribeTask(Agent *agent, const std::string &context,
                                const std::string &ifname,
                                const boost::uuids::uuid &vmi_uuid) :
        SandeshPortSubscribeTask(agent, context),
        ifname_(ifname), vmi_uuid_(vmi_uuid) {
    }
    virtual ~SandeshVmiPortSubscribeTask() { }

    virtual bool Run();
    std::string Description() const { return "SandeshVmiPortSubscribeTask"; }
private:
    std::string ifname_;
    boost::uuids::uuid vmi_uuid_;
    DISALLOW_COPY_AND_ASSIGN(SandeshVmiPortSubscribeTask);
};

bool SandeshVmiPortSubscribeTask::Run() {
    SandeshVmiPortSubscriptionResp *resp = new SandeshVmiPortSubscriptionResp();

    PortSubscribeTable::VmiTree::const_iterator it = table_->vmi_tree_.begin();
    std::vector<SandeshVmiPortSubscriptionInfo> port_list;
    while (it != table_->vmi_tree_.end()) {
        const VmiSubscribeEntry *entry =
            dynamic_cast<const VmiSubscribeEntry *> (it->second.get());
        it++;

        if ((ifname_.empty() == false) &&
            (entry->ifname().find(ifname_) == string::npos) ) {
            continue;
        }

        if (vmi_uuid_.is_nil() == false && entry->vmi_uuid() != vmi_uuid_) {
            continue;
        }

        SandeshVmiPortSubscriptionInfo info;
        info.set_ifname(entry->ifname());
        info.set_version(entry->version());
        info.set_vmi_uuid(UuidToString(entry->vmi_uuid()));
        info.set_vm_uuid(UuidToString(entry->vm_uuid()));
        info.set_vn_uuid(UuidToString(entry->vn_uuid()));
        info.set_vm_name(entry->vm_name());
        info.set_ip4_addr(entry->ip4_addr().to_string());
        info.set_ip6_addr(entry->ip6_addr().to_string());
        info.set_mac(entry->mac_addr());
        info.set_tx_vlan(entry->tx_vlan_id());
        info.set_rx_vlan(entry->rx_vlan_id());
        info.set_vhostuser_mode(entry->vhostuser_mode());

        port_list.push_back(info);
    }
    resp->set_port_list(port_list);

    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void FetchVmiPortSubscriptionReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    boost::uuids::uuid u = StringToUuid(get_vmi_uuid());
    SandeshVmiPortSubscribeTask *t =
        new SandeshVmiPortSubscribeTask(agent, context(), get_ifname(), u);
    agent->task_scheduler()->Enqueue(t);
}

// vmvn_subscribe_tree_ routes
class SandeshVmVnPortSubscribeTask : public SandeshPortSubscribeTask {
public:
    SandeshVmVnPortSubscribeTask(Agent *agent, const std::string &context,
                                const std::string &ifname,
                                const boost::uuids::uuid &vm_uuid) :
        SandeshPortSubscribeTask(agent, context),
        ifname_(ifname), vm_uuid_(vm_uuid) {
    }
    virtual ~SandeshVmVnPortSubscribeTask() { }

    virtual bool Run();
    std::string Description() const { return "SandeshVmVnPortSubscribeTask"; }
private:
    std::string ifname_;
    boost::uuids::uuid vm_uuid_;
    DISALLOW_COPY_AND_ASSIGN(SandeshVmVnPortSubscribeTask);
};

bool SandeshVmVnPortSubscribeTask::Run() {
    SandeshVmVnPortSubscriptionResp *resp =
        new SandeshVmVnPortSubscriptionResp();

    PortSubscribeTable::VmVnTree::const_iterator it =
        table_->vmvn_subscribe_tree_.begin();
    std::vector<SandeshVmVnPortSubscriptionInfo> port_list;
    while (it != table_->vmvn_subscribe_tree_.end()) {
        const VmVnPortSubscribeEntry *entry =
            dynamic_cast<const VmVnPortSubscribeEntry *> (it->second.get());
        it++;

        if ((ifname_.empty() == false) &&
            (entry->ifname().find(ifname_) == string::npos) ) {
            continue;
        }

        if (vm_uuid_.is_nil() == false && entry->vm_uuid() != vm_uuid_) {
            continue;
        }

        SandeshVmVnPortSubscriptionInfo info;
        info.set_ifname(entry->ifname());
        info.set_version(entry->version());
        info.set_vm_uuid(UuidToString(entry->vm_uuid()));
        info.set_vn_uuid(UuidToString(nil_uuid()));
        info.set_vm_name(entry->vm_name());
        info.set_vm_identifier(entry->vm_identifier());
        info.set_vm_ifname(entry->vm_ifname());
        info.set_vm_namespace(entry->vm_namespace());
        info.set_vmi_uuid(UuidToString(entry->vmi_uuid()));
        port_list.push_back(info);
    }
    resp->set_port_list(port_list);

    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void FetchVmVnPortSubscriptionReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    boost::uuids::uuid u = StringToUuid(get_vm_uuid());
    SandeshVmVnPortSubscribeTask *t =
        new SandeshVmVnPortSubscribeTask(agent, context(), get_ifname(), u);
    agent->task_scheduler()->Enqueue(t);
}

// vmi_to_vmvn_tree_ routes
class SandeshVmiToVmVnTask : public SandeshPortSubscribeTask {
public:
    SandeshVmiToVmVnTask(Agent *agent, const std::string &context,
                           const boost::uuids::uuid &vmi_uuid) :
        SandeshPortSubscribeTask(agent, context),
        vmi_uuid_(vmi_uuid) {
    }
    virtual ~SandeshVmiToVmVnTask() { }

    virtual bool Run();
    std::string Description() const { return "SandeshVmiToVmVnTask"; }
private:
    boost::uuids::uuid vmi_uuid_;
    DISALLOW_COPY_AND_ASSIGN(SandeshVmiToVmVnTask);
};

bool SandeshVmiToVmVnTask::Run() {
    SandeshVmiToVmVnResp *resp = new SandeshVmiToVmVnResp();

    PortSubscribeTable::VmiToVmVnTree::const_iterator it =
        table_->vmi_to_vmvn_tree_.begin();
    std::vector<SandeshVmiToVmVnInfo> port_list;
    while (it != table_->vmi_to_vmvn_tree_.end()) {
        boost::uuids::uuid vmi_uuid = it->first;
        const PortSubscribeTable::VmiEntry *vmi_entry = &it->second;
        boost::uuids::uuid vm_uuid = vmi_entry->vm_uuid_;
        boost::uuids::uuid vn_uuid = vmi_entry->vn_uuid_;
        it++;

        if (vmi_uuid_.is_nil() == false && vmi_uuid != vmi_uuid_) {
            continue;
        }

        SandeshVmiToVmVnInfo info;
        info.set_vmi_uuid(UuidToString(vmi_uuid));
        info.set_vm_uuid(UuidToString(vm_uuid));
        info.set_vn_uuid(UuidToString(vn_uuid));
        if (vmi_entry->sub_interface_)
            info.set_sub_interface("True");
        else
            info.set_sub_interface("False");
        info.set_vlan_tag(vmi_entry->vlan_tag_);
        info.set_parent_uuid(UuidToString(vmi_entry->parent_vmi_));
        info.set_mac(vmi_entry->mac_);
        port_list.push_back(info);
    }
    resp->set_port_list(port_list);

    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void FetchVmiToVmVnUuidReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    boost::uuids::uuid u = StringToUuid(get_vmi_uuid());
    agent->task_scheduler()->Enqueue(new SandeshVmiToVmVnTask(agent, context(),
                                                              u));
}

// vmvn_to_vmi_tree_ routes
class SandeshVmVnToVmiTask : public SandeshPortSubscribeTask {
public:
    SandeshVmVnToVmiTask(Agent *agent, const std::string &context,
                           const boost::uuids::uuid &vm_uuid,
                           const boost::uuids::uuid &vn_uuid) :
        SandeshPortSubscribeTask(agent, context),
        vm_uuid_(vm_uuid), vn_uuid_(vn_uuid) {
    }
    virtual ~SandeshVmVnToVmiTask() { }

    virtual bool Run();
    std::string Description() const { return "SandeshVmVnToVmiTask"; }
private:
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vn_uuid_;
    DISALLOW_COPY_AND_ASSIGN(SandeshVmVnToVmiTask);
};

bool SandeshVmVnToVmiTask::Run() {
    SandeshVmVnToVmiResp *resp = new SandeshVmVnToVmiResp();

    PortSubscribeTable::VmVnToVmiTree::const_iterator it =
        table_->vmvn_to_vmi_tree_.begin();
    std::vector<SandeshVmVnToVmiInfo> port_list;
    while (it != table_->vmvn_to_vmi_tree_.end()) {
        boost::uuids::uuid vm_uuid = it->first.vm_uuid_;
        boost::uuids::uuid vn_uuid = it->first.vn_uuid_;
        boost::uuids::uuid vmi_uuid = it->second;
        it++;

        if (vm_uuid_.is_nil() == false && vm_uuid != vm_uuid_) {
            continue;
        }

        if (vn_uuid_.is_nil() == false && vn_uuid != vn_uuid_) {
            continue;
        }

        SandeshVmVnToVmiInfo info;
        info.set_vmi_uuid(UuidToString(vmi_uuid));
        info.set_vm_uuid(UuidToString(vm_uuid));
        info.set_vn_uuid(UuidToString(vn_uuid));
        port_list.push_back(info);
    }
    resp->set_port_list(port_list);

    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void FetchVmVnToVmiUuidReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    boost::uuids::uuid u1 = StringToUuid(get_vm_uuid());
    boost::uuids::uuid u2 = StringToUuid(get_vn_uuid());
    agent->task_scheduler()->Enqueue(new SandeshVmVnToVmiTask(agent, context(),
                                                              u1, u2));
}
