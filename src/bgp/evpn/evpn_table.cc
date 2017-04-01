/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_table.h"

#include "bgp/ipeer.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"

using std::auto_ptr;
using std::string;

size_t EvpnTable::HashFunction(const EvpnPrefix &prefix) {
    if (prefix.type() == EvpnPrefix::MacAdvertisementRoute) {
        if (prefix.mac_addr().IsBroadcast())
            return 0;
        const uint8_t *data = prefix.mac_addr().GetData();
        uint32_t value = get_value(data + 2, 4);
        return boost::hash_value(value);
    }
    if (prefix.type() == EvpnPrefix::IpPrefixRoute) {
        if (prefix.ip_address().is_v4()) {
            return InetTable::HashFunction(prefix.inet_prefix());
        } else {
            return Inet6Table::HashFunction(prefix.inet6_prefix());
        }
    }
    return 0;
}

EvpnTable::EvpnTable(DB *db, const string &name)
    : BgpTable(db, name), evpn_manager_(NULL) {
    mac_route_count_ = 0;
    unique_mac_route_count_ = 0;
    im_route_count_ = 0;
}

auto_ptr<DBEntry> EvpnTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return auto_ptr<DBEntry> (new EvpnRoute(pfxkey->prefix));
}

auto_ptr<DBEntry> EvpnTable::AllocEntryStr(
        const string &key_str) const {
    EvpnPrefix prefix = EvpnPrefix::FromString(key_str);
    return auto_ptr<DBEntry> (new EvpnRoute(prefix));
}

void EvpnTable::AddRemoveCallback(const DBEntryBase *entry, bool add) const {
    if (IsVpnTable())
        return;
    const EvpnRoute *evpn_rt = static_cast<const EvpnRoute *>(entry);
    const EvpnPrefix &evpn_prefix = evpn_rt->GetPrefix();
    switch (evpn_prefix.type()) {
    case EvpnPrefix::MacAdvertisementRoute:
        // Ignore Broadcast MAC routes.
        if (evpn_prefix.mac_addr().IsBroadcast())
            break;

        if (add) {
            mac_route_count_++;
        } else {
            mac_route_count_--;
        }

        // Ignore MAC routes with IP addresses.
        if (evpn_prefix.family() != Address::UNSPEC)
            break;

        if (add) {
            unique_mac_route_count_++;
        } else {
            unique_mac_route_count_--;
        }
        break;

    case EvpnPrefix::InclusiveMulticastRoute:
        if (add) {
            im_route_count_++;
        } else {
            im_route_count_--;
        }
        break;

    default:
        break;
    }
}

size_t EvpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

