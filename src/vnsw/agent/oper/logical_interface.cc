/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <cfg/cfg_init.h>
#include <oper/agent_sandesh.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <oper/logical_interface.h>
#include <oper/vm_interface.h>
#include <oper/config_manager.h>
#include <oper/vn.h>

#include <vector>
#include <string>

using std::string;
using std::auto_ptr;
using boost::uuids::uuid;

/////////////////////////////////////////////////////////////////////////////
// LogicalInterface routines
/////////////////////////////////////////////////////////////////////////////
LogicalInterface::LogicalInterface(const boost::uuids::uuid &uuid,
                                   const std::string &name,
                                   const boost::uuids::uuid &logical_router_uuid) :
    Interface(Interface::LOGICAL, uuid, name, NULL, true, logical_router_uuid),
    display_name_(),
    physical_interface_(),
    vm_interface_(),
    physical_device_(NULL),
    phy_dev_display_name_(),
    phy_intf_display_name_(),
    vn_uuid_() {
}

LogicalInterface::~LogicalInterface() {
}

PhysicalDevice *LogicalInterface::physical_device() const {
    return physical_device_.get();
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

    if (phy_intf_display_name_ != data->phy_intf_display_name_) {
        OPER_TRACE_ENTRY(Trace, table,
                         "Changing Physical Interface display name from " \
                         + phy_intf_display_name_ + " to " +
                         data->phy_intf_display_name_);
        phy_intf_display_name_ = data->phy_intf_display_name_;
        ret = true;
    }

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, data->vm_interface_, "");
    Interface *intrface = static_cast<Interface *>
        (table->agent()->interface_table()->FindActiveEntry(&vmi_key));
    if (intrface != vm_interface_.get()) {
        vm_interface_.reset(intrface);
        ret = true;
    }

    boost::uuids::uuid old_vn_uuid = vn_uuid_;
    VmInterface *vmi = static_cast<VmInterface *>(vm_interface_.get());
    if (vm_interface_.get() && vmi->vn()) {
        vn_uuid_ = vmi->vn()->GetUuid();
    } else {
        vn_uuid_ = boost::uuids::nil_uuid();
    }
    if (old_vn_uuid != vn_uuid_)
        ret = true;

    vm_uuid_ = data->vm_interface_;

    PhysicalDevice *dev = table->agent()->physical_device_table()->
                          Find(data->device_uuid_);
    if (dev != physical_device_.get()) {
        physical_device_.reset(dev);
        ret = true;
    }

    if (phy_dev_display_name_ != data->phy_dev_display_name_) {
        OPER_TRACE_ENTRY(Trace, table,
                         "Changing Physical Device display name from " \
                         + phy_dev_display_name_ + " to " +
                         data->phy_dev_display_name_);
        phy_dev_display_name_ = data->phy_dev_display_name_;
        ret = true;
    }

    return ret;
}

bool LogicalInterface::Delete(const DBRequest *req) {
    return true;
}

void LogicalInterface::GetOsParams(Agent *agent) {
    os_params_.os_index_ = Interface::kInvalidIndex;
    os_params_.mac_.Zero();
    os_params_.os_oper_state_ = true;
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
LogicalInterfaceData::LogicalInterfaceData(Agent *agent, IFMapNode *node,
                                           const std::string &display_name,
                                           const std::string &port,
                                           const boost::uuids::uuid &vif,
                                           const uuid &device_uuid,
                                           const std::string &phy_dev_display_name,
                                           const std::string &phy_intf_display_name) :
    InterfaceData(agent, node, Interface::TRANSPORT_INVALID),
    display_name_(display_name),
    physical_interface_(port), vm_interface_(vif), device_uuid_(device_uuid),
    phy_dev_display_name_(phy_dev_display_name),
    phy_intf_display_name_(phy_intf_display_name) {
}

LogicalInterfaceData::~LogicalInterfaceData() {
}

//////////////////////////////////////////////////////////////////////////////
// VlanLogicalInterface routines
//////////////////////////////////////////////////////////////////////////////
VlanLogicalInterface::VlanLogicalInterface(const boost::uuids::uuid &uuid,
                                           const std::string &name,
                                           uint16_t vlan,
                                           const boost::uuids::uuid &logical_router_uuid) :
    LogicalInterface(uuid, name, logical_router_uuid), vlan_(vlan) {
}

VlanLogicalInterface::~VlanLogicalInterface() {
}

DBEntryBase::KeyPtr VlanLogicalInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VlanLogicalInterfaceKey(uuid_, name());
    return DBEntryBase::KeyPtr(key);
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
    return new VlanLogicalInterface(uuid_, name_, 0, boost::uuids::nil_uuid());
}

LogicalInterface *
VlanLogicalInterfaceKey::AllocEntry(const InterfaceTable *table,
                                    const InterfaceData *d) const {
    const VlanLogicalInterfaceData *data =
        static_cast<const VlanLogicalInterfaceData *>(d);
    VlanLogicalInterface *intf = new VlanLogicalInterface(uuid_, name_,
                                                data->vlan_,
                                                data->logical_router_uuid_);

    intf->OnChange(table, data);
    return intf;
}

