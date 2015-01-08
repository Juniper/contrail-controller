/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

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
    proxy_arp_(false), address_string_(entry->address_string_),
    tunnel_type_(entry->tunnel_type_),
    wait_for_traffic_(entry->wait_for_traffic_),
    evpn_ip_(entry->evpn_ip_),
    local_vm_peer_route_(entry->local_vm_peer_route_),
    flood_(entry->flood_) {
}

RouteKSyncEntry::RouteKSyncEntry(RouteKSyncObject* obj, const AgentRoute *rt) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    vrf_id_(rt->vrf_id()), nh_(NULL), label_(0), proxy_arp_(false),
    tunnel_type_(TunnelType::DefaultType()), wait_for_traffic_(false),
    evpn_ip_(),
    local_vm_peer_route_(false),
    flood_(false) {
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
    case Agent::LAYER2: {
          const Layer2RouteEntry *l2_rt =
              static_cast<const Layer2RouteEntry *>(rt);              
          mac_ = l2_rt->GetAddress();
          addr_ = l2_rt->ip_addr();
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

KSyncDBObject *RouteKSyncEntry::GetObject() {
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

    if (mac_ != entry.mac_) {
        return mac_ < entry.mac_;
    }

    return (addr_ < entry.addr_);
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

    if (rt_type_ == Agent::LAYER2) {
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
    case Agent::LAYER2:
        return "EVPN";
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

    return s.str();
}

bool RouteKSyncEntry::BuildRouteFlags(const DBEntry *e,
                                      const MacAddress &mac) {
    bool ret = false;

    //Route flags for inet4 and inet6
    if ((rt_type_ != Agent::INET6_UNICAST) &&
        (rt_type_ != Agent::INET4_UNICAST))
        return false;

    Agent *agent = ksync_obj_->ksync()->agent();
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(e);

    //resolve NH handling i.e. gateway
    if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) { 
        if (rt->vrf()->GetName() != agent->fabric_vrf_name()) {
            if (proxy_arp_ == false) {
                proxy_arp_ = true;
                ret = true;
            }

            if (flood_ == true) {
                flood_ = false;
                ret = true;
            }
        } else {
            if (proxy_arp_ == true) {
                proxy_arp_ = false;
                ret = true;
            }

            if (flood_ == true) {
                flood_ = false;
                ret = true;
            }
        }

        return ret;
    }

    //Rest of the v4 cases
    bool is_binding_available = (MacAddress::ZeroMac() != mac);
    if (is_binding_available) {
        if (proxy_arp_ != true) {
            proxy_arp_ = true;
            ret = true;
        }
        if (flood_ != false) {
            flood_ = false;
            ret = true;
        }
    } else {
        if (rt->FindLocalVmPortPath()) {
            if (proxy_arp_ != false) {
                proxy_arp_ = false;
                ret = true;
            }
            if (flood_ != true) {
                flood_ = true;
                ret = true;
            }
        } else {
            if (proxy_arp_ != rt->proxy_arp()) {
                proxy_arp_ = rt->proxy_arp();
                ret = true;
            }

            if (flood_ != rt->ipam_subnet_route()) {
                flood_ = rt->ipam_subnet_route();
                ret = true;
            }
        }
    }

    return ret;
 }

bool RouteKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    Agent *agent = ksync_obj_->ksync()->agent();
    const AgentRoute *route = static_cast<AgentRoute *>(e);

    const AgentPath *path = route->GetActivePath();
    if (path->peer() == agent->local_vm_peer())
        local_vm_peer_route_ = true;
    else
        local_vm_peer_route_ = false;

    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    NHKSyncEntry *old_nh = nh();

    const NextHop *tmp = NULL;
    tmp = route->GetActiveNextHop();
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

    //Bother for label for unicast and EVPN routes
    if (rt_type_ != Agent::INET4_MULTICAST) {
        uint32_t old_label = label_;
        const AgentPath *path = 
            (static_cast <InetUnicastRouteEntry *>(e))->GetActivePath();
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

    if (rt_type_ == Agent::INET4_UNICAST || rt_type_ == Agent::INET6_UNICAST) {
        VrfKSyncObject *obj = ksync_obj_->ksync()->vrf_ksync_obj();
        const InetUnicastRouteEntry *uc_rt =
            static_cast<const InetUnicastRouteEntry *>(e);
        MacAddress mac;
        if (obj->RouteNeedsMacBinding(uc_rt)) {
            mac = obj->GetIpMacBinding(uc_rt->vrf(), addr_);
        }

        if (mac != mac_) {
            mac_ = mac;
            ret = true;
        }
    }

    if (BuildRouteFlags(e, mac_))
        ret = true;

    if (rt_type_ == Agent::LAYER2) {
        const Layer2RouteEntry *l2_rt =
            static_cast<const Layer2RouteEntry *>(e);
        if (evpn_ip_ != l2_rt->ip_addr()) {
            VrfKSyncObject *obj = ksync_obj_->ksync()->vrf_ksync_obj();
            if (evpn_ip_.is_unspecified() == false) {
                obj->DelIpMacBinding(l2_rt->vrf(), evpn_ip_, mac_);
            }

            evpn_ip_ = l2_rt->ip_addr();
            if (evpn_ip_.is_unspecified() == false) {
                obj->AddIpMacBinding(l2_rt->vrf(), evpn_ip_, mac_);
            }
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
    int encode_len, error;
    NHKSyncEntry *nexthop = nh();

    encoder.set_h_op(op);
    encoder.set_rtr_rid(0);
    encoder.set_rtr_vrf_id(vrf_id_);
    if (rt_type_ != Agent::LAYER2) {
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

    if (rt_type_ == Agent::LAYER2) {
        flags |= 0x02;
        label = label_;
        if (nexthop != NULL && nexthop->type() == NextHop::COMPOSITE) {
            flags |= VR_RT_LABEL_VALID_FLAG;
        }
    }

    if (proxy_arp_) {
        flags |= VR_RT_ARP_PROXY_FLAG;
    }

    if (wait_for_traffic_) {
        flags |= VR_RT_ARP_TRAP_FLAG;
    }

    if (flood_) {
        flags |= VR_RT_ARP_FLOOD_FLAG;
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

    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}


int RouteKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Route, info);
    return Encode(sandesh_op::ADD, 0, buf, buf_len);
}

int RouteKSyncEntry::ChangeMsg(char *buf, int buf_len){
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Route, info);

    return Encode(sandesh_op::ADD, 0, buf, buf_len);
}

int RouteKSyncEntry::DeleteMsg(char *buf, int buf_len) {

    RouteKSyncEntry key(ksync_obj_, this, KSyncEntry::kInvalidIndex);
    KSyncEntry *found = NULL;
    RouteKSyncEntry *route = NULL;
    NHKSyncEntry *ksync_nh = NULL;

    // IF multicast or EVPN delete unconditionally
    if ((rt_type_ == Agent::LAYER2) ||
        (rt_type_ == Agent::INET4_MULTICAST)) {
        return DeleteInternal(nh(), 0, 0, false, buf, buf_len);
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
                if(ksync_nh && ksync_nh->IsResolved()) {
                    return DeleteInternal(ksync_nh, route->label(),
                                          route->prefix_len(),
                                          route->proxy_arp(), buf, buf_len);
                }
                ksync_nh = NULL;
            }
        }
    }

    /* If better route is not found, send discardNH for route */
    DiscardNHKey nh_oper_key;
    NextHop *nexthop = static_cast<NextHop *>
        (ksync_obj_->ksync()->agent()->nexthop_table()->
                     FindActiveEntry(&nh_oper_key));
    if (nexthop != NULL) {
        NHKSyncObject *ksync_nh_object = 
            ksync_obj_->ksync()->nh_ksync_obj();
        NHKSyncEntry nh_key(ksync_nh_object, nexthop);
        ksync_nh = static_cast<NHKSyncEntry *>(ksync_nh_object->Find(&nh_key));
    }

    return DeleteInternal(ksync_nh, 0, 0, false, buf, buf_len);
}


int RouteKSyncEntry::DeleteInternal(NHKSyncEntry *nexthop, uint32_t lbl,
                                    uint8_t replace_plen, bool proxy_arp,
                                    char *buf, int buf_len) {
    nh_ = nexthop;
    label_ = lbl;
    proxy_arp_ = proxy_arp;

    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Route, info);

    return Encode(sandesh_op::DELETE, replace_plen, buf, buf_len);
}

