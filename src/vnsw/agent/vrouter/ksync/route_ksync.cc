/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include "cmn/agent.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/mirror_table.h"
#include "oper/agent_route_walker.h"

#include "vrouter/ksync/interface_ksync.h"
#include "vrouter/ksync/nexthop_ksync.h"
#include "vrouter/ksync/route_ksync.h"

#include "ksync_init.h"
#include "vr_types.h"
#include "vr_defs.h"
#include "vr_nexthop.h"
#if defined(__FreeBSD__)
#include "vr_os.h"
#endif

RouteKSyncEntry::RouteKSyncEntry(RouteKSyncObject* obj, 
                                 const RouteKSyncEntry *entry, 
                                 uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), 
    rt_type_(entry->rt_type_), vrf_id_(entry->vrf_id_), 
    addr_(entry->addr_), src_addr_(entry->src_addr_), mac_(entry->mac_), 
    prefix_len_(entry->prefix_len_), nh_(entry->nh_), label_(entry->label_), 
    proxy_arp_(false), flood_dhcp_(entry->flood_dhcp_),
    address_string_(entry->address_string_),
    tunnel_type_(entry->tunnel_type_),
    wait_for_traffic_(entry->wait_for_traffic_),
    local_vm_peer_route_(entry->local_vm_peer_route_),
    flood_(entry->flood_), ethernet_tag_(entry->ethernet_tag_),
    layer2_control_word_(entry->layer2_control_word_) {
}

RouteKSyncEntry::RouteKSyncEntry(RouteKSyncObject* obj, const AgentRoute *rt) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    vrf_id_(rt->vrf_id()), mac_(), nh_(NULL), label_(0), proxy_arp_(false),
    flood_dhcp_(false), tunnel_type_(TunnelType::DefaultType()),
    wait_for_traffic_(false), local_vm_peer_route_(false),
    flood_(false), ethernet_tag_(0), layer2_control_word_(false) {
    boost::system::error_code ec;
    rt_type_ = rt->GetTableType();
    switch (rt_type_) {
    case Agent::INET4_UNICAST: {
          const InetUnicastRouteEntry *uc_rt =
              static_cast<const InetUnicastRouteEntry *>(rt);
          addr_ = uc_rt->addr();
          src_addr_ = IpAddress::from_string("0.0.0.0", ec).to_v4();
          prefix_len_ = uc_rt->plen();
          break;
    }
    case Agent::INET6_UNICAST: {
          const InetUnicastRouteEntry *uc_rt =
              static_cast<const InetUnicastRouteEntry *>(rt);
          addr_ = uc_rt->addr();
          src_addr_ = Ip6Address();
          prefix_len_ = uc_rt->plen();
          break;
    }
    case Agent::INET4_MULTICAST: {
          const Inet4MulticastRouteEntry *mc_rt = 
              static_cast<const Inet4MulticastRouteEntry *>(rt);
          addr_ = mc_rt->dest_ip_addr();
          src_addr_ = mc_rt->src_ip_addr();
          prefix_len_ = 32;
          break;
    }
    case Agent::BRIDGE: {
          const BridgeRouteEntry *l2_rt =
              static_cast<const BridgeRouteEntry *>(rt);
          mac_ = l2_rt->mac();
          prefix_len_ = 0;
          break;
    }
    default: {
          assert(0);
    }
    }
    address_string_ = rt->GetAddressString();
}

RouteKSyncEntry::~RouteKSyncEntry() {
}

KSyncDBObject *RouteKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool RouteKSyncEntry::UcIsLess(const KSyncEntry &rhs) const {
    const RouteKSyncEntry &entry = static_cast<const RouteKSyncEntry &>(rhs);
    if (vrf_id_ != entry.vrf_id_) {
        return vrf_id_ < entry.vrf_id_;
    }

    if (addr_ != entry.addr_) {
        return addr_ < entry.addr_;
    }

    return (prefix_len_ < entry.prefix_len_);
}

bool RouteKSyncEntry::McIsLess(const KSyncEntry &rhs) const {
    const RouteKSyncEntry &entry = static_cast<const RouteKSyncEntry &>(rhs);
    if (vrf_id_ != entry.vrf_id_) {
        return vrf_id_ < entry.vrf_id_;
    }

    if (src_addr_ != entry.src_addr_) {
        return src_addr_ < entry.src_addr_;
    }

    return (addr_ < entry.addr_);
}

bool RouteKSyncEntry::L2IsLess(const KSyncEntry &rhs) const {
    const RouteKSyncEntry &entry = static_cast<const RouteKSyncEntry &>(rhs);
 
    if (vrf_id_ != entry.vrf_id_) {
        return vrf_id_ < entry.vrf_id_;
    }

    return mac_ < entry.mac_;
}

