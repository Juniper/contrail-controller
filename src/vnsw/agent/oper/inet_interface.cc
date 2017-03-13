/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include <boost/uuid/uuid_io.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"
#include "net/address_util.h"

#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/interface_common.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;
using namespace boost::uuids;

/////////////////////////////////////////////////////////////////////////////
// Inet Interface Key and Data methods
/////////////////////////////////////////////////////////////////////////////
InetInterfaceKey::InetInterfaceKey(const std::string &name) :
    InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::INET, nil_uuid(), name,
                 false) {
}

Interface *InetInterfaceKey::AllocEntry(const InterfaceTable *table) const {
    return new InetInterface(name_);
}

Interface *InetInterfaceKey::AllocEntry(const InterfaceTable *table,
                                        const InterfaceData *data)const {
    const InetInterfaceData *vhost_data  = 
        static_cast<const InetInterfaceData *>(data);

    VrfKey key(data->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>
        (table->agent()->vrf_table()->FindActiveEntry(&key));
    assert(vrf);

	Interface *xconnect = NULL;
    if (vhost_data->sub_type_ == InetInterface::VHOST) {
        PhysicalInterfaceKey key(vhost_data->xconnect_);
        xconnect = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
        assert(xconnect != NULL);
    }

    InetInterface *intf = new InetInterface(name_, vhost_data->sub_type_, vrf,
                                            vhost_data->ip_addr_,
                                            vhost_data->plen_, vhost_data->gw_,
                                            xconnect, vhost_data->vn_name_);
    return intf;
}

InterfaceKey *InetInterfaceKey::Clone() const {
    return new InetInterfaceKey(name_);
}

InetInterfaceData::InetInterfaceData(InetInterface::SubType sub_type,
                                     const std::string &vrf_name,
                                     const Ip4Address &addr, int plen,
                                     const Ip4Address &gw,
                                     const std::string &xconnect,
                                     const std::string vn_name,
                                     Interface::Transport transport) :
    InterfaceData(NULL, NULL, transport), sub_type_(sub_type), ip_addr_(addr),
    plen_(plen), gw_(gw), xconnect_(xconnect), vn_name_(vn_name) {
    InetInit(vrf_name);
}

/////////////////////////////////////////////////////////////////////////////
// SIMPLE GATEWAY Utility functions
/////////////////////////////////////////////////////////////////////////////
void InetInterface::ActivateSimpleGateway() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    // Create InterfaceNH before MPLS is created
    InterfaceNH::CreateInetInterfaceNextHop(name(), vrf()->GetName(),
                                            agent->pkt_interface_mac());

    //There is no policy enabled nexthop created for VGW interface,
    //hence use interface nexthop without policy as flow key index
    InterfaceNHKey key(new InetInterfaceKey(name()),
                       false, InterfaceNHFlags::INET4,
                       agent->pkt_interface_mac());
    flow_key_nh_ = static_cast<const NextHop *>(
            agent->nexthop_table()->FindActiveEntry(&key));
    assert(flow_key_nh_);
    label_ = flow_key_nh_->mpls_label()->label();
}

void InetInterface::DeActivateSimpleGateway() {
    InetUnicastAgentRouteTable *uc_rt_table =
        (VrfTable::GetInstance()->GetInet4UnicastRouteTable(vrf()->GetName()));

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    // Delete routes
    Ip4Address addr = Address::GetIp4SubnetAddress(ip_addr_, plen_);
    uc_rt_table->DeleteReq(agent->local_vm_peer(), agent->fabric_vrf_name(),
                           addr, plen_, NULL);

    uc_rt_table->DeleteReq(agent->local_vm_peer(),
                           vrf()->GetName(), Ip4Address(0), 0, NULL);

    // Delete NH
    InterfaceNH::DeleteInetInterfaceNextHop(name(), agent->pkt_interface_mac());

    // Reset Label
    label_ = MplsTable::kInvalidLabel;
    flow_key_nh_ = NULL;
}

/////////////////////////////////////////////////////////////////////////////
// VHOST / LL Utility functions
/////////////////////////////////////////////////////////////////////////////

