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

#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/mirror_table.h"

#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/route_ksync.h"

#include "ksync_init.h"
#include "vr_types.h"
#include "vr_defs.h"
#include "vr_nexthop.h"

RouteKSyncEntry::RouteKSyncEntry(RouteKSyncObject* obj, 
                                 const RouteKSyncEntry *entry, 
                                 uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), 
    rt_type_(entry->rt_type_), vrf_id_(entry->vrf_id_), 
    addr_(entry->addr_), src_addr_(entry->src_addr_), mac_(entry->mac_), 
    prefix_len_(entry->prefix_len_), nh_(entry->nh_), label_(entry->label_), 
    proxy_arp_(false), address_string_(entry->address_string_),
    tunnel_type_(entry->tunnel_type_),
    wait_for_traffic_(entry->wait_for_traffic_) {
}

RouteKSyncEntry::RouteKSyncEntry(RouteKSyncObject* obj, const AgentRoute *rt) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    vrf_id_(rt->vrf_id()), nh_(NULL), label_(0), proxy_arp_(false),
    tunnel_type_(TunnelType::DefaultType()), wait_for_traffic_(false) {
    boost::system::error_code ec;
    switch (rt->GetTableType()) {
    case Agent::INET4_UNICAST: {
          const InetUnicastRouteEntry *uc_rt =
              static_cast<const InetUnicastRouteEntry *>(rt);
          addr_ = uc_rt->addr();
          src_addr_ = IpAddress::from_string("0.0.0.0", ec).to_v4();
          prefix_len_ = uc_rt->plen();
          rt_type_ = RT_UCAST;
          break;
    }
    case Agent::INET6_UNICAST: {
          const InetUnicastRouteEntry *uc_rt =
              static_cast<const InetUnicastRouteEntry *>(rt);
          addr_ = uc_rt->addr();
          src_addr_ = Ip6Address();
          prefix_len_ = uc_rt->plen();
          rt_type_ = RT_UCAST;
          break;
    }
    case Agent::INET4_MULTICAST: {
          const Inet4MulticastRouteEntry *mc_rt = 
              static_cast<const Inet4MulticastRouteEntry *>(rt);
          addr_ = mc_rt->dest_ip_addr();
          src_addr_ = mc_rt->src_ip_addr();
          prefix_len_ = 32;
          rt_type_ = RT_MCAST;
          break;
    }
    case Agent::LAYER2: {
          const Layer2RouteEntry *l2_rt =
              static_cast<const Layer2RouteEntry *>(rt);              
          mac_ = l2_rt->GetAddress();
          rt_type_ = RT_LAYER2;
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
    /*return static_cast<KSyncDBObject*>
        (VrfKSyncObject::GetKSyncObject()->GetRouteKSyncObject(vrf_id_,
                                                               rt_type_));*/
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
    LOG(DEBUG, "MCastompare " << ToString() << "\n"
               << rhs.ToString() << "Verdict: " << (addr_ < entry.addr_)); 
 
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

    return (mac_.CompareTo(entry.mac_) < 0);
}

bool RouteKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const RouteKSyncEntry &entry = static_cast<const RouteKSyncEntry &>(rhs);
    if (rt_type_ != entry.rt_type_)
        return rt_type_ < entry.rt_type_;

    //First unicast
    if (rt_type_ == RT_UCAST) {
        return UcIsLess(rhs);
    }

    if (rt_type_ == RT_LAYER2) {
        return L2IsLess(rhs);
    }

    return McIsLess(rhs);
}

std::string RouteKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *nexthop;
    nexthop = nh();

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

bool RouteKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const AgentRoute *route;
  
    route = static_cast<AgentRoute *>(e);
    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    NHKSyncEntry *old_nh = nh();

    Agent *agent = ksync_obj_->ksync()->agent();
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
    if (rt_type_ == RT_UCAST || rt_type_ == RT_LAYER2) {
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

        proxy_arp_ = path->proxy_arp();
    }

    if (wait_for_traffic_ != route->WaitForTraffic()) {
        wait_for_traffic_ =  route->WaitForTraffic();
        ret = true;
    }
    return ret;
};

void RouteKSyncEntry::FillObjectLog(sandesh_op::type type, 
                                    KSyncRouteInfo &info) const {
    info.set_addr(address_string_);
    info.set_vrf(vrf_id_);

    if (type == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }

    if (nh()) {
        info.set_nh_idx(nh()->nh_id());
        if (nh()->type() == NextHop::TUNNEL) {
            info.set_label(label_);
        }
    } else {
        info.set_nh_idx(NH_DISCARD_ID);
    }
}