bool RouteKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const RouteKSyncEntry &entry = static_cast<const RouteKSyncEntry &>(rhs);
    if (rt_type_ != entry.rt_type_)
        return rt_type_ < entry.rt_type_;

    //First unicast
    if ((rt_type_ == Agent::INET4_UNICAST) ||
        (rt_type_ == Agent::INET6_UNICAST)) {
        return UcIsLess(rhs);
    }

    if (rt_type_ == Agent::BRIDGE) {
        return L2IsLess(rhs);
    }

    return McIsLess(rhs);
}

static std::string RouteTypeToString(Agent::RouteTableType type) {
    switch (type) {
    case Agent::INET4_UNICAST:
        return "INET4_UNICAST";
        break;
    case Agent::INET6_UNICAST:
        return "INET6_UNICAST";
        break;
    case Agent::INET4_MULTICAST:
        return "INET_MULTICAST";
        break;
    case Agent::BRIDGE:
        return "BRIDGE";
        break;
    default:
        break;
    }

    assert(0);
    return "";
}

std::string RouteKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *nexthop;
    nexthop = nh();

    s << "Type : " << RouteTypeToString(rt_type_) << " ";
    const VrfEntry* vrf =
        ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
    if (vrf) {
        s << "Route Vrf : " << vrf->GetName() << " ";
    }
    s << address_string_ << "/" << prefix_len_ << " Type:" << rt_type_;


    s << " Label : " << label_;
    s << " Tunnel Type: " << tunnel_type_;

    if (nexthop) {
        s << nexthop->ToString();
    } else {
        s << " NextHop : <NULL>";
    }

    s << " Mac: " << mac_.ToString();
    s << " Flood DHCP:" << flood_dhcp_;
    return s.str();
}

// Check if NH points to a service-chain interface or a Gateway interface
static bool IsGatewayOrServiceInterface(const NextHop *nh) {
    if (nh->GetType() != NextHop::INTERFACE &&
        nh->GetType() != NextHop::VLAN && nh->GetType() != NextHop::ARP)
        return false;

    const Interface *intf = NULL;
    if (nh->GetType() == NextHop::INTERFACE) {
        intf = (static_cast<const InterfaceNH *>(nh))->GetInterface();
        if (intf->type() == Interface::PACKET)
            return true;
    } else if (nh->GetType() == NextHop::VLAN) {
        intf = (static_cast<const VlanNH *>(nh))->GetInterface();
    } else if (nh->GetType() == NextHop::ARP) {
        intf = (static_cast<const ArpNH *>(nh))->GetInterface();
    }

    const VmInterface *vmi = dynamic_cast<const VmInterface *>(intf);
    if (vmi == NULL)
        return false;

    if (vmi->HasServiceVlan())
        return true;
    if (vmi->device_type() == VmInterface::LOCAL_DEVICE)
        return true;

    return false;
}