// Add default route with given gateway
static void AddDefaultRoute(Agent *agent, InetUnicastAgentRouteTable *table,
                            const VrfEntry *vrf, const Interface *xconnect,
                            const Ip4Address &gw, const string &vn_name) {

    if (xconnect) {
        const PhysicalInterface *physical_intf =
            static_cast<const PhysicalInterface *>(xconnect);
        if (physical_intf->no_arp() ||
            (physical_intf->encap_type() == PhysicalInterface::RAW_IP)) {
            table->AddInterfaceRouteReq(agent, agent->local_peer(),
                                        vrf->GetName(), Ip4Address(0), 0,
                                        xconnect, vn_name);
            return;
        }
    }

    table->AddGatewayRoute(agent->local_peer(),
                           vrf->GetName(), Ip4Address(0), 0, gw, vn_name,
                           MplsTable::kInvalidLabel, SecurityGroupList(),
                           CommunityList());
    return;
}

static void DeleteDefaultRoute(Agent *agent, InetUnicastAgentRouteTable *table,
                               const VrfEntry *vrf, const Ip4Address &addr) {
    table->Delete(agent->local_peer(), vrf->GetName(), Ip4Address(0), 0);
}

// Following routes are added due o an inet interface
// - Receive route for IP address assigned
// - Receive route for the sub-net broadcast address
// - Resolve route for the subnet address
static void AddHostRoutes(Agent *agent, InetUnicastAgentRouteTable *table,
                          const VrfEntry *vrf, const string &interface,
                          const Interface *xconnect, const Ip4Address &addr,
                          int plen, const string &vn_name) {

    table->AddVHostRecvRoute(agent->local_peer(), vrf->GetName(), interface,
                             addr, 32, vn_name, false);

    const PhysicalInterface *physical_intf =
        static_cast<const PhysicalInterface *>(xconnect);
    if (physical_intf) {
       if (physical_intf->no_arp())
           return;
       if (physical_intf->encap_type() != PhysicalInterface::ETHERNET)
           return;
    }

    table->AddVHostSubnetRecvRoute(agent->local_peer(), vrf->GetName(),
                                   interface,
                                   GetIp4SubnetBroadcastAddress(addr, plen),
                                   32, vn_name, false);
    if (agent->params()->subnet_hosts_resolvable() == true) {
        InetInterfaceKey intf_key(interface);
        table->AddResolveRoute(agent->local_peer(), vrf->GetName(),
                Address::GetIp4SubnetAddress(addr, plen), plen,
                intf_key, MplsTable::kInvalidLabel, false, vn_name,
                SecurityGroupList());
    }
}

static void DeleteHostRoutes(Agent *agent, InetUnicastAgentRouteTable *table,
                             const VrfEntry *vrf, const Interface *xconnect,
                             const Ip4Address &addr, int plen) {
    table->Delete(agent->local_peer(), vrf->GetName(), addr, 32);
    const PhysicalInterface *physical_intf =
        static_cast<const PhysicalInterface *>(xconnect);
    if (physical_intf && physical_intf->no_arp()) {
        return;
    }

    table->Delete(agent->local_peer(), vrf->GetName(),
                  Address::GetIp4SubnetAddress(addr, plen), plen);
    table->Delete(agent->local_peer(), vrf->GetName(),
                  GetIp4SubnetBroadcastAddress(addr, plen), 32);
}

void InetInterface::AddHostMulticastRoutes() {
    VrfTable *vrf_table = static_cast<VrfTable *>(vrf()->get_table());
    InetUnicastAgentRouteTable *uc_rt_table =
        (vrf_table->GetInet4UnicastRouteTable(vrf()->GetName()));
    boost::system::error_code ec;
    // Add v4 route for covering multicast
    uc_rt_table->
        AddVHostRecvRoute(uc_rt_table->agent()->local_peer(),
                          vrf()->GetName(),
                          name_,
                          Ip4Address::from_string(IPV4_MULTICAST_BASE_ADDRESS,
                                                  ec),
                          MULTICAST_BASE_ADDRESS_PLEN,
                          vn_name_,
                          false);
    // Add v6 route for covering multicast
    uc_rt_table->
        AddVHostRecvRoute(uc_rt_table->agent()->local_peer(),
                          vrf()->GetName(),
                          name_,
                          Ip6Address::from_string(IPV6_MULTICAST_BASE_ADDRESS,
                                                  ec),
                          MULTICAST_BASE_ADDRESS_PLEN,
                          vn_name_,
                          false);
}

