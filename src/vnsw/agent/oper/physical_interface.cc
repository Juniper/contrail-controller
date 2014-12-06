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

/////////////////////////////////////////////////////////////////////////////
// PhysicalInterface routines
/////////////////////////////////////////////////////////////////////////////
PhysicalInterface::PhysicalInterface(const std::string &name) :
    Interface(Interface::PHYSICAL, nil_uuid(), name, NULL), persistent_(false),
    subtype_(INVALID) {
}

PhysicalInterface::~PhysicalInterface() {
}

string PhysicalInterface::ToString() const {
    return "PORT <" + name() + ">";
}

bool PhysicalInterface::CmpInterface(const DBEntry &rhs) const {
    const PhysicalInterface &a = static_cast<const PhysicalInterface &>(rhs);
    return name_ < a.name_;
}

DBEntryBase::KeyPtr PhysicalInterface::GetDBRequestKey() const {
    InterfaceKey *key = new PhysicalInterfaceKey(name_);
    return DBEntryBase::KeyPtr(key);
}

bool PhysicalInterface::OnChange(const InterfaceTable *table,
                                 const PhysicalInterfaceData *data) {
    bool ret = false;

    // Handle VRF Change
    VrfKey key(data->vrf_name_);
    VrfEntry *new_vrf = static_cast<VrfEntry *>
        (table->agent()->vrf_table()->FindActiveEntry(&key));
    if (new_vrf != vrf_.get()) {
        vrf_.reset(new_vrf);
        ret = true;
    }

    return ret;
}

bool PhysicalInterface::Delete(const DBRequest *req) {
    InterfaceNH::DeletePhysicalInterfaceNh(name_);
    return true;
}

void PhysicalInterface::PostAdd() {
    InterfaceNH::CreatePhysicalInterfaceNh(name_, mac_);

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    if (table->agent()->test_mode()) {
        return;
    }

    // Interfaces in VMWARE must be put into promiscous mode
    if (subtype_ != VMWARE) {
        return;
    }

    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(fd, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> setting promiscuous flag for interface <" << name_ << ">");
        close(fd);
        return;
    }

    ifr.ifr_flags |= IFF_PROMISC;
    if (ioctl(fd, SIOCSIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> setting promiscuous flag for interface <" << name_ << ">");
        close(fd);
        return;
    }

    close(fd);
}

/////////////////////////////////////////////////////////////////////////////
// PhysicalInterfaceKey routines
/////////////////////////////////////////////////////////////////////////////
PhysicalInterfaceKey::PhysicalInterfaceKey(const std::string &name) :
    InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::PHYSICAL, nil_uuid(),
                 name, false) {
}

PhysicalInterfaceKey::~PhysicalInterfaceKey() {
}

Interface *PhysicalInterfaceKey::AllocEntry(const InterfaceTable *table) const {
    return new PhysicalInterface(name_);
}

Interface *PhysicalInterfaceKey::AllocEntry(const InterfaceTable *table,
                                            const InterfaceData *data) const {
    PhysicalInterface *intf = new PhysicalInterface(name_);
    const PhysicalInterfaceData *phy_data =
        static_cast<const PhysicalInterfaceData *>(data);
    intf->encap_type_ = phy_data->encap_type_;
    intf->no_arp_ = phy_data->no_arp_;
    intf->subtype_ = phy_data->subtype_;
    if (intf->subtype_ == PhysicalInterface::VMWARE ||
        intf->subtype_ == PhysicalInterface::CONFIG) {
        intf->persistent_ = true;
    }

    intf->OnChange(table, phy_data);
    return intf;
}

InterfaceKey *PhysicalInterfaceKey::Clone() const {
    return new PhysicalInterfaceKey(name_);
}

/////////////////////////////////////////////////////////////////////////////
// PhysicalInterfaceData routines
/////////////////////////////////////////////////////////////////////////////
PhysicalInterfaceData::PhysicalInterfaceData(IFMapNode *node,
                                             const string &vrf_name,
                                             PhysicalInterface::SubType subtype,
                                             PhysicalInterface::EncapType encap,
                                             bool no_arp) :
    InterfaceData(node), subtype_(subtype), encap_type_(encap),
    no_arp_(no_arp) {
    EthInit(vrf_name);
}
    
/////////////////////////////////////////////////////////////////////////////
// Config handling routines
/////////////////////////////////////////////////////////////////////////////
static PhysicalInterfaceKey *BuildKey(const autogen::PhysicalInterface *port) {
    autogen::IdPermsType id_perms = port->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return new PhysicalInterfaceKey(port->display_name());
}

bool InterfaceTable::PhysicalInterfaceIFNodeToReq(IFMapNode *node,
                                                  DBRequest &req) {
    autogen::PhysicalInterface *port =
        static_cast <autogen::PhysicalInterface *>(node->GetObject());
    assert(port);

    // Get the physical-router from FQDN
    string device = "";
    vector<string> elements;
    split(elements, node->name(), boost::is_any_of(":"), boost::token_compress_on);
    if (elements.size() == 3) {
        device = elements[1];
    }

    // If physical-router does not match agent_name, treat as remote interface
    if (elements.size() == 3 && device != agent()->agent_name()) {
        return RemotePhysicalInterfaceIFNodeToReq(node, req);
    }

    req.key.reset(BuildKey(port));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(new PhysicalInterfaceData(node, agent()->fabric_vrf_name(),
                                             PhysicalInterface::CONFIG,
                                             PhysicalInterface::ETHERNET,
                                             false));
    Enqueue(&req);
    VmInterface::PhysicalPortSync(this, node);
    return false;
}

void PhysicalInterface::ConfigEventHandler(IFMapNode *node) {
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
// Enqueue DBRequest to create a Host Interface
void PhysicalInterface::CreateReq(InterfaceTable *table, const string &ifname,
                                  const string &vrf_name, SubType subtype,
                                  EncapType encap, bool no_arp) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(new PhysicalInterfaceData(NULL, vrf_name, subtype, encap,
                                             no_arp));
    table->Enqueue(&req);
}

void PhysicalInterface::Create(InterfaceTable *table, const string &ifname,
                               const string &vrf_name, SubType subtype,
                               EncapType encap, bool no_arp) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(new PhysicalInterfaceData(NULL, vrf_name, subtype, encap,
                                             no_arp));
    table->Process(req);
}

// Enqueue DBRequest to delete a Host Interface
void PhysicalInterface::DeleteReq(InterfaceTable *table, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

void PhysicalInterface::Delete(InterfaceTable *table, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(NULL);
    table->Process(req);
}