// Set the flood_ and proxy_arp_ flag for the route
// flood_ flag says that ARP packets hitting route should be flooded
// proxy_arp_ flag says VRouter should do proxy ARP
//
// The flags are set based on NH and Interface-type
bool RouteKSyncEntry::BuildArpFlags(const DBEntry *e, const AgentPath *path,
                                    const MacAddress &mac) {
    bool ret = false;

    //Route flags for inet4 and inet6
    if ((rt_type_ != Agent::INET6_UNICAST) &&
        (rt_type_ != Agent::INET4_UNICAST))
        return false;

    Agent *agent = ksync_obj_->ksync()->agent();
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(e);

    // Assume no flood and proxy_arp by default
    bool flood = false;
    bool proxy_arp = false;
    const NextHop *nh = rt->GetActiveNextHop();

    switch (nh->GetType()) {
    case NextHop::RESOLVE:
        // RESOLVE NH can be used by Gateway Interface or Fabric VRF
        // VRouter does not honour flood_ and proxy_arp_ flag for Fabric VRF
        // We dont want to flood ARP on Gateway Interface
        if (rt->vrf()->GetName() != agent->fabric_vrf_name()) {
            proxy_arp = true;
        }
        break;

    case NextHop::COMPOSITE:
        // ECMP flows have composite NH. We want to do routing for ECMP flows
        // So, set proxy_arp flag
        proxy_arp = true;

        // There is an exception for ECMP routes
        // The subnet route configured on a VN can potentially be exported by
        // gateway route also. If gateway are redundant, then the subnet route
        // can be an ECMP route.
        if (rt->ipam_subnet_route())
        {
            proxy_arp = false;
            flood = true;
        }
        break;

    default:
        if (mac != MacAddress::ZeroMac()) {
            // Proxy-ARP without flood if mac-stitching is present
            proxy_arp = true;
            flood = false;
            break;
        }

        // MAC stitching not present.
        // If interface belongs to service-chain or Gateway, we want packet to
        // be routed. Following config ensures routing,
        //     - Enable Proxy
        //     - Disable Flood-bit
        //     - Dont do MAC Stitching (in RouteNeedsMacBinding)
        if (IsGatewayOrServiceInterface(nh) == true) {
            proxy_arp = true;
            flood = false;
            break;
        }

        AgentPath *local_vm_path = rt->FindLocalVmPortPath();
        if (local_vm_path != NULL) {
            if (local_vm_path->is_health_check_service()) {
                // for local vm path exported by health check service
                // set proxy arp to be true to arp with vrouter MAC
                proxy_arp = true;
                flood = false;
            } else {
                // Local port without MAC stitching should only be a transition
                // case. In the meanwhile, flood ARP so that VM can respond
                // Note: In case VN is in L3 forwarding mode flags will be reset
                // This is done below when VN entry is extracted.
                proxy_arp_ = false;
                flood = true;
            }
        } else {
            // Non local-route. Set flags based on the route
            proxy_arp = rt->proxy_arp();
            flood = rt->ipam_subnet_route();
        }
        break;
    }

    // If the route crosses a VN, we want packet to be routed. So, override
    // the flags set above and set only Proxy flag
    // When L2 forwarding mode is disabled, reset the proxy arp to true and flood
    // of arp to false.
    VnEntry *vn= rt->vrf()->vn();
    if (vn == NULL || !path->dest_vn_match(vn->GetName()) ||
        (path->dest_vn_list().size() > 1) ||
        (vn->bridging() == false)) {
        proxy_arp = true;
        flood = false;
    }

    //If VN is running in l2 mode, then any l3 route installed should
    //have flood flag set for ARP.
    if (vn && (vn->layer3_forwarding() == false)) {
        proxy_arp = false;
        flood = true;
    }

    // VRouter does not honour flood/proxy_arp flags for fabric-vrf
    if (rt->vrf()->GetName() == agent->fabric_vrf_name()) {
        proxy_arp = false;
        flood = false;
    }

    if (proxy_arp != proxy_arp_) {
        proxy_arp_ = proxy_arp;
        ret = true;
    }

    if (flood != flood_) {
        flood_ = flood;
        ret = true;
    }
    return ret;
}

//Uses internal API to extract path.
const NextHop *RouteKSyncEntry::GetActiveNextHop(const AgentRoute *route) const {
    const AgentPath *path = GetActivePath(route);
    if (path == NULL)
        return NULL;
    return path->ComputeNextHop(ksync_obj_->ksync()->agent());
}

//Returns the usable path.
//In case of bridge tables the path contains reference path which
//has all the data. This path known as evpn_path is a level of indirection
//to leaked path from MAC+IP route.
const AgentPath *RouteKSyncEntry::GetActivePath(const AgentRoute *route) const {
    const AgentPath *path = route->GetActivePath();
    return path;
}

