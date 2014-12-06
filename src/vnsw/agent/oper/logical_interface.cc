/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <oper/agent_sandesh.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <oper/logical_interface.h>
#include <oper/vm_interface.h>

#include <vector>
#include <string>

using std::string;
using std::auto_ptr;
using boost::uuids::uuid;

/////////////////////////////////////////////////////////////////////////////
// LogicalInterface routines
/////////////////////////////////////////////////////////////////////////////
LogicalInterface::LogicalInterface(const boost::uuids::uuid &uuid,
                                   const std::string &name) :
    Interface(Interface::LOGICAL, uuid, name, NULL), display_name_(),
    physical_interface_(), vm_interface_() {
}

LogicalInterface::~LogicalInterface() {
}

string LogicalInterface::ToString() const {
    return UuidToString(uuid_);
}

bool LogicalInterface::CmpInterface(const DBEntry &rhs) const {
    const LogicalInterface &a = static_cast<const LogicalInterface &>(rhs);
    return (uuid_ < a.uuid_);
}

bool LogicalInterface::OnChange(const InterfaceTable *table,
                                const LogicalInterfaceData *data) {
    bool ret = false;

    if (display_name_ != data->display_name_) {
        display_name_ = data->display_name_;
        ret = true;
    }

    PhysicalInterfaceKey phy_key(data->physical_interface_);
    Interface *intf = static_cast<PhysicalInterface *>
        (table->agent()->interface_table()->FindActiveEntry(&phy_key));
    if (intf == NULL) {
        RemotePhysicalInterfaceKey rem_key(data->physical_interface_);
        intf = static_cast<RemotePhysicalInterface *>
            (table->agent()->interface_table()->FindActiveEntry(&rem_key));
    }

    if (intf != physical_interface_.get()) {
        physical_interface_.reset(intf);
        ret = true;
    }

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, data->vm_interface_, "");
    Interface *interface = static_cast<Interface *>
        (table->agent()->interface_table()->FindActiveEntry(&vmi_key));
    if (interface != vm_interface_.get()) {
        vm_interface_.reset(interface);
        ret = true;
    }

    if (Copy(table, data) == true) {
        ret = true;
    }

    return ret;
}

bool LogicalInterface::Delete(const DBRequest *req) {
    return true;
}

VmInterface *LogicalInterface::vm_interface() const {
    return static_cast<VmInterface *>(vm_interface_.get());
}

Interface *LogicalInterface::physical_interface() const {
    return static_cast<Interface *>(physical_interface_.get());
}

//////////////////////////////////////////////////////////////////////////////
// LogicalInterfaceKey routines
//////////////////////////////////////////////////////////////////////////////

LogicalInterfaceKey::LogicalInterfaceKey(const boost::uuids::uuid &uuid,
                                         const std::string &name) :
    InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::LOGICAL, uuid, name,
                 false) {
}

LogicalInterfaceKey::~LogicalInterfaceKey() {
}

//////////////////////////////////////////////////////////////////////////////
// LogicalInterfaceData routines
//////////////////////////////////////////////////////////////////////////////
LogicalInterfaceData::LogicalInterfaceData(IFMapNode *node,
                                           const std::string &display_name,
                                           const std::string &port,
                                           const boost::uuids::uuid &vif) :
    InterfaceData(node), display_name_(display_name), physical_interface_(port),
    vm_interface_(vif) {
}

LogicalInterfaceData::~LogicalInterfaceData() {
}

//////////////////////////////////////////////////////////////////////////////
// VlanLogicalInterface routines
//////////////////////////////////////////////////////////////////////////////
VlanLogicalInterface::VlanLogicalInterface(const boost::uuids::uuid &uuid,
                                           const std::string &name) :
    LogicalInterface(uuid, name) {
}

VlanLogicalInterface::~VlanLogicalInterface() {
}

DBEntryBase::KeyPtr VlanLogicalInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VlanLogicalInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

bool VlanLogicalInterface::Copy(const InterfaceTable *table,
                                const LogicalInterfaceData *d) {
    bool ret = false;
    const VlanLogicalInterfaceData *data =
        static_cast<const VlanLogicalInterfaceData *>(d);

    if (vlan_ != data->vlan_) {
        vlan_ = data->vlan_;
        ret = true;
    }

    return ret;
}

//////////////////////////////////////////////////////////////////////////////
// VlanLogicalInterfaceKey routines
//////////////////////////////////////////////////////////////////////////////
VlanLogicalInterfaceKey::VlanLogicalInterfaceKey(const boost::uuids::uuid &uuid,
                                                 const std::string &name) :
    LogicalInterfaceKey(uuid, name) {
}

VlanLogicalInterfaceKey::~VlanLogicalInterfaceKey() {
}

LogicalInterface *
VlanLogicalInterfaceKey::AllocEntry(const InterfaceTable *table)
    const {
    return new VlanLogicalInterface(uuid_, name_);
}

LogicalInterface *
VlanLogicalInterfaceKey::AllocEntry(const InterfaceTable *table,
                                    const InterfaceData *d) const {
    VlanLogicalInterface *intf = new VlanLogicalInterface(uuid_, name_);
    const VlanLogicalInterfaceData *data =
        static_cast<const VlanLogicalInterfaceData *>(d);
    intf->OnChange(table, data);
    return intf;
}