int RouteKSyncEntry::Encode(sandesh_op::type op, uint8_t replace_plen,
                            char *buf, int buf_len) {
    vr_route_req encoder;
    int encode_len, error;
    NHKSyncEntry *nexthop = nh();

    encoder.set_h_op(op);
    encoder.set_rtr_rid(0);
    encoder.set_rtr_rt_type(rt_type_);
    encoder.set_rtr_vrf_id(vrf_id_);
    if (rt_type_ != RT_LAYER2) {
        if (addr_.is_v4()) {
            encoder.set_rtr_family(AF_INET);
            boost::array<unsigned char, 4> bytes = addr_.to_v4().to_bytes();
            std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
            encoder.set_rtr_prefix(rtr_prefix);
            boost::array<unsigned char, 4> src_bytes = src_addr_.to_v4().to_bytes();
            std::vector<int8_t> rtr_src(src_bytes.begin(), src_bytes.end());
            encoder.set_rtr_src(rtr_src);
        } else if (addr_.is_v6()) {
            encoder.set_rtr_family(AF_INET6);
            boost::array<unsigned char, 16> bytes = addr_.to_v6().to_bytes();
            std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
            encoder.set_rtr_prefix(rtr_prefix);
            boost::array<unsigned char, 16> src_bytes = src_addr_.to_v6().to_bytes();
            std::vector<int8_t> rtr_src(src_bytes.begin(), src_bytes.end());
            encoder.set_rtr_src(rtr_src);
        }
        encoder.set_rtr_prefix_len(prefix_len_);
    } else {
        encoder.set_rtr_family(AF_BRIDGE);
        //TODO add support for mac
        std::vector<int8_t> mac((int8_t *)mac_,
                                (int8_t *)mac_ + mac_.size());
        encoder.set_rtr_mac(mac);
    }

    int label = 0;
    int flags = 0;
    if (rt_type_ == RT_UCAST || rt_type_ == RT_LAYER2) {
        if (nexthop != NULL && nexthop->type() == NextHop::TUNNEL) {
            label = label_;
            flags |= VR_RT_LABEL_VALID_FLAG;
        }
    }

    if (rt_type_ == RT_LAYER2) {
        flags |= 0x02;
        label = label_;
        if (nexthop != NULL && nexthop->type() == NextHop::COMPOSITE) {
            flags |= VR_RT_LABEL_VALID_FLAG;
        }
    }

    if (proxy_arp_) {
        flags |= VR_RT_HOSTED_FLAG;
    }
    if (wait_for_traffic_) {
        flags |= VR_RT_ARP_TRAP_FLAG;
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
    if (rt_type_ == RT_MCAST || rt_type_ == RT_LAYER2) {
        return DeleteInternal(nh(), 0, 0, false, buf, buf_len);
    }

    // For INET routes, we need to give replacement NH and prefixlen
    for (int plen = (prefix_len() - 1); plen >= 0; plen--) {

        if (addr_.is_v4()) {
            uint32_t mask = plen ? (0xFFFFFFFF << (32 - plen)) : 0;
            Ip4Address v4 = addr_.to_v4();
            Ip4Address addr = boost::asio::ip::address_v4(v4.to_ulong() & mask);
            key.set_ip(addr);
        } else if (addr_.is_v6()) {
            Ip6Address addr = GetIp6SubnetAddress(addr_.to_v6(), plen);
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
        ksync_->vrf_ksync_obj()->DelFromVrfMap(this);
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

RouteKSyncObject *VrfKSyncObject::GetRouteKSyncObject(uint32_t vrf_id,
                                                      uint32_t table) const {
    VrfRtObjectMap::const_iterator it;

    switch (table) {
      case RT_UCAST: {
          it = vrf_ucrt_object_map_.find(vrf_id);
          if (it != vrf_ucrt_object_map_.end()) {
              return it->second;
          }          
          break;    
      }
      case RT_MCAST: {
          it = vrf_mcrt_object_map_.find(vrf_id);
          if (it != vrf_mcrt_object_map_.end()) {
              return it->second;
          }          
          break;
      }
      case RT_LAYER2: {
          it = vrf_l2rt_object_map_.find(vrf_id);
          if (it != vrf_l2rt_object_map_.end()) {
              return it->second;
          }          
          break;
      }
      default: {
          assert(0);
      }
    }
    return NULL;
}

void VrfKSyncObject::AddToVrfMap(uint32_t vrf_id, RouteKSyncObject *rt,
                                 unsigned int table) {
    if (table == RT_UCAST) {
        vrf_ucrt_object_map_.insert(make_pair(vrf_id, rt));
    } else if (table == RT_MCAST) {
        vrf_mcrt_object_map_.insert(make_pair(vrf_id, rt));
    } else if (table == RT_LAYER2) {
        vrf_l2rt_object_map_.insert(make_pair(vrf_id, rt));
    }
}

void VrfKSyncObject::DelFromVrfMap(RouteKSyncObject *rt) {
    VrfRtObjectMap::iterator it;
    for (it = vrf_ucrt_object_map_.begin(); it != vrf_ucrt_object_map_.end(); 
        ++it) {
        if (it->second == rt) {
            vrf_ucrt_object_map_.erase(it);
            return;
        }
    }

    for (it = vrf_mcrt_object_map_.begin(); it != vrf_mcrt_object_map_.end(); 
        ++it) {
        if (it->second == rt) {
            vrf_mcrt_object_map_.erase(it);
            return;
        }
    }

    for (it = vrf_l2rt_object_map_.begin(); it != vrf_l2rt_object_map_.end(); 
        ++it) {
        if (it->second == rt) {
            vrf_l2rt_object_map_.erase(it);
            return;
        }
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

        // Register route-table with KSync
        RouteKSyncObject *ksync = new RouteKSyncObject(ksync_, rt_table);
        AddToVrfMap(vrf->vrf_id(), ksync, RT_UCAST);

        // Get Inet6 Route table and register with KSync
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet6UnicastRouteTable());

        // Register route-table with KSync
        ksync = new RouteKSyncObject(ksync_, rt_table);
        //AddToVrfMap(vrf->vrf_id(), ksync, RT_UCAST);

        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetLayer2RouteTable());

        ksync = new RouteKSyncObject(ksync_, rt_table);
        AddToVrfMap(vrf->vrf_id(), ksync, RT_LAYER2);

        //Now for multicast table. Ksync object for multicast table is 
        //not maintained in vrf list
        //TODO Enhance ksyncobject for UC/MC, currently there is only one entry
        //in MC so just use the UC object for time being.
        rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4MulticastRouteTable());
        ksync = new RouteKSyncObject(ksync_, rt_table);
        AddToVrfMap(vrf->vrf_id(), ksync, RT_MCAST);
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