InterfaceKey *VlanLogicalInterfaceKey::Clone() const {
    return new VlanLogicalInterfaceKey(uuid_, name_);
}

VlanLogicalInterfaceData::VlanLogicalInterfaceData
(Agent *agent, IFMapNode *node, const std::string &display_name,
 const std::string &physical_interface,
 const boost::uuids::uuid &vif, const boost::uuids::uuid &u,
 const std::string &phy_dev_display_name,
 const std::string &phy_intf_display_name, uint16_t vlan) :
    LogicalInterfaceData(agent, node, display_name, physical_interface, vif, u,
                         phy_dev_display_name, phy_intf_display_name),
    vlan_(vlan) {
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
static LogicalInterfaceKey *BuildKey(IFMapNode *node,
                                     const boost::uuids::uuid &u) {
    return new VlanLogicalInterfaceKey(u, node->name());
}

static LogicalInterfaceData *BuildData(Agent *agent, IFMapNode *node,
                                       const uuid &u,
                                       const autogen::LogicalInterface *port) {
    // Find link with physical-interface adjacency
    string physical_interface;
    string phy_dev_display_name;
    string phy_intf_display_name;
    IFMapNode *adj_node = NULL;
    boost::uuids::uuid dev_uuid = boost::uuids::nil_uuid();
    adj_node = agent->config_manager()->FindAdjacentIFMapNode(node,
            "physical-interface");
    IFMapNode *prouter_node = NULL;

    if (adj_node) {
        physical_interface = adj_node->name();
        autogen::PhysicalInterface *port =
            static_cast <autogen::PhysicalInterface *>(adj_node->GetObject());
        assert(port);
        phy_intf_display_name = port->display_name();
        prouter_node = agent->config_manager()->
            FindAdjacentIFMapNode(adj_node, "physical-router");
        if (prouter_node) {
            autogen::PhysicalRouter *router =
                static_cast<autogen::PhysicalRouter *>(prouter_node->GetObject());
            phy_dev_display_name = router->display_name();
            autogen::IdPermsType id_perms = router->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       dev_uuid);
        }
    }

    // Find link with virtual-machine-interface adjacency
    boost::uuids::uuid vmi_uuid = boost::uuids::nil_uuid();
    adj_node = agent->config_manager()->FindAdjacentIFMapNode
        (node, "virtual-machine-interface");
    if (adj_node) {
        autogen::VirtualMachineInterface *vmi =
            static_cast<autogen::VirtualMachineInterface *>
            (adj_node->GetObject());
        autogen::IdPermsType id_perms = vmi->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   vmi_uuid);
    }

    string dev_name;
    adj_node = agent->config_manager()->FindAdjacentIFMapNode
        (node, "physical-router");
    if (adj_node) {
        dev_name = adj_node->name();
        if (dev_uuid != boost::uuids::nil_uuid()) {
            IFMAP_ERROR(LogicalInterfaceConfiguration,
                "Both physical-router and physical-interface links for "
                "interface:", node->name(),
                "physical interface", physical_interface,
                "prouter name", dev_name);
        }
        autogen::PhysicalRouter *router =
            static_cast<autogen::PhysicalRouter *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = router->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   dev_uuid);
    }

    // Logical-Interface must have VLAN-Tag field. Ignore the interface if
    // VLAN-Tag is not yet present
    if (port->IsPropertySet(autogen::LogicalInterface::VLAN_TAG) == false) {
        OperConfigInfo t;
        t.set_name(node->name());
        t.set_uuid(UuidToString(u));
        t.set_message("VLAN-Tag property not set. Ignoring node");

        OPER_IFMAP_TRACE(Config, t);
        return NULL;
    }

    return new VlanLogicalInterfaceData(agent, node, port->display_name(),
                                        physical_interface, vmi_uuid, dev_uuid,
                                        phy_dev_display_name,
                                        phy_intf_display_name,
                                        port->vlan_tag());
}

bool InterfaceTable::LogicalInterfaceIFNodeToUuid(IFMapNode *node,
        boost::uuids::uuid &u) {

    autogen::LogicalInterface *port =
        static_cast <autogen::LogicalInterface *>(node->GetObject());
    assert(port);

    autogen::IdPermsType id_perms = port->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool InterfaceTable::LogicalInterfaceProcessConfig(IFMapNode *node,
                                                   DBRequest &req,
                                                   const boost::uuids::uuid &u) {
    autogen::LogicalInterface *port =
        static_cast <autogen::LogicalInterface *>(node->GetObject());
    assert(port);

    req.key.reset(BuildKey(node, u));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, u, port));

    if (req.data.get() != NULL) {
        li_ifnode_to_req_++;
        Enqueue(&req);
    }
    return false;
}

bool InterfaceTable::LogicalInterfaceIFNodeToReq(IFMapNode *node,
                                                 DBRequest &req,
                                                 const boost::uuids::uuid &u) {
    autogen::LogicalInterface *port =
        static_cast <autogen::LogicalInterface *>(node->GetObject());
    assert(port);

    req.key.reset(BuildKey(node, u));
    if (req.oper == DBRequest::DB_ENTRY_DELETE || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddLogicalInterfaceNode(node);
    return false;
}