KSyncEntry *RouteKSyncEntry::UnresolvedReference() {
    if (rt_type_ == Agent::LAYER2) {
        if (addr_.is_v6()) {
            return KSyncObjectManager::default_defer_entry();
        }

        if ((local_vm_peer_route_ == false) &&
            (mac_ != MacAddress::BroadcastMac() && addr_.is_unspecified())) {
            return KSyncObjectManager::default_defer_entry();
        }
    }

    NHKSyncEntry *nexthop = nh();
    if (!nexthop->IsResolved()) {
        return nexthop;
    }
    return NULL;
}

RouteKSyncObject::RouteKSyncObject(KSync *ksync, AgentRouteTable *rt_table):
    KSyncDBObject(), ksync_(ksync), marked_delete_(false), 
    table_delete_ref_(this, rt_table->deleter()) {
    rt_table_ = rt_table;
    RegisterDb(rt_table);
}

RouteKSyncObject::~RouteKSyncObject() {
    UnregisterDb(GetDBTable());
    table_delete_ref_.Reset(NULL);
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
        KSYNC_TRACE(Trace, "Destroying ksync object: " + rt_table_->name());
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

void VrfKSyncObject::VrfNotify(DBTablePartBase *partition, DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(partition->parent(), vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (state) {
            vrf->ClearState(partition->parent(), vrf_listener_id_);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        KSYNC_TRACE(Trace, "Subscribing to route table " + vrf->GetName());
        state = new VrfState();
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
                          GetLayer2RouteTable());
        state->layer2_route_table_ = new RouteKSyncObject(ksync_, rt_table);

        //Now for multicast table. Ksync object for multicast table is 
        //not maintained in vrf list
        //TODO Enhance ksyncobject for UC/MC, currently there is only one entry
        //in MC so just use the UC object for time being.
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4MulticastRouteTable());
        state->inet4_mc_route_table_ = new RouteKSyncObject(ksync_, rt_table);
    }
}

