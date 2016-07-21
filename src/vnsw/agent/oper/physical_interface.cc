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
#include <oper/config_manager.h>

#include <vector>
#include <string>

using std::string;

/////////////////////////////////////////////////////////////////////////////
// PhysicalInterface routines
/////////////////////////////////////////////////////////////////////////////
PhysicalInterface::PhysicalInterface(const std::string &name) :
    Interface(Interface::PHYSICAL, nil_uuid(), name, NULL), persistent_(false),
    subtype_(INVALID), physical_device_(NULL) {
}

PhysicalInterface::~PhysicalInterface() {
}

PhysicalDevice *PhysicalInterface::physical_device() const {
    return physical_device_.get();
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

    PhysicalDevice *dev =
        table->agent()->physical_device_table()->Find(data->device_uuid_);
    if (dev != physical_device_.get()) {
        physical_device_.reset(dev);
        ret = true;
    }

    return ret;
}

bool PhysicalInterface::Delete(const DBRequest *req) {
    InterfaceNH::DeletePhysicalInterfaceNh(name_, mac_);
    return true;
}

std::string PhysicalInterface::GetPhysicalInterfaceName() const {
    std::size_t pos = name_.find_last_of(":");
    if (pos != string::npos) {
        return name_.substr(pos + 1);
    }
    return name_;
}

void PhysicalInterface::PostAdd() {
    InterfaceNH::CreatePhysicalInterfaceNh(name_, mac_);

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    if (table->agent()->test_mode()) {
        return;
    }

    std::string interface_name = name_;
    // Interfaces in VMWARE mode and having remote VMs
    // must be put into promiscuous mode
    if (subtype_ != VMWARE) {
        if (!table->agent()->server_gateway_mode() ||
            subtype_ == PhysicalInterface::FABRIC) {
            return;
        } else {
            interface_name = GetPhysicalInterfaceName();
        }
    }

    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name.c_str(), IF_NAMESIZE);
    if (ioctl(fd, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> setting promiscuous flag for interface <" << interface_name << ">");
        close(fd);
        return;
    }

    ifr.ifr_flags |= IFF_PROMISC;
    if (ioctl(fd, SIOCSIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> setting promiscuous flag for interface <" << interface_name << ">");
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
    intf->display_name_ = phy_data->display_name_;
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
PhysicalInterfaceData::PhysicalInterfaceData(Agent *agent, IFMapNode *node,
                                             const string &vrf_name,
                                             PhysicalInterface::SubType subtype,
                                             PhysicalInterface::EncapType encap,
                                             bool no_arp,
                                             const uuid &device_uuid,
                                             const string &display_name,
                                             const Ip4Address &ip,
                                             Interface::Transport transport) :
    InterfaceData(agent, node, transport), subtype_(subtype), encap_type_(encap),
    no_arp_(no_arp), device_uuid_(device_uuid), display_name_(display_name),
    ip_(ip) {
    EthInit(vrf_name);
}
    
/////////////////////////////////////////////////////////////////////////////
// Config handling routines
/////////////////////////////////////////////////////////////////////////////
static PhysicalInterfaceKey *BuildKey(const std::string &name) {
    return new PhysicalInterfaceKey(name);
}

bool InterfaceTable::PhysicalInterfaceIFNodeToReq(IFMapNode *node,
                                                  DBRequest &req,
                                                  const boost::uuids::uuid &u) {

    // Enqueue request to config-manager if add/change
    if ((req.oper != DBRequest::DB_ENTRY_DELETE) &&
                            (node->IsDeleted() == false)) {
        agent()->config_manager()->AddPhysicalInterfaceNode(node);
        return false;
    }

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

    if (elements.size() == 3 && device != agent()->agent_name()) {
        if (RemotePhysicalInterfaceIFNodeToReq(node, req, u)) {
            Enqueue(&req);
        }
        return false;
    }

    req.key.reset(BuildKey(node->name()));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    return true;
}

bool InterfaceTable::PhysicalInterfaceProcessConfig(IFMapNode *node,
        DBRequest &req, const boost::uuids::uuid &u) {

    if (node->IsDeleted()) {
        return false;
    }

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
        return RemotePhysicalInterfaceIFNodeToReq(node, req, u);
    }

    req.key.reset(BuildKey(node->name()));

    boost::uuids::uuid dev_uuid = nil_uuid();
    // Find link with physical-router adjacency
    IFMapNode *adj_node = NULL;
    adj_node = agent()->config_manager()->FindAdjacentIFMapNode(node,
                                                            "physical-router");
    if (adj_node) {
        autogen::PhysicalRouter *router =
            static_cast<autogen::PhysicalRouter *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = router->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   dev_uuid);
    }
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(new PhysicalInterfaceData(agent(), node,
                                             agent()->fabric_vrf_name(),
                                             PhysicalInterface::CONFIG,
                                             PhysicalInterface::ETHERNET,
                                             false, dev_uuid,
                                             port->display_name(),
                                             Ip4Address(0),
                                             Interface::TRANSPORT_ETHERNET));
    pi_ifnode_to_req_++;
    Enqueue(&req);
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
// Enqueue DBRequest to create a Host Interface
void PhysicalInterface::CreateReq(InterfaceTable *table, const string &ifname,
                                  const string &vrf_name, SubType subtype,
                                  EncapType encap, bool no_arp,
                                  const uuid &device_uuid,
                                  const Ip4Address &ip,
                                  Interface::Transport transport) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(new PhysicalInterfaceData(NULL, NULL, vrf_name, subtype,
                                             encap, no_arp, device_uuid,
                                             ifname, ip, transport));
    table->Enqueue(&req);
}

void PhysicalInterface::Create(InterfaceTable *table, const string &ifname,
                               const string &vrf_name, SubType subtype,
                               EncapType encap, bool no_arp,
                               const uuid &device_uuid,
                               const Ip4Address &ip,
                               Interface::Transport transport) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(new PhysicalInterfaceData(NULL, NULL, vrf_name, subtype,
                                             encap, no_arp, device_uuid,
                                             ifname, ip, transport));
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