bool RouteKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    Agent *agent = ksync_obj_->ksync()->agent();
    const AgentRoute *route = static_cast<AgentRoute *>(e);

    const AgentPath *path = GetActivePath(route);
    if (path->peer() == agent->local_vm_peer())
        local_vm_peer_route_ = true;
    else
        local_vm_peer_route_ = false;

    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    NHKSyncEntry *old_nh = nh();

    const NextHop *tmp = NULL;
    tmp = GetActiveNextHop(route);
    if (tmp == NULL) {
        DiscardNHKey key;
        tmp = static_cast<NextHop *>(agent->nexthop_table()->
             FindActiveEntry(&key));
    }
    NHKSyncEntry nexthop(nh_object, tmp);

    nh_ = static_cast<NHKSyncEntry *>(nh_object->GetReference(&nexthop));
    if (old_nh != nh()) {
        ret = true;
    }

    //Bother for label for unicast and bridge routes
    if (rt_type_ != Agent::INET4_MULTICAST) {
        uint32_t old_label = label_;

        if (route->is_multicast()) {
            label_ = path->vxlan_id();
        } else {
            label_ = path->GetActiveLabel();
        }
        if (label_ != old_label) {
            ret = true;
        }

        if (tunnel_type_ != path->GetTunnelType()) {
            tunnel_type_ = path->GetTunnelType();
            ret = true;
        }
    }

    if (wait_for_traffic_ != route->WaitForTraffic()) {
        wait_for_traffic_ =  route->WaitForTraffic();
        ret = true;
    }

    if (route->GetActivePath()) {
        if (layer2_control_word_ != route->GetActivePath()->layer2_control_word()) {
            layer2_control_word_ = route->GetActivePath()->layer2_control_word();
            ret = true;
        }
    }

    if (rt_type_ == Agent::INET4_UNICAST || rt_type_ == Agent::INET6_UNICAST) {
        VrfKSyncObject *obj = ksync_obj_->ksync()->vrf_ksync_obj();
        const InetUnicastRouteEntry *uc_rt =
            static_cast<const InetUnicastRouteEntry *>(e);
        MacAddress mac = MacAddress::ZeroMac();
        bool wait_for_traffic = false;

        if (obj->RouteNeedsMacBinding(uc_rt)) {
            mac = obj->GetIpMacBinding(uc_rt->vrf(), addr_);
            wait_for_traffic = obj->GetIpMacWaitForTraffic(uc_rt->vrf(), addr_);
        }

        if (wait_for_traffic_ == false &&
            wait_for_traffic_ != wait_for_traffic) {
            wait_for_traffic_ = wait_for_traffic;
            ret = true;
        }

        if (mac != mac_) {
            mac_ = mac;
            ret = true;
        }

        if (BuildArpFlags(e, path, mac_))
            ret = true;
    }

    if (rt_type_ == Agent::BRIDGE) {
        const BridgeRouteEntry *l2_rt =
            static_cast<const BridgeRouteEntry *>(route);

        //First search for v4
        const MacVmBindingPath *dhcp_path = l2_rt->FindMacVmBindingPath();
        bool flood_dhcp = true; // Flood DHCP if MacVmBindingPath is not present
        if (dhcp_path)
            flood_dhcp = dhcp_path->flood_dhcp();

        if (flood_dhcp_ != flood_dhcp) {
            flood_dhcp_ = flood_dhcp;
            ret = true;
        }
    }

    return ret;
}

void RouteKSyncEntry::FillObjectLog(sandesh_op::type type, 
                                    KSyncRouteInfo &info) const {
    if (type == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }

    info.set_addr(address_string_);
    info.set_plen(prefix_len_);
    info.set_vrf(vrf_id_);

    if (nh()) {
        info.set_nh_idx(nh()->nh_id());
        if (nh()->type() == NextHop::TUNNEL) {
            info.set_label(label_);
        }
    } else {
        info.set_nh_idx(NH_DISCARD_ID);
    }

    info.set_mac(mac_.ToString());
    info.set_type(RouteTypeToString(rt_type_));
}

int RouteKSyncEntry::Encode(sandesh_op::type op, uint8_t replace_plen,
                            char *buf, int buf_len) {
    vr_route_req encoder;
    int encode_len;
    NHKSyncEntry *nexthop = nh();

    encoder.set_h_op(op);
    encoder.set_rtr_rid(0);
    encoder.set_rtr_vrf_id(vrf_id_);
    if (rt_type_ != Agent::BRIDGE) {
        if (addr_.is_v4()) {
            encoder.set_rtr_family(AF_INET);
            boost::array<unsigned char, 4> bytes = addr_.to_v4().to_bytes();
            std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
            encoder.set_rtr_prefix(rtr_prefix);
        } else if (addr_.is_v6()) {
            encoder.set_rtr_family(AF_INET6);
            boost::array<unsigned char, 16> bytes = addr_.to_v6().to_bytes();
            std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
            encoder.set_rtr_prefix(rtr_prefix);
        }
        encoder.set_rtr_prefix_len(prefix_len_);
        if (mac_ != MacAddress::ZeroMac()) {
            if ((addr_.is_v4() && prefix_len_ != 32) ||
                (addr_.is_v6() && prefix_len_ != 128)) {
                LOG(ERROR, "Unexpected MAC stitching for route "
                    << ToString());
                mac_ = MacAddress::ZeroMac();
            }
            std::vector<int8_t> mac((int8_t *)mac_,
                                    (int8_t *)mac_ + mac_.size());
            encoder.set_rtr_mac(mac);
        }
    } else {
        encoder.set_rtr_family(AF_BRIDGE);
        //TODO add support for mac
        std::vector<int8_t> mac((int8_t *)mac_,
                                (int8_t *)mac_ + mac_.size());
        encoder.set_rtr_mac(mac);
    }

    int label = 0;
    int flags = 0;
    if (rt_type_ != Agent::INET4_MULTICAST) {
        if (nexthop != NULL && nexthop->type() == NextHop::TUNNEL) {
            label = label_;
            flags |= VR_RT_LABEL_VALID_FLAG;
        }
    }

    if (rt_type_ == Agent::BRIDGE) {
        label = label_;
        if (nexthop != NULL && ((nexthop->type() == NextHop::COMPOSITE) ||
                               (nexthop->type() == NextHop::TUNNEL))) {
            flags |= VR_BE_LABEL_VALID_FLAG;
        }
        if (flood_dhcp_)
            flags |= VR_BE_FLOOD_DHCP_FLAG;
    } else {

        if (proxy_arp_) {
            flags |= VR_RT_ARP_PROXY_FLAG;
        }

        if (wait_for_traffic_) {
            flags |= VR_RT_ARP_TRAP_FLAG;
        }

        if (flood_) {
            flags |= VR_RT_ARP_FLOOD_FLAG;
        }
    }

    if (layer2_control_word_) {
        flags |= VR_BE_L2_CONTROL_DATA_FLAG;
    }

    encoder.set_rtr_label_flags(flags);
    encoder.set_rtr_label(label);

    if (nexthop != NULL) {
        encoder.set_rtr_nh_id(nexthop->nh_id());
    } else {
        encoder.set_rtr_nh_id(NH_DISCARD_ID);
    }

    if (op == sandesh_op::DELETE) {
        encoder.set_rtr_replace_plen(replace_plen);
    }

    int error = 0;
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
}


int RouteKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Route, GetObject(), info);
    return Encode(sandesh_op::ADD, 0, buf, buf_len);
}

int RouteKSyncEntry::ChangeMsg(char *buf, int buf_len){
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Route, GetObject(), info);

    return Encode(sandesh_op::ADD, 0, buf, buf_len);
}

int RouteKSyncEntry::DeleteMsg(char *buf, int buf_len) {

    RouteKSyncEntry key(ksync_obj_, this, KSyncEntry::kInvalidIndex);
    KSyncEntry *found = NULL;
    RouteKSyncEntry *route = NULL;
    NHKSyncEntry *ksync_nh = NULL;

    // IF multicast or bridge delete unconditionally
    if ((rt_type_ == Agent::BRIDGE) ||
        (rt_type_ == Agent::INET4_MULTICAST)) {
        return DeleteInternal(nh(), NULL, buf, buf_len);
    }

    // For INET routes, we need to give replacement NH and prefixlen
    for (int plen = (prefix_len() - 1); plen >= 0; plen--) {

        if (addr_.is_v4()) {
            Ip4Address addr = Address::GetIp4SubnetAddress(addr_.to_v4(), plen);
            key.set_ip(addr);
        } else if (addr_.is_v6()) {
            Ip6Address addr = Address::GetIp6SubnetAddress(addr_.to_v6(), plen);
            key.set_ip(addr);
        }

        key.set_prefix_len(plen);
        found = GetObject()->Find(&key);
        
        if (found) {
            route = static_cast<RouteKSyncEntry *>(found);
            if (route->IsResolved()) {
                ksync_nh = route->nh();
                if(ksync_nh) {
                    return DeleteInternal(ksync_nh, route, buf, buf_len);
                }
                ksync_nh = NULL;
            }
        }
    }

    /* If better route is not found, send discardNH for route */
    return DeleteInternal(NULL, NULL, buf, buf_len);
}

uint8_t RouteKSyncEntry::CopyReplacementData(NHKSyncEntry *nexthop,
                                             RouteKSyncEntry *new_rt) {
    uint8_t new_plen = 0;
    nh_ = nexthop;
    if (new_rt == NULL) {
        label_ = 0;
        proxy_arp_ = false;
        flood_ = false;
        wait_for_traffic_ = false;
        // mac_ is key for bridge entries and modifying it will corrut the
        // KSync tree. So, reset mac in case of non-bridge entries only
        if (rt_type_ != Agent::BRIDGE) {
            mac_ = MacAddress::ZeroMac();
        }
    } else {
        label_ = new_rt->label();
        new_plen = new_rt->prefix_len();
        proxy_arp_ = new_rt->proxy_arp();
        flood_ = new_rt->flood();
        wait_for_traffic_ = new_rt->wait_for_traffic();
        mac_ = new_rt->mac();
    }
    return new_plen;
}

int RouteKSyncEntry::DeleteInternal(NHKSyncEntry *nexthop,
                                    RouteKSyncEntry *new_rt,
                                    char *buf, int buf_len) {
    uint8_t replace_plen = CopyReplacementData(nexthop, new_rt);
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Route, GetObject(), info);

    return Encode(sandesh_op::DELETE, replace_plen, buf, buf_len);
}

