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
#include <oper/nexthop.h>

#include <vector>
#include <string>

using std::string;
using boost::uuids::uuid;

/////////////////////////////////////////////////////////////////////////////
// RemotePhysicalInterface routines
/////////////////////////////////////////////////////////////////////////////
RemotePhysicalInterface::RemotePhysicalInterface(const std::string &name) :
    Interface(Interface::REMOTE_PHYSICAL, nil_uuid(), name, NULL),
    display_name_(), physical_device_(NULL) {
}

RemotePhysicalInterface::~RemotePhysicalInterface() {
}

string RemotePhysicalInterface::ToString() const {
    return "Remote-PORT <" + name() + ">";
}

bool RemotePhysicalInterface::CmpInterface(const DBEntry &rhs) const {
    const RemotePhysicalInterface &a =
        static_cast<const RemotePhysicalInterface &>(rhs);
    return name_ < a.name_;
}

DBEntryBase::KeyPtr RemotePhysicalInterface::GetDBRequestKey() const {
    InterfaceKey *key = new RemotePhysicalInterfaceKey(name_);
    return DBEntryBase::KeyPtr(key);
}

bool RemotePhysicalInterface::OnChange(const InterfaceTable *table,
                                       const RemotePhysicalInterfaceData *data){
    bool ret = false;

    // Handle VRF Change
    VrfKey key(data->vrf_name_);
    VrfEntry *new_vrf = static_cast<VrfEntry *>
        (table->agent()->vrf_table()->FindActiveEntry(&key));
    if (new_vrf != vrf_.get()) {
        vrf_.reset(new_vrf);
        ret = true;
    }

    PhysicalDevice *dev =
        table->agent()->physical_device_table()->Find(data->device_uuid_);
    if (dev != physical_device_.get()) {
        physical_device_.reset(dev);
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// RemotePhysicalInterfaceKey routines
/////////////////////////////////////////////////////////////////////////////
RemotePhysicalInterfaceKey::RemotePhysicalInterfaceKey(const std::string &name):
    InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::REMOTE_PHYSICAL,
                 nil_uuid(), name, false) {
}

RemotePhysicalInterfaceKey::~RemotePhysicalInterfaceKey() {
}

Interface *RemotePhysicalInterfaceKey::AllocEntry(const InterfaceTable *table)
    const {
    return new RemotePhysicalInterface(name_);
}

Interface *RemotePhysicalInterfaceKey::AllocEntry(const InterfaceTable *table,
                                                  const InterfaceData *data)
    const {
    RemotePhysicalInterface *intf = new RemotePhysicalInterface(name_);

    const RemotePhysicalInterfaceData *phy_data =
        static_cast<const RemotePhysicalInterfaceData *>(data);

    intf->OnChange(table, phy_data);
    return intf;
}

InterfaceKey *RemotePhysicalInterfaceKey::Clone() const {
    return new RemotePhysicalInterfaceKey(name_);
}

/////////////////////////////////////////////////////////////////////////////
// RemotePhysicalInterfaceData routines
/////////////////////////////////////////////////////////////////////////////
RemotePhysicalInterfaceData::RemotePhysicalInterfaceData
    (IFMapNode *node, const string &display_name, const string &vrf_name,
     const uuid &device_uuid) :
    InterfaceData(node), display_name_(display_name),
    device_uuid_(device_uuid) {
    RemotePhysicalPortInit(vrf_name);
}

RemotePhysicalInterfaceData::~RemotePhysicalInterfaceData() {
}

/////////////////////////////////////////////////////////////////////////////
// Config handling routines
/////////////////////////////////////////////////////////////////////////////
static RemotePhysicalInterfaceKey *BuildKey
(const autogen::PhysicalInterface *port) {
    autogen::IdPermsType id_perms = port->id_perms();
    return new RemotePhysicalInterfaceKey(port->display_name());
}

static RemotePhysicalInterfaceData *BuildData
(const Agent *agent, IFMapNode *node, const autogen::PhysicalInterface *port) {
    boost::uuids::uuid dev_uuid = nil_uuid();
    // Find link with physical-router adjacency
    IFMapNode *adj_node = NULL;
    adj_node = agent->cfg_listener()->FindAdjacentIFMapNode(agent, node,
                                                            "physical-router");
    if (adj_node) {
        autogen::PhysicalRouter *router =
            static_cast<autogen::PhysicalRouter *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = router->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   dev_uuid);
    }

    return new RemotePhysicalInterfaceData(node, port->display_name(),
                                           agent->fabric_vrf_name(), dev_uuid);
}

bool InterfaceTable::RemotePhysicalInterfaceIFNodeToReq(IFMapNode *node,
                                                  DBRequest &req) {
    autogen::PhysicalInterface *port =
        static_cast <autogen::PhysicalInterface *>(node->GetObject());
    assert(port);

    req.key.reset(BuildKey(port));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, port));
    return true;
}

void RemotePhysicalInterface::ConfigEventHandler(IFMapNode *node) {
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
static void SetReq(DBRequest *req, const string &fqdn,
                   const string &display_name, const string &vrf_name,
                   const uuid &device_uuid) {
    req->key.reset(new RemotePhysicalInterfaceKey(fqdn));
    req->data.reset(new RemotePhysicalInterfaceData(NULL, display_name,
                                                    vrf_name, device_uuid));
}

void RemotePhysicalInterface::CreateReq(InterfaceTable *table,
                                        const string &fqdn,
                                        const string &display_name,
                                        const string &vrf_name,
                                        const uuid &device_uuid) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    SetReq(&req, fqdn, display_name, vrf_name, device_uuid);
    table->Enqueue(&req);
}

void RemotePhysicalInterface::Create(InterfaceTable *table, const string &fqdn,
                                     const string &display_name,
                                     const string &vrf_name,
                                     const uuid &device_uuid) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    SetReq(&req, fqdn, display_name, vrf_name, device_uuid);
    table->Process(req);
}

// Enqueue DBRequest to delete a Host Interface
void RemotePhysicalInterface::DeleteReq(InterfaceTable *table,
                                        const string &fqdn) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new RemotePhysicalInterfaceKey(fqdn));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

void RemotePhysicalInterface::Delete(InterfaceTable *table,
                                     const string &fqdn) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new RemotePhysicalInterfaceKey(fqdn));
    req.data.reset(NULL);
    table->Process(req);
}
