/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>

#include <vnc_cfg_types.h>
#include <bgp_schema_types.h>
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/multicast.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/layer2_route.h>

#include "ksync_vxlan.h"
#include "ksync_vxlan_bridge.h"
#include "ksync_vxlan_port.h"
#include "ksync_vxlan_route.h"

/**************************************************************************
 **************************************************************************/
KSyncVxlanRouteEntry::KSyncVxlanRouteEntry(KSyncVxlanRouteObject *obj,
                                           const KSyncVxlanRouteEntry *entry) :
    KSyncDBEntry(), vrf_id_(entry->vrf_id()), ksync_obj_(obj) {
}

KSyncVxlanRouteEntry::KSyncVxlanRouteEntry(KSyncVxlanRouteObject *obj,
                                           const AgentRoute *route) :
    KSyncDBEntry(), vrf_id_(route->vrf_id()), ksync_obj_(obj) {
}

KSyncVxlanRouteEntry::~KSyncVxlanRouteEntry() {
}

KSyncDBObject *KSyncVxlanRouteEntry::GetObject() {
    return ksync_obj_;
}

bool KSyncVxlanRouteEntry::IsLess(const KSyncEntry &rhs) const {
    const KSyncVxlanRouteEntry &rhs_route =
        static_cast<const KSyncVxlanRouteEntry &>(rhs);
    Agent::RouteTableType lhs_type = ksync_obj_->route_table()->GetTableType();
    Agent::RouteTableType rhs_type =
        rhs_route.ksync_obj_->route_table()->GetTableType();

    if (lhs_type != rhs_type)
        return lhs_type < rhs_type;

    if (vrf_id_ != rhs_route.vrf_id_)
        return vrf_id_ < rhs_route.vrf_id_;

    return CompareRoute(rhs_route);
}

/**************************************************************************
 **************************************************************************/
KSyncVxlanFdbEntry::KSyncVxlanFdbEntry(KSyncVxlanRouteObject *obj,
                                       const KSyncVxlanFdbEntry *entry) :
    KSyncVxlanRouteEntry(obj, entry), bridge_(entry->bridge_),
    mac_(entry->mac_), port_(entry->port_), tunnel_dest_(entry->tunnel_dest_) {
}

KSyncVxlanFdbEntry::KSyncVxlanFdbEntry(KSyncVxlanRouteObject *obj,
                                       const Layer2RouteEntry *route) :
    KSyncVxlanRouteEntry(obj, route), bridge_(NULL), mac_(route->GetAddress()),
    port_(NULL), tunnel_dest_() {
}

KSyncVxlanFdbEntry::~KSyncVxlanFdbEntry() {
}

bool KSyncVxlanFdbEntry::CompareRoute(const KSyncVxlanRouteEntry &rhs) const {
    const KSyncVxlanFdbEntry &entry = static_cast
        <const KSyncVxlanFdbEntry &>(rhs);
    return (mac_.CompareTo(entry.mac_) < 0);
}

std::string KSyncVxlanFdbEntry::ToString() const {
    std::stringstream s;
    s << "FDB : ";
    return s.str();
}

bool KSyncVxlanFdbEntry::Sync(DBEntry *e) {
    bool ret = false;

    KSyncVxlanBridgeEntry *bridge = bridge_;
    uint32_t new_vxlan_id = VxLanTable::kInvalidvxlan_id;
    uint32_t old_vxlan_id = VxLanTable::kInvalidvxlan_id;

    Layer2RouteEntry *fdb = static_cast<Layer2RouteEntry *>(e);
    KSyncVxlanRouteObject *obj =
        static_cast<KSyncVxlanRouteObject *>(GetObject());
    Agent *agent = obj->ksync()->agent();

    const AgentPath *path = fdb->GetActivePath();
    if (path) {
        new_vxlan_id = path->vxlan_id();
    }

    if (bridge_) {
        old_vxlan_id = bridge_->vxlan_id();
    }

    if (old_vxlan_id != new_vxlan_id) {
        KSyncVxlanBridgeObject *bridge_obj =
            ksync_object()->ksync()->bridge_obj();
        VxLanIdKey key(new_vxlan_id);
        VxLanId *vxlan = static_cast<VxLanId *>
            (agent->vxlan_table()->FindActiveEntry(&key));
        if (vxlan) {
            KSyncEntry *vxlan_key = bridge_obj->DBToKSyncEntry(vxlan);
            bridge = static_cast<KSyncVxlanBridgeEntry *>
                (bridge_obj->GetReference(vxlan_key));
            delete vxlan_key;
            assert(bridge);
        }
    }

    if (bridge_ != bridge) {
        bridge_ = bridge;
        ret = true;
    }

    // Look for change in nexthop
    KSyncVxlanPortEntry *port = NULL;
    Ip4Address tunnel_dest = Ip4Address(0);
    const NextHop *nh = fdb->GetActiveNextHop();
    if (nh != NULL) {
        if (nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            KSyncVxlanPortObject *port_obj =
                ksync_object()->ksync()->port_obj();
            KSyncEntry *port_key =
                port_obj->DBToKSyncEntry(intf_nh->GetInterface());
            port = static_cast<KSyncVxlanPortEntry *>
                (port_obj->GetReference(port_key));
            delete port_key;
            assert(port);
        }

        if (nh->GetType() == NextHop::TUNNEL) {
            const TunnelNH *tunnel_nh = static_cast<const TunnelNH *>(nh);
            tunnel_dest = *tunnel_nh->GetDip();
        }
    }

    if (port_ != port) {
        port_ = port;
        ret = true;
    }

    if (tunnel_dest_ != tunnel_dest) {
        tunnel_dest_ = tunnel_dest;
        ret = true;
    }

    return ret;
}