KSyncEntry *RouteKSyncEntry::UnresolvedReference() {
    NHKSyncEntry *nexthop = nh();
    if (!nexthop->IsResolved()) {
        return nexthop;
    }

    if ((rt_type_ == Agent::INET4_UNICAST) ||
        (rt_type_ == Agent::INET6_UNICAST)) {
        if (!mac_.IsZero()) {
            //Get Vrf and bridge ksync object
            VrfKSyncObject *vrf_obj = ksync_obj_->ksync()->vrf_ksync_obj();
            VrfEntry* vrf =
                ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
            if (vrf) {
                VrfKSyncObject::VrfState *state =
                    static_cast<VrfKSyncObject::VrfState *>
                    (vrf->GetState(vrf->get_table(),
                                   vrf_obj->vrf_listener_id()));
                RouteKSyncObject* bridge_ksync_obj = state->bridge_route_table_;
                BridgeRouteEntry tmp_l2_rt(vrf, mac_, Peer::EVPN_PEER, false);
                RouteKSyncEntry key(bridge_ksync_obj, &tmp_l2_rt);
                RouteKSyncEntry *mac_route_reference =
                    static_cast<RouteKSyncEntry *>(bridge_ksync_obj->
                                                   GetReference(&key));
                //Get the ksync entry for stitched mac
                //else mark dependancy on same.
                if (!mac_route_reference->IsResolved())
                    return mac_route_reference;
            } else {
                // clear the mac_ to avoid failure in programming vrouter
                mac_ = MacAddress::ZeroMac();
            }
        }
    }

    return NULL;
}

RouteKSyncObject::RouteKSyncObject(KSync *ksync, AgentRouteTable *rt_table):
    KSyncDBObject("KSync Route"), ksync_(ksync), marked_delete_(false),
    table_delete_ref_(this, rt_table->deleter()) {
    rt_table_ = rt_table;
    RegisterDb(rt_table);
}

RouteKSyncObject::~RouteKSyncObject() {
    UnregisterDb(GetDBTable());
    table_delete_ref_.Reset(NULL);
}

KSyncDBObject::DBFilterResp
RouteKSyncObject::DBEntryFilter(const DBEntry *entry,
                                const KSyncDBEntry *ksync) {
    const AgentRoute *route = static_cast<const AgentRoute *>(entry);
    // Ignore Add/Change notifications when VRF is deleted
    if (route->vrf()->IsDeleted() == true) {
        return DBFilterIgnore;
    }

    return DBFilterAccept;
}

KSyncEntry *RouteKSyncObject::Alloc(const KSyncEntry *entry, uint32_t index) {
    const RouteKSyncEntry *route = static_cast<const RouteKSyncEntry *>(entry);
    RouteKSyncEntry *ksync = new RouteKSyncEntry(this, route, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *RouteKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const AgentRoute *route = static_cast<const AgentRoute *>(e);
    RouteKSyncEntry *key = new RouteKSyncEntry(this, route);
    return static_cast<KSyncEntry *>(key);
}

void RouteKSyncObject::Unregister() {
    if (IsEmpty() == true && marked_delete_ == true) {
        KSYNC_TRACE(Trace, this, "Destroying ksync object: "\
                    + rt_table_->name());
        KSyncObjectManager::Unregister(this);
    }
}

void RouteKSyncObject::ManagedDelete() {
    marked_delete_ = true;
    Unregister();
}

void RouteKSyncObject::EmptyTable() {
    if (marked_delete_ == true) {
        Unregister();
    }
}

VrfKSyncObject::VrfState::VrfState(Agent *agent) :
    DBState(), seen_(false),
    evpn_rt_table_listener_id_(DBTableBase::kInvalidId) {
    ksync_route_walker_ = new KSyncRouteWalker(agent, this);
}

void VrfKSyncObject::VrfNotify(DBTablePartBase *partition, DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(partition->parent(), vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (state) {
            UnRegisterEvpnRouteTableListener(vrf, state);
            vrf->ClearState(partition->parent(), vrf_listener_id_);
            state->ksync_route_walker_->Delete();
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state = new VrfState(ksync_->agent());
        state->seen_ = true;
        vrf->SetState(partition->parent(), vrf_listener_id_, state);

        // Get Inet4 Route table and register with KSync
        AgentRouteTable *rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4UnicastRouteTable());
        state->inet4_uc_route_table_ = new RouteKSyncObject(ksync_, rt_table);

        // Get Inet6 Route table and register with KSync
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet6UnicastRouteTable());
        state->inet6_uc_route_table_ = new RouteKSyncObject(ksync_, rt_table);

        // Get Layer 2 Route table and register with KSync
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetBridgeRouteTable());
        state->bridge_route_table_ = new RouteKSyncObject(ksync_, rt_table);

        //Now for multicast table. Ksync object for multicast table is 
        //not maintained in vrf list
        //TODO Enhance ksyncobject for UC/MC, currently there is only one entry
        //in MC so just use the UC object for time being.
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4MulticastRouteTable());
        state->inet4_mc_route_table_ = new RouteKSyncObject(ksync_, rt_table);

        //Add EVPN route table listener to update IP MAC binding table.
        rt_table = static_cast<AgentRouteTable *>(vrf->
                                                  GetEvpnRouteTable());
        KSYNC_TRACE(Trace, state->inet4_uc_route_table_,
                    "Subscribing to route table "\
                    + vrf->GetName());
        if (state->evpn_rt_table_listener_id_ == DBTableBase::kInvalidId) {
            state->evpn_rt_table_listener_id_ =
                rt_table->Register(boost::bind(&VrfKSyncObject::EvpnRouteTableNotify,
                                           this, _1, _2));
        }
        state->ksync_route_walker_->NotifyRoutes(vrf);
    }
}