VrfKSyncObject::VrfKSyncObject(KSync *ksync) 
    : ksync_(ksync) {
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
 * Layer2 routes.
 *
 * When an Inet route is notified, if it needs MAC Stitching, the MAC to 
 * stitch is found from the ip_mac_binding_ tree
 *
 * Any change to ip_mac_binding_ tree will also result in re-evaluation of
 * Inet4/Inet6 route that may potentially have stitching changed
 ****************************************************************************/

// A route potentially needs IP-MAC binding if its a host route and points
// to interface or tunnel-nh
bool VrfKSyncObject::RouteNeedsMacBinding(const InetUnicastRouteEntry *rt) {
    if (rt->addr().is_v4() && rt->plen() != 32)
        return false;

    if (rt->addr().is_v6() && rt->plen() != 128)
        return false;

    const NextHop *nh = rt->GetActiveNextHop();
    if (nh == NULL)
        return false;

    if (nh->GetType() != NextHop::INTERFACE &&
        nh->GetType() != NextHop::TUNNEL)
        return false;

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
    if (rt == NULL || rt->IsDeleted() == false)
        return;

    if (rt->GetTableType() == Agent::INET4_UNICAST) {
        state->inet4_uc_route_table_->Notify(rt->get_table_partition(), rt);
    } else if (rt->GetTableType() == Agent::INET6_UNICAST) {
        state->inet6_uc_route_table_->Notify(rt->get_table_partition(), rt);
    }
}

void VrfKSyncObject::AddIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                                     const MacAddress &mac) {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL)
        return;

    state->ip_mac_binding_[ip] = mac;
    NotifyUcRoute(vrf, state, ip);
}

void VrfKSyncObject::DelIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                                     const MacAddress &mac) {
    VrfState *state = static_cast<VrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (state == NULL)
        return;

    state->ip_mac_binding_.erase(ip);
    NotifyUcRoute(vrf, state, ip);
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

    return it->second;
}