InterfaceKey *VlanLogicalInterfaceKey::Clone() const {
    return new VlanLogicalInterfaceKey(uuid_, name_);
}

VlanLogicalInterfaceData::VlanLogicalInterfaceData
(IFMapNode *node, const std::string &display_name,
 const std::string &physical_interface,
 const boost::uuids::uuid &vif, uint16_t vlan) :
    LogicalInterfaceData(node, display_name, physical_interface, vif), vlan_(vlan) {
}

VlanLogicalInterfaceData::~VlanLogicalInterfaceData() {
}

#if 0
void VlanLogicalInterface::SetSandeshData(SandeshLogicalInterface *data) const {
    data->set_vlan_tag(vlan_);
}
#endif
/////////////////////////////////////////////////////////////////////////////
// Config handling routines
/////////////////////////////////////////////////////////////////////////////
void LogicalInterface::ConfigEventHandler(IFMapNode *node) {
}

static LogicalInterfaceKey *BuildKey(IFMapNode *node,
                                     const autogen::LogicalInterface *port) {
    autogen::IdPermsType id_perms = port->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return new VlanLogicalInterfaceKey(u, node->name());
}

static LogicalInterfaceData *BuildData(const Agent *agent, IFMapNode *node,
                                  const autogen::LogicalInterface *port) {
    // Find link with physical-interface adjacency
    string physical_interface;
    IFMapNode *adj_node = NULL;
    adj_node = agent->cfg_listener()->FindAdjacentIFMapNode
        (agent, node, "physical-interface");
    if (adj_node) {
        physical_interface = adj_node->name();
    }

    // Find link with virtual-machine-interface adjacency
    boost::uuids::uuid vmi_uuid = nil_uuid();
    adj_node = agent->cfg_listener()->FindAdjacentIFMapNode
        (agent, node, "virtual-machine-interface");
    if (adj_node) {
        autogen::VirtualMachineInterface *vmi =
            static_cast<autogen::VirtualMachineInterface *>
            (adj_node->GetObject());
        autogen::IdPermsType id_perms = vmi->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   vmi_uuid);
    }

    return new VlanLogicalInterfaceData(node, port->display_name(), physical_interface,
                                        vmi_uuid, port->vlan_tag());
}

bool InterfaceTable::LogicalInterfaceIFNodeToReq(IFMapNode *node,
                                                 DBRequest &req) {
    autogen::LogicalInterface *port =
        static_cast <autogen::LogicalInterface *>(node->GetObject());
    assert(port);

    req.key.reset(BuildKey(node, port));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    IFMapNode *adj_node = agent()->cfg_listener()->FindAdjacentIFMapNode
        (agent(), node, "virtual-machine-interface");
    if (adj_node != NULL) {
        DBRequest vmi_req;
        if (VmiIFNodeToReq(adj_node, vmi_req)) {
            Enqueue(&vmi_req);
        }
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, port));

    Enqueue(&req);
    VmInterface::LogicalPortSync(this, node);
    return false;
}

#if 0
void LogicalInterface::SetSandeshData(SandeshLogicalInterface *data) const {
}

static void SetLogicalInterfaceSandeshData(const LogicalInterface *entry,
                                       SandeshLogicalInterface *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    data->set_fq_name(entry->fq_name());
    data->set_name(entry->name());
    if (entry->physical_port()) {
        data->set_physical_port(entry->physical_port()->name());
    } else {
        data->set_physical_port("INVALID");
    }

    if (entry->vm_interface()) {
        data->set_vif(entry->vm_interface()->name());
    } else {
        data->set_vif("INVALID");
    }
    entry->SetSandeshData(data);
}

bool LogicalInterface::DBEntrySandesh(Sandesh *resp, std::string &name) const {
    SandeshLogicalInterfaceListResp *port_resp =
        static_cast<SandeshLogicalInterfaceListResp *>(resp);

    if (name.empty() || name_ == name) {
        SandeshLogicalInterface data;
        SetLogicalInterfaceSandeshData(this, &data);
        std::vector<SandeshLogicalInterface> &list =
            const_cast<std::vector<SandeshLogicalInterface>&>
            (port_resp->get_port_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void SandeshLogicalInterfaceReq::HandleRequest() const {
    LogicalInterfaceSandesh *sand = new LogicalInterfaceSandesh(context(), get_name());
    sand->DoSandesh();
}

void LogicalInterface::SendObjectLog(AgentLogEvent::type event) const {
    LogicalInterfaceObjectLogInfo info;

    string str;
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        default:
            str.assign("INVALID");
            break;
    }
    info.set_event(str);

    info.set_uuid(UuidToString(uuid_));
    info.set_fq_name(fq_name_);
    info.set_name(name_);
    if (physical_port_) {
        info.set_physical_port(physical_port_->name());
    } else {
        info.set_physical_port("INVALID");
    }
    info.set_ref_count(GetRefCount());
    LOGICAL_PORT_OBJECT_LOG_LOG("LogicalInterface", SandeshLevel::SYS_INFO, info);
}

#endif