void VrfKSyncObject::EvpnRouteTableNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt = static_cast<const EvpnRouteEntry *>(e);
    if (evpn_rt->IsDeleted()) {
        DelIpMacBinding(evpn_rt->vrf(), evpn_rt->ip_addr(),
                        evpn_rt->mac());
    } else {
        AddIpMacBinding(evpn_rt->vrf(), evpn_rt->ip_addr(),
                        evpn_rt->mac(),
                        evpn_rt->GetActivePath()->path_preference().preference(),
                        evpn_rt->WaitForTraffic());
    }
    return;
}

void VrfKSyncObject::UnRegisterEvpnRouteTableListener(const VrfEntry *vrf,
                                                      VrfState *state) {
    if (state->evpn_rt_table_listener_id_ == DBTableBase::kInvalidId)
        return;

    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>(vrf->
                                            GetEvpnRouteTable());
    rt_table->Unregister(state->evpn_rt_table_listener_id_);
    state->evpn_rt_table_listener_id_ = DBTableBase::kInvalidId;
}

VrfKSyncObject::VrfKSyncObject(KSync *ksync) : ksync_(ksync) {
}

VrfKSyncObject::~VrfKSyncObject() {
}

void VrfKSyncObject::RegisterDBClients() {
    vrf_listener_id_ = ksync_->agent()->vrf_table()->Register
            (boost::bind(&VrfKSyncObject::VrfNotify, this, _1, _2));
}

void VrfKSyncObject::Shutdown() {
    ksync_->agent()->vrf_table()->Unregister(vrf_listener_id_);
    vrf_listener_id_ = -1;
}

void vr_route_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->RouteMsgHandler(this);
}

/****************************************************************************
 * Methods to stitch IP and MAC addresses. The KSync object maintains mapping
 * between IP <-> MAC in ip_mac_binding_ tree. The table is built based on
 * Evpn routes.
 *
 * When an Inet route is notified, if it needs MAC Stitching, the MAC to 
 * stitch is found from the ip_mac_binding_ tree
 *
 * Any change to ip_mac_binding_ tree will also result in re-evaluation of
 * Inet4/Inet6 route that may potentially have stitching changed
 ****************************************************************************/

// Compute if a route needs IP-MAC binding. A route needs IP-MAC binding if,
// 1. The NH points to interface or Tunnel-NH
// 2. NH does not belong to Service-Chain Interface
// 3. NH does not belong to Gateway Interface
// to interface or tunnel-nh
bool VrfKSyncObject::RouteNeedsMacBinding(const InetUnicastRouteEntry *rt) {
    if (rt->addr().is_v4() && rt->plen() != 32)
        return false;

    if (rt->addr().is_v6() && rt->plen() != 128)
        return false;

    //Check if VN is enabled for bridging, if not then skip mac binding.
    VnEntry *vn= rt->vrf()->vn();
    if (vn == NULL || (vn->bridging() == false))
        return false;

    const NextHop *nh = rt->GetActiveNextHop();
    if (nh == NULL)
        return false;

    if (nh->GetType() != NextHop::INTERFACE &&
        nh->GetType() != NextHop::TUNNEL &&
        nh->GetType() != NextHop::VLAN)
        return false;

    if (IsGatewayOrServiceInterface(nh) == true)
        return false;

    //Is this a IPAM gateway? It may happen that better path is present pointing
    //to tunnel. In this case IPAM gateway path will not be active path and
    //identifying same will be missed. Stitching has to be avoided on non-TSN
    //node.
    //This has to be done only when layer3 forwarding is enabled on VN,
    //else mac binding should be done irrespective of IPAM gateway.
    if (vn->layer3_forwarding()) {
        const Agent *agent = ksync()->agent();
        if (agent->tsn_enabled() == false) {
            const AgentPath *path = rt->FindPath(agent->local_peer());
            nh = path ? path->nexthop() : NULL;
        }
        if (nh && (IsGatewayOrServiceInterface(nh) == true))
            return false;
    }

    return true;
}