KSyncEntry *KSyncVxlanFdbEntry::UnresolvedReference() {
    if (bridge_ == NULL) {
        return KSyncVxlan::defer_entry();
    }

    if (bridge_->IsResolved() == false) {
        return bridge_;
    }


    if (port_ == NULL && tunnel_dest_.to_ulong() == 0) {
        return KSyncVxlan::defer_entry();
    }

    if (port_ && port_->IsResolved() == false) {
        return port_;
    }

    return NULL;
}

/**************************************************************************
 **************************************************************************/
KSyncVxlanRouteObject::KSyncVxlanRouteObject(KSyncVxlanVrfObject *vrf,
                                             AgentRouteTable *rt_table) :
    KSyncDBObject(), ksync_(vrf->ksync()), marked_delete_(false),
    table_delete_ref_(this, rt_table->deleter()) {
    rt_table_ = rt_table;
    RegisterDb(rt_table);
}

KSyncVxlanRouteObject::~KSyncVxlanRouteObject() {
    UnregisterDb(GetDBTable());
    table_delete_ref_.Reset(NULL);
}

void KSyncVxlanRouteObject::Unregister() {
    if (IsEmpty() == true && marked_delete_ == true) {
        ksync_->vrf_obj()->DelFromVrfMap(this);
        KSyncObjectManager::Unregister(this);
    }
}

void KSyncVxlanRouteObject::ManagedDelete() {
    marked_delete_ = true;
    Unregister();
}

void KSyncVxlanRouteObject::EmptyTable() {
    if (marked_delete_ == true) {
        Unregister();
    }
}

/****************************************************************************
 * VRF Notification handler
 * Creates a KSyncVxlanRouteObject for every VRF that is created
 * The RouteObject is stored in VrfRouteObjectMap
 ***************************************************************************/
KSyncVxlanVrfObject::KSyncVxlanVrfObject(KSyncVxlan *ksync) : ksync_(ksync) {
}

KSyncVxlanVrfObject::~KSyncVxlanVrfObject() {
}

// Register to the VRF table
void KSyncVxlanVrfObject::RegisterDBClients() {
    vrf_listener_id_ = ksync_->agent()->vrf_table()->Register
            (boost::bind(&KSyncVxlanVrfObject::VrfNotify, this, _1, _2));
}

KSyncVxlanRouteObject *KSyncVxlanVrfObject::GetRouteKSyncObject(uint32_t vrf_id)
    const {
    VrfRouteObjectMap::const_iterator it;
    it = vrf_fdb_object_map_.find(vrf_id);
    if (it != vrf_fdb_object_map_.end()) {
        return it->second;
    }
}

void KSyncVxlanVrfObject::AddToVrfMap(uint32_t vrf_id,
                                      KSyncVxlanRouteObject *rt) {
    vrf_fdb_object_map_.insert(make_pair(vrf_id, rt));
}

void KSyncVxlanVrfObject::DelFromVrfMap(KSyncVxlanRouteObject *rt) {
    VrfRouteObjectMap::iterator it;
    for (it = vrf_fdb_object_map_.begin(); it != vrf_fdb_object_map_.end();
        ++it) {
        if (it->second == rt) {
            vrf_fdb_object_map_.erase(it);
            return;
        }
    }
}

void KSyncVxlanVrfObject::VrfNotify(DBTablePartBase *partition, DBEntryBase *e){
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
        state = new VrfState();
        state->seen_ = true;
        vrf->SetState(partition->parent(), vrf_listener_id_, state);
        AddToVrfMap(vrf->vrf_id(), AllocLayer2RouteTable(vrf));
    }

    return;
}

void KSyncVxlanVrfObject::Init() {
}

void KSyncVxlanVrfObject::Shutdown() {
    ksync_->agent()->vrf_table()->Unregister(vrf_listener_id_);
    vrf_listener_id_ = -1;
}