void InetInterface::DelHostMulticastRoutes() {
    VrfTable *vrf_table = static_cast<VrfTable *>(vrf()->get_table());
    InetUnicastAgentRouteTable *uc_rt_table =
        (vrf_table->GetInet4UnicastRouteTable(vrf()->GetName()));
    boost::system::error_code ec;
    // Del v4 route for covering multicast
    uc_rt_table->Delete(uc_rt_table->agent()->local_peer(),
                        vrf()->GetName(),
                        Ip4Address::from_string(IPV4_MULTICAST_BASE_ADDRESS,
                                                ec),
                        MULTICAST_BASE_ADDRESS_PLEN);
    // Del v6 route for covering multicast
    uc_rt_table->Delete(uc_rt_table->agent()->local_peer(),
                        vrf()->GetName(),
                        Ip6Address::from_string(IPV6_MULTICAST_BASE_ADDRESS,
                                                ec),
                        MULTICAST_BASE_ADDRESS_PLEN);
}

// Things to do to activate VHOST/LL interface
// 1. Create the receive next-hops for interface (with policy and witout policy)
// 2. Add routes needed to manage the IP address on interface
// 3. Add default route for the gateway
// 4. Add broadcast route in multicast route table
void InetInterface::ActivateHostInterface() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    // Create receive nexthops
    ReceiveNH::Create(agent->nexthop_table(), name_);

    VrfTable *vrf_table = static_cast<VrfTable *>(vrf()->get_table());
    InetUnicastAgentRouteTable *uc_rt_table =
        (vrf_table->GetInet4UnicastRouteTable(vrf()->GetName()));
    if (ip_addr_.to_ulong()) {
        AddHostRoutes(agent, uc_rt_table, vrf(), name(), xconnect_.get(),
                      ip_addr_, plen_, vn_name_);
    }
    AddHostMulticastRoutes();

    if (gw_.to_ulong()) {
        AddDefaultRoute(agent, uc_rt_table, vrf(), xconnect_.get(), gw_,
                        vn_name_);
    }

    ReceiveNHKey nh_key(new InetInterfaceKey(name_), false);
    flow_key_nh_ = static_cast<const NextHop *>(
            agent->nexthop_table()->FindActiveEntry(&nh_key));
}