size_t EvpnTable::Hash(const DBEntry *entry) const {
    const EvpnRoute *rt_entry = static_cast<const EvpnRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

BgpRoute *EvpnTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    EvpnRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *EvpnTable::CreateTable(DB *db, const string &name) {
    EvpnTable *table = new EvpnTable(db, name);
    table->Init();
    return table;
}

BgpRoute *EvpnTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    assert(src_table->family() == Address::EVPN);
    EvpnRoute *evpn_rt = dynamic_cast<EvpnRoute *>(src_rt);
    assert(evpn_rt);
    EvpnPrefix evpn_prefix(evpn_rt->GetPrefix());

    if (!IsMaster() && evpn_prefix.type() != EvpnPrefix::IpPrefixRoute) {
        // Don't replicate to a VRF from other VRF tables.
        EvpnTable *src_evpn_table = dynamic_cast<EvpnTable *>(src_table);
        if (!src_evpn_table->IsMaster())
            return NULL;

        // Don't replicate to VRF from the VPN table if OriginVn doesn't match.
        OriginVn origin_vn(server->autonomous_system(),
            routing_instance()->virtual_network_index());
        if (!community->ContainsOriginVn(origin_vn.GetExtCommunity()))
            return NULL;
    }

    if (evpn_prefix.type() == EvpnPrefix::AutoDiscoveryRoute)
        return NULL;
    if (evpn_prefix.type() == EvpnPrefix::SegmentRoute)
        return NULL;
    if (evpn_prefix.type() == EvpnPrefix::MacAdvertisementRoute &&
        evpn_prefix.mac_addr().IsBroadcast())
        return NULL;

    BgpAttrDB *attr_db = server->attr_db();
    BgpAttrPtr new_attr(src_path->GetAttr());

    if (IsMaster()) {
        if (evpn_prefix.route_distinguisher().IsZero()) {
            evpn_prefix.set_route_distinguisher(new_attr->source_rd());
        }

        if (evpn_prefix.type() != EvpnPrefix::IpPrefixRoute) {
            Ip4Address originator_id = new_attr->nexthop().to_v4();
            new_attr = attr_db->ReplaceOriginatorIdAndLocate(
                new_attr.get(), originator_id);
        }
    } else {
        if (evpn_prefix.type() == EvpnPrefix::MacAdvertisementRoute ||
            evpn_prefix.type() == EvpnPrefix::IpPrefixRoute) {
            evpn_prefix.set_route_distinguisher(RouteDistinguisher::kZeroRd);
        }
    }
    EvpnRoute rt_key(evpn_prefix);

    // Find or create the route.
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new EvpnRoute(evpn_prefix);
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    new_attr = attr_db->ReplaceExtCommunityAndLocate(new_attr.get(), community);

    // Check whether peer already has a path
    BgpPath *dest_path = dest_route->FindSecondaryPath(src_rt,
            src_path->GetSource(), src_path->GetPeer(),
            src_path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetOriginalAttr()) ||
            (src_path->GetFlags() != dest_path->GetFlags()) ||
            (src_path->GetLabel() != dest_path->GetLabel()) ||
            (src_path->GetL3Label() != dest_path->GetL3Label())) {
            bool success = dest_route->RemoveSecondaryPath(src_rt,
                src_path->GetSource(), src_path->GetPeer(),
                src_path->GetPathId());
            assert(success);
        } else {
            return dest_route;
        }
    }

    // Create replicated path and insert it on the route
    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(),
                             src_path->GetSource(), new_attr,
                             src_path->GetFlags(), src_path->GetLabel(),
                             src_path->GetL3Label());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Always trigger notification.
    rtp->Notify(dest_route);

    return dest_route;
}

bool EvpnTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    EvpnRoute *evpn_route = dynamic_cast<EvpnRoute *>(route);
    assert(evpn_route);

    if (ribout->IsEncodingBgp()) {
        UpdateInfo *uinfo = GetUpdateInfo(ribout, evpn_route, peerset);
        if (!uinfo)
            return false;
        uinfo_slist->push_front(*uinfo);
        return true;
    }

    const EvpnPrefix &evpn_prefix = evpn_route->GetPrefix();
    if (evpn_prefix.type() != EvpnPrefix::MacAdvertisementRoute)
        return false;

    if (!evpn_prefix.mac_addr().IsBroadcast()) {
        UpdateInfo *uinfo = GetUpdateInfo(ribout, evpn_route, peerset);
        if (!uinfo)
            return false;
        uinfo_slist->push_front(*uinfo);
        return true;
    }

    if (!evpn_manager_ || evpn_manager_->deleter()->IsDeleted())
        return false;

    const IPeer *peer = evpn_route->BestPath()->GetPeer();
    if (!peer || !ribout->IsRegistered(const_cast<IPeer *>(peer)))
        return false;

    size_t peerbit = ribout->GetPeerIndex(const_cast<IPeer *>(peer));
    if (!peerset.test(peerbit))
        return false;

    UpdateInfo *uinfo = evpn_manager_->GetUpdateInfo(evpn_route);
    if (!uinfo)
        return false;

    uinfo->target.set(peerbit);
    uinfo_slist->push_front(*uinfo);
    return true;
}

void EvpnTable::CreateEvpnManager() {
    if (IsVpnTable())
        return;
    assert(!evpn_manager_);
    evpn_manager_ = BgpObjectFactory::Create<EvpnManager>(this);
    evpn_manager_->Initialize();
}

void EvpnTable::DestroyEvpnManager() {
    assert(evpn_manager_);
    evpn_manager_->Terminate();
    delete evpn_manager_;
    evpn_manager_ = NULL;
}

EvpnManager *EvpnTable::GetEvpnManager() {
    return evpn_manager_;
}

const EvpnManager *EvpnTable::GetEvpnManager() const {
    return evpn_manager_;
}

void EvpnTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
    CreateEvpnManager();
}

bool EvpnTable::IsMaster() const {
    return routing_instance()->IsMasterRoutingInstance();
}

static void RegisterFactory() {
    DB::RegisterFactory("evpn.0", &EvpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