// Notify change to KSync entry of InetUnicast Route
void VrfKSyncObject::NotifyUcRoute(VrfEntry *vrf, VrfState *state,
                                   const IpAddress &ip) {
    InetUnicastAgentRouteTable *table = NULL;
    if (ip.is_v4()) {
        table = vrf->GetInet4UnicastRouteTable();
    } else {
        table = vrf->GetInet6UnicastRouteTable();
    }

    InetUnicastRouteEntry *rt = table->FindLPM(ip);
    if (rt == NULL || rt->IsDeleted())
        return;

    if (rt->GetTableType() == Agent::INET4_UNICAST) {
        state->inet4_uc_route_table_->Notify(rt->get_table_partition(), rt);
    } else if (rt->GetTableType() == Agent::INET6_UNICAST) {
        state->inet6_uc_route_table_->Notify(rt->get_table_partition(), rt);
    }
}

void VrfKSyncObject::AddIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                                     const MacAddress &mac,
                                     const PathPreference::Preference &pref,
                                     bool wait_for_traffic) {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL)
        return;

    IpToMacBinding::iterator it = state->ip_mac_binding_.find(ip);

    PathPreference path_pref(0, pref, wait_for_traffic, false);
    if (it == state->ip_mac_binding_.end()) {
        MacBinding mac_binding(mac, path_pref);
        state->ip_mac_binding_.insert(
                std::pair<IpAddress, MacBinding>(ip, mac_binding));
    } else {
        it->second.set_mac(path_pref, mac);
    }

    NotifyUcRoute(vrf, state, ip);
}

void VrfKSyncObject::DelIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                                     const MacAddress &mac) {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL)
        return;

    IpToMacBinding::iterator it = state->ip_mac_binding_.find(ip);
    if (it != state->ip_mac_binding_.end()) {
        it->second.reset_mac(mac);
        if (it->second.can_erase()) {
            state->ip_mac_binding_.erase(ip);
        }
    }
    NotifyUcRoute(vrf, state, ip);
}

bool VrfKSyncObject::GetIpMacWaitForTraffic(VrfEntry *vrf,
                                            const IpAddress &ip) const {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL) {
        return false;
    }

    IpToMacBinding::const_iterator it = state->ip_mac_binding_.find(ip);
    if (it == state->ip_mac_binding_.end()) {
        return false;
    }

    return  it->second.WaitForTraffic();
}

MacAddress VrfKSyncObject::GetIpMacBinding(VrfEntry *vrf,
                                           const IpAddress &ip) const {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL)
        return MacAddress::ZeroMac();

    IpToMacBinding::const_iterator it = state->ip_mac_binding_.find(ip);
    if (it == state->ip_mac_binding_.end())
        return MacAddress::ZeroMac();

    return it->second.get_mac();
}

KSyncRouteWalker::KSyncRouteWalker(Agent *agent, VrfKSyncObject::VrfState *state) :
    AgentRouteWalker(agent, AgentRouteWalker::ALL), state_(state) {
}

KSyncRouteWalker::~KSyncRouteWalker() {
}

bool IsStatePresent(AgentRoute *route, DBTableBase::ListenerId id,
                    DBTablePartBase *partition) {
    DBState *state = route->GetState(partition->parent(), id);
    return (state != NULL);
}

bool KSyncRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);

    switch (route->GetTableType()) {
    case Agent::INET4_UNICAST: {
        if (IsStatePresent(route, state_->inet4_uc_route_table_->id(),
                           partition) == false) {
            state_->inet4_uc_route_table_->Notify(partition, route);
        }
        break;
    }
    case Agent::INET6_UNICAST: {
        if (IsStatePresent(route, state_->inet6_uc_route_table_->id(),
                           partition) == false) {
            state_->inet6_uc_route_table_->Notify(partition, route);
        }
        break;
    }
    case Agent::INET4_MULTICAST: {
        if (IsStatePresent(route, state_->inet4_mc_route_table_->id(),
                           partition) == false) {
            state_->inet4_mc_route_table_->Notify(partition, route);
        }
        break;
    }
    case Agent::BRIDGE: {
        if (IsStatePresent(route, state_->bridge_route_table_->id(),
                           partition) == false) {
            state_->bridge_route_table_->Notify(partition, route);
        }
        break;
    }
    default: {
        break;
    }
    }
    return true;
}

void KSyncRouteWalker::NotifyRoutes(VrfEntry *vrf) {
    StartRouteWalk(vrf);
}