void InetInterface::DeActivateHostInterface() {
    ipv4_active_ = false;

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfTable *vrf_table = static_cast<VrfTable *>(vrf()->get_table());
    InetUnicastAgentRouteTable *uc_rt_table =
        (vrf_table->GetInet4UnicastRouteTable(vrf()->GetName()));
    if (ip_addr_.to_ulong()) {
        DeleteHostRoutes(agent, uc_rt_table, vrf(), xconnect_.get(), ip_addr_,
                         plen_);
    }
    DelHostMulticastRoutes();

    if (gw_.to_ulong()) {
        DeleteDefaultRoute(agent, uc_rt_table, vrf(), gw_);
    }

    // Delete receive nexthops
    ReceiveNH::Delete(agent->nexthop_table(), name_);
    flow_key_nh_ = NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Inet Interface methods
/////////////////////////////////////////////////////////////////////////////
InetInterface::InetInterface(const std::string &name) :
    Interface(Interface::INET, nil_uuid(), name, NULL),
    sub_type_(VHOST), ip_addr_(0), plen_(0), gw_(0), xconnect_(NULL),
    vn_name_("") {
    ipv4_active_ = false;
    l2_active_ = false;
}

InetInterface::InetInterface(const std::string &name, SubType sub_type,
                             VrfEntry *vrf, const Ip4Address &ip_addr, int plen,
                             const Ip4Address &gw, Interface *xconnect,
                             const std::string &vn_name) :
    Interface(Interface::INET, nil_uuid(), name, vrf), sub_type_(sub_type),
    ip_addr_(ip_addr), plen_(plen), gw_(gw), xconnect_(xconnect),
    vn_name_(vn_name) {
    ipv4_active_ = false;
    l2_active_ = false;
}

bool InetInterface::CmpInterface(const DBEntry &rhs) const {
    const InetInterface &intf = static_cast<const InetInterface &>(rhs);
    return name_ < intf.name_;
}

DBEntryBase::KeyPtr InetInterface::GetDBRequestKey() const {
    InterfaceKey *key = new InetInterfaceKey(name_);
    return DBEntryBase::KeyPtr(key);
}

void InetInterface::Activate() {
    ipv4_active_ = true;
    if (sub_type_ == SIMPLE_GATEWAY) {
        InetInterface::ActivateSimpleGateway();
    } else {
        InetInterface::ActivateHostInterface();
    }

    return;
}

void InetInterface::DeActivate() {
    ipv4_active_ = false;

    if (sub_type_ == SIMPLE_GATEWAY) {
        InetInterface::DeActivateSimpleGateway();
    } else {
        InetInterface::DeActivateHostInterface();
    }
}

bool InetInterface::Delete(const DBRequest *req) {
    DeActivate();
    return true;
}

// Interface Activate cannot be done in AllocEntry. It must be done in PostAdd 
// Activating an interface results in adding Interface Nexthops. Creating of
// Interface NH from AllocEntry will fail since the interface is not yet added
// in DB Table PartitionInterface. So, Activate an interface in PostAdd
void InetInterface::PostAdd() {
    Activate();
}

bool InetInterface::OnChange(InetInterfaceData *data) {
    bool ret = false;

    // A Delete followed by Add will result in OnChange callback. Interface is
    // Deactivated on delete. Activate it again to create Interface NH etc...
    if (ipv4_active_ != true) {
        Activate();
        ret = true;
    }

    if (sub_type_ == SIMPLE_GATEWAY) {
        return ret;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    VrfTable *vrf_table = static_cast<VrfTable *>(vrf()->get_table());
    InetUnicastAgentRouteTable *uc_rt_table =
        (vrf_table->GetInet4UnicastRouteTable(vrf()->GetName()));

    if (ip_addr_ != data->ip_addr_ || plen_ != data->plen_) {
        // Delete routes based on old ip-addr and prefix
        if (ip_addr_.to_ulong()) {
            DeleteHostRoutes(agent, uc_rt_table, vrf(), xconnect_.get(),
                             ip_addr_, plen_);
        }

        ip_addr_ = data->ip_addr_;
        plen_ = data->plen_;
        vn_name_ = data->vn_name_;
        // Add routes for new ip-address and prefix
        if (data->ip_addr_.to_ulong()) {
            AddHostRoutes(agent, uc_rt_table, vrf(), name(), xconnect_.get(),
                          ip_addr_, plen_, vn_name_);
        }
        ret = true;
    } else if (vn_name_ != data->vn_name_) {
        // Change in vn_name, update route with new VN Name
        vn_name_ = data->vn_name_;
        AddHostRoutes(agent, uc_rt_table, vrf(), name(), xconnect_.get(),
                      ip_addr_, plen_, vn_name_);
        ret = true;
    }

    if (gw_ != data->gw_) {
        // Delete routes based on old gateway
        if (gw_.to_ulong()) {
            DeleteDefaultRoute(agent, uc_rt_table, vrf(), gw_);
        }

        gw_ = data->gw_;
        // Add route for new gateway
        if (gw_.to_ulong()) {
            AddDefaultRoute(agent, uc_rt_table, vrf(), xconnect_.get(), gw_,
                            vn_name_);
        }

        ret = true;
    }

    if (transport_ != data->transport_) {
        transport_ = data->transport_;
    }
    return ret;
}

// Helper to Inet Interface
void InetInterface::CreateReq(InterfaceTable *table, const std::string &ifname,
                              SubType sub_type, const std::string &vrf_name,
                              const Ip4Address &addr, int plen,
                              const Ip4Address &gw,
                              const std::string &xconnect,
                              const std::string &vn_name,
                              Interface::Transport transport) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetInterfaceKey(ifname));
    req.data.reset(new InetInterfaceData(sub_type, vrf_name, Ip4Address(addr),
                                         plen, Ip4Address(gw), xconnect,
                                         vn_name, transport));
    table->Enqueue(&req);
}

// Helper to Inet Interface
void InetInterface::Create(InterfaceTable *table, const std::string &ifname,
                           SubType sub_type, const std::string &vrf_name,
                           const Ip4Address &addr, int plen,
                           const Ip4Address &gw,
                           const std::string &xconnect,
                           const std::string &vn_name,
                           Interface::Transport transport) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetInterfaceKey(ifname));
    req.data.reset(new InetInterfaceData(sub_type, vrf_name, Ip4Address(addr),
                                         plen, Ip4Address(gw), xconnect,
                                         vn_name, transport));
    table->Process(req);
}

// Helper to delete Inet Interface
void InetInterface::DeleteReq(InterfaceTable *table, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetInterfaceKey(ifname));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

void InetInterface::Delete(InterfaceTable *table, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetInterfaceKey(ifname));
    req.data.reset(NULL);
    table->Process(req);
}
