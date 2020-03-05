/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_table.h"

#include <boost/foreach.hpp>

#include "sandesh/sandesh_trace.h"
#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/iroute_aggregator.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "net/community_type.h"

using std::make_pair;
using std::string;
using boost::scoped_ptr;

class BgpTable::DeleteActor : public LifetimeActor {
  public:
    explicit DeleteActor(BgpTable *table)
        : LifetimeActor(table->rtinstance_->server()->lifetime_manager()),
          table_(table) {
    }
    virtual ~DeleteActor() {
    }
    virtual bool MayDelete() const {
        return table_->MayDelete();
    }

    virtual void Shutdown() {
        table_->Shutdown();
    }

    // Make sure that all notifications have been processed and all db
    // state have been cleared for all partitions, before we inform the
    // parent instance that this table deletion process is complete.
    virtual void Destroy() {
        table_->rtinstance_->DestroyDBTable(table_);
    }

  private:
    BgpTable *table_;
};

BgpTable::BgpTable(DB *db, const string &name)
    : RouteTable(db, name),
      rtinstance_(NULL),
      path_resolver_(NULL),
      instance_delete_ref_(this, NULL) {
    primary_path_count_ = 0;
    secondary_path_count_ = 0;
    infeasible_path_count_ = 0;
    stale_path_count_ = 0;
    llgr_stale_path_count_ = 0;
}

//
// Remove the table from the instance dependents before attempting to
// destroy the DeleteActor which can have its Delete() method be called
// via the reference.
//
BgpTable::~BgpTable() {
    assert(path_resolver_ == NULL),
    instance_delete_ref_.Reset(NULL);
}

void BgpTable::set_routing_instance(RoutingInstance *rtinstance) {
    rtinstance_ = rtinstance;
    assert(rtinstance);
    deleter_.reset(new DeleteActor(this));
    instance_delete_ref_.Reset(rtinstance->deleter());
}

BgpServer *BgpTable::server() {
    return rtinstance_->server();
}

const BgpServer *BgpTable::server() const {
    return rtinstance_->server();
}

//
// Find the RibOut for the given RibExportPolicy.
//
RibOut *BgpTable::RibOutFind(const RibExportPolicy &policy) {
    RibOutMap::iterator loc = ribout_map_.find(policy);
    return (loc != ribout_map_.end()) ? loc->second : NULL;
}

//
// Find or create the RibOut associated with the given RibExportPolicy.
// If a new RibOut is created, an entry for the pair gets added to the
// RibOutMap.
//
RibOut *BgpTable::RibOutLocate(BgpUpdateSender *sender,
                               const RibExportPolicy &policy) {
    RibOutMap::iterator loc = ribout_map_.find(policy);
    if (loc == ribout_map_.end()) {
        RibOut *ribout = new RibOut(this, sender, policy);
        ribout_map_.insert(make_pair(policy, ribout));
        return ribout;
    }
    return loc->second;
}

//
// Delete the entry corresponding to the given RibExportPolicy from the
// RibOutMap.  Also deletes the RibOut itself.
//
void BgpTable::RibOutDelete(const RibExportPolicy &policy) {
    RibOutMap::iterator loc = ribout_map_.find(policy);
    assert(loc != ribout_map_.end());
    delete loc->second;
    ribout_map_.erase(loc);
}

// If both as2 and as4 aggregators are present then we need to choose one
void BgpTable::CheckAggregatorAttr(BgpAttr *attr) const {
    if (attr->aggregator_as_num() && attr->aggregator_as4_num()) {
        if (attr->aggregator_as_num() != AS_TRANS) {
            // If as2 aggregator is not as_trans then we ignore as4 aggregator
            // and ignore as4_path also
            attr->set_as4_path(NULL);
            attr->set_as4_aggregator(0, attr->aggregator_adderess());
        } else {
            // If as2 aggregator is as_trans then we use as4 aggregator
            attr->set_aggregator(attr->aggregator_as4_num(),
                                 attr->aggregator_adderess());
            attr->set_as4_aggregator(0, attr->aggregator_adderess());
        }
    }
}

void BgpTable::PrependLocalAs(const RibOut *ribout, BgpAttr *clone,
        const IPeer* peer) const {
    CheckAggregatorAttr(clone);
    as_t local_as = ribout->local_as() ?:
                clone->attr_db()->server()->local_autonomous_system();
    if (!server()->enable_4byte_as())
       return PrependAsToAsPath2Byte(clone, (as2_t)local_as);
    if (ribout->as4_supported()) {
        if (clone->aspath_4byte())
            PrependAsToAsPath4Byte(clone, local_as);
        else
            CreateAsPath4Byte(clone, local_as);
    } else {
        if (!clone->as_path())
            CreateAsPath2Byte(clone);
        PrependAsToAsPath2Byte(clone, local_as);
    }
}

void BgpTable::RemovePrivateAs(const RibOut *ribout, BgpAttr *attr) const {
    bool all = ribout->remove_private_all();
    bool replace = ribout->remove_private_replace();
    bool peer_loop_check = ribout->remove_private_peer_loop_check();

    const AsPathSpec &spec = attr->as_path()->path();
    as_t peer_asn = peer_loop_check ? ribout->peer_as() : 0;
    as_t replace_asn = 0;
    if (replace) {
        if (ribout->peer_type() == BgpProto::EBGP) {
            replace_asn = server()->local_autonomous_system();
        } else {
            replace_asn = spec.AsLeftMostPublic();
        }
    }
    if (replace_asn > AS2_MAX) {
        if (!attr->as4_path())
            CreateAs4Path(attr);
        const As4PathSpec &spec = attr->as4_path()->path();
        As4PathSpec *new_spec = spec.RemovePrivate(all, replace_asn, peer_asn);
        attr->set_as4_path(new_spec);
        delete new_spec;
        replace_asn = AS_TRANS;
    }
    if (peer_asn > AS2_MAX)
        peer_asn = AS_TRANS;

    AsPathSpec *new_spec = spec.RemovePrivate(all, replace_asn, peer_asn);
    attr->set_as_path(new_spec);
    delete new_spec;
}

void BgpTable::RemovePrivate4ByteAs(const RibOut *ribout, BgpAttr *attr) const {
    bool all = ribout->remove_private_all();
    bool replace = ribout->remove_private_replace();
    bool peer_loop_check = ribout->remove_private_peer_loop_check();

    const AsPath4ByteSpec &spec = attr->aspath_4byte()->path();
    as_t peer_asn = peer_loop_check ? ribout->peer_as() : 0;
    as_t replace_asn = 0;
    if (replace) {
        if (ribout->peer_type() == BgpProto::EBGP) {
            replace_asn = server()->local_autonomous_system();
        } else {
            replace_asn = spec.AsLeftMostPublic();
        }
    }

    AsPath4ByteSpec *new_spec = spec.RemovePrivate(all, replace_asn, peer_asn);
    attr->set_aspath_4byte(new_spec);
    delete new_spec;
}

void BgpTable::RemovePrivateAs4(const RibOut *ribout, BgpAttr *attr) const {
    bool all = ribout->remove_private_all();
    bool replace = ribout->remove_private_replace();
    bool peer_loop_check = ribout->remove_private_peer_loop_check();

    const As4PathSpec &spec = attr->as4_path()->path();
    as_t peer_asn = peer_loop_check ? ribout->peer_as() : 0;
    as_t replace_asn = 0;
    if (replace) {
        if (ribout->peer_type() == BgpProto::EBGP) {
            replace_asn = server()->local_autonomous_system();
        } else {
            replace_asn = spec.AsLeftMostPublic();
        }
    }

    As4PathSpec *new_spec = spec.RemovePrivate(all, replace_asn, peer_asn);
    attr->set_as4_path(new_spec);
    delete new_spec;
}

//
// Process Remove Private information.
//
void BgpTable::ProcessRemovePrivate(const RibOut *ribout, BgpAttr *attr) const {
    if (!ribout->IsEncodingBgp())
        return;
    if (!ribout->remove_private_enabled())
        return;

    if (attr->as_path())
        RemovePrivateAs(ribout, attr);
    if (attr->aspath_4byte())
        RemovePrivate4ByteAs(ribout, attr);
    if (attr->as4_path())
        RemovePrivateAs4(ribout, attr);
}

//
// Process Remove Private information.
//
void BgpTable::ProcessAsOverride(const RibOut *ribout, BgpAttr *attr) const {
    if (ribout->as_override() && !attr->IsAsPathEmpty()) {
        as_t replace_as = ribout->local_as() ?:
                attr->attr_db()->server()->local_autonomous_system();
        if (attr->as_path()) {
            const AsPathSpec &as_path = attr->as_path()->path();
            // If peer_as is > 0xffff, it can't be in as_path
            if (ribout->peer_as() <= AS2_MAX) {
                if (replace_as > AS2_MAX) {
                    // if replace_as > 0xffff, as4_path should be created if
                    // not already there by copying data from as_path
                    if (!attr->as4_path()) {
                        CreateAs4Path(attr);
                    }
                    replace_as = AS_TRANS;
                }
                AsPathSpec *as_path_ptr = as_path.Replace(
                                                 ribout->peer_as(), replace_as);
                attr->set_as_path(as_path_ptr);
                delete as_path_ptr;
            }
            if (attr->as4_path()) {
                As4PathSpec *as_path_ptr = attr->as4_path()->path().Replace(
                                                 ribout->peer_as(), replace_as);
                attr->set_as4_path(as_path_ptr);
                delete as_path_ptr;
            }
        }
        if (attr->aspath_4byte()) {
            const AsPath4ByteSpec &as_path = attr->aspath_4byte()->path();
            AsPath4ByteSpec *as_path_ptr =
                          as_path.Replace(ribout->peer_as(), replace_as);
            attr->set_aspath_4byte(as_path_ptr);
            delete as_path_ptr;
        }
    }
}

//
// Process Long Lived Graceful Restart state information.
//
// For LLGR_STALE paths, if the peer supports LLGR then attach LLGR_STALE
// community. Otherwise, strip LLGR_STALE community, reduce LOCAL_PREF and
// attach NO_EXPORT community instead.
//
void BgpTable::ProcessLlgrState(const RibOut *ribout, const BgpPath *path,
                                BgpAttr *attr, bool llgr_stale_comm) {
    if (!server() || !server()->comm_db())
        return;

    // Skip LLGR specific attributes manipulation for rtarget routes.
    if (family() == Address::RTARGET)
        return;

    // If the path is not marked as llgr_stale or if it does not have the
    // LLGR_STALE community, then no action is necessary.
    if (!path->IsLlgrStale() && !llgr_stale_comm)
        return;

    // If peers support LLGR, then attach LLGR_STALE community and return.
    if (ribout->llgr()) {
        if (!llgr_stale_comm) {
            CommunityPtr comm = server()->comm_db()->AppendAndLocate(
                    attr->community(), CommunityType::LlgrStale);
            attr->set_community(comm);
        }
        return;
    }

    // Peers do not understand LLGR. Bring down local preference instead to
    // make the advertised path less preferred.
    attr->set_local_pref(0);

    // Remove LLGR_STALE community as the peers do not support LLGR.
    if (llgr_stale_comm) {
        CommunityPtr comm = server()->comm_db()->RemoveAndLocate(
                                attr->community(), CommunityType::LlgrStale);
        attr->set_community(comm);
    }

    // Attach NO_EXPORT community as well to make sure that this path does not
    // exits local AS, unless it is already present.
    if (!attr->community() ||
        !attr->community()->ContainsValue(CommunityType::NoExport)) {
        CommunityPtr comm = server()->comm_db()->AppendAndLocate(
                                attr->community(), CommunityType::NoExport);
        attr->set_community(comm);
    }
}

void BgpTable::ProcessDefaultTunnelEncapsulation(const RibOut *ribout,
    ExtCommunityDB *extcomm_db, BgpAttr *attr) const {
    if (!ribout->ExportPolicy().default_tunnel_encap_list.empty()) {
        ExtCommunity::ExtCommunityList encap_list;
        BOOST_FOREACH(const string &encap_string,
          ribout->ExportPolicy().default_tunnel_encap_list) {
            TunnelEncap tunnel_encap(encap_string);
            encap_list.push_back(tunnel_encap.GetExtCommunity());
        }
        ExtCommunityPtr ext_community = attr->ext_community();
        ext_community =
            extcomm_db->ReplaceTunnelEncapsulationAndLocate(
            ext_community.get(), encap_list);
        attr->set_ext_community(ext_community);
    }
}

void BgpTable::PrependAsToAsPath2Byte(BgpAttr *attr, as2_t asn) const {
    if (attr->as_path()) {
        const AsPathSpec &as_path = attr->as_path()->path();
        AsPathSpec *as_path_ptr = as_path.Add(asn);
        attr->set_as_path(as_path_ptr);
        delete as_path_ptr;
    } else {
        AsPathSpec as_path;
        AsPathSpec *as_path_ptr = as_path.Add(asn);
        attr->set_as_path(as_path_ptr);
        delete as_path_ptr;
    }
}

void BgpTable::PrependAsToAsPath2Byte(BgpAttr *attr, as_t asn) const {
    if (asn <= AS2_MAX) {
        if (attr->as_path() && attr->as4_path()) {
            PrependAsToAs4Path(attr, asn);
        }
        return PrependAsToAsPath2Byte(attr, static_cast<as2_t>(asn & AS2_MAX));
    }
    if (attr->as_path() && !attr->as4_path()) {
        CreateAs4Path(attr);
        assert(attr->as_path()->path().path_segments.size() ==
                    attr->as4_path()->path().path_segments.size());
    }
    as2_t as_trans = AS_TRANS;
    PrependAsToAsPath2Byte(attr, as_trans);
    PrependAsToAs4Path(attr, asn);
}

void BgpTable::PrependAsToAsPath4Byte(BgpAttr *clone, as_t asn) const {
    if (clone->aspath_4byte()) {
        const AsPath4ByteSpec &as4_path = clone->aspath_4byte()->path();
        AsPath4ByteSpec *as4_path_ptr = as4_path.Add(asn);
        clone->set_aspath_4byte(as4_path_ptr);
        delete as4_path_ptr;
    } else {
        AsPath4ByteSpec as_path;
        AsPath4ByteSpec *as_path_ptr = as_path.Add(asn);
        clone->set_aspath_4byte(as_path_ptr);
        delete as_path_ptr;
    }
}

void BgpTable::PrependAsToAs4Path(BgpAttr* attr, as_t asn) const {
    if (attr->as4_path()) {
        const As4PathSpec &as4_path = attr->as4_path()->path();
        As4PathSpec *as4_path_ptr = as4_path.Add(asn);
        attr->set_as4_path(as4_path_ptr);
        delete as4_path_ptr;
    } else {
        As4PathSpec as_path;
        As4PathSpec *as_path_ptr = as_path.Add(asn);
        attr->set_as4_path(as_path_ptr);
        delete as_path_ptr;
    }
}

// Create as4_path by copying data from as_path
void BgpTable::CreateAs4Path(BgpAttr *attr) const {
    if (attr->as_path()) {
        scoped_ptr<As4PathSpec> new_as_path(new As4PathSpec);
        const AsPathSpec &as_path = attr->as_path()->path();
        for (size_t i = 0; i < as_path.path_segments.size(); i++) {
            As4PathSpec::PathSegment *ps4 = new As4PathSpec::PathSegment;
            AsPathSpec::PathSegment *ps = as_path.path_segments[i];
            ps4->path_segment_type = ps->path_segment_type;
            for (size_t j = 0; j < ps->path_segment.size(); j++) {
                as_t as = ps->path_segment[j];
                ps4->path_segment.push_back(as);
            }
            new_as_path->path_segments.push_back(ps4);
        }
        attr->set_as4_path(new_as_path.get());
    }
}

// Check if aspath_4byte has any asn > 0xFFFF
bool BgpTable::Has4ByteAsn(BgpAttr *attr) const {
    if (!attr->aspath_4byte())
        return false;
    const AsPath4ByteSpec &as_path4 = attr->aspath_4byte()->path();
    for (size_t i = 0; i < as_path4.path_segments.size(); ++i) {
        AsPath4ByteSpec::PathSegment *ps4 = as_path4.path_segments[i];
        for (size_t j = 0; j < ps4->path_segment.size(); ++j) {
            if (ps4->path_segment[j] > AS2_MAX)
                return true;
        }
    }
    return false;
}

// Create as_path (and as4_path) from as_path4byte
void BgpTable::CreateAsPath2Byte(BgpAttr *attr) const {
    scoped_ptr<AsPathSpec> new_as_path(new AsPathSpec);
    As4PathSpec *new_as4_path = NULL;
    bool has_4byte_asn = Has4ByteAsn(attr);
    if (has_4byte_asn)
        new_as4_path = new As4PathSpec;
    if (attr->aspath_4byte()) {
        const AsPath4ByteSpec &as_path4 = attr->aspath_4byte()->path();
        for (size_t i = 0; i < as_path4.path_segments.size(); i++) {
            AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
            As4PathSpec::PathSegment *as4_ps = NULL;
            AsPath4ByteSpec::PathSegment *ps4 = as_path4.path_segments[i];
            ps->path_segment_type = ps4->path_segment_type;
            if (has_4byte_asn) {
                as4_ps = new As4PathSpec::PathSegment;
                as4_ps->path_segment_type = ps4->path_segment_type;
            }
            for (size_t j = 0; j < ps4->path_segment.size(); ++j) {
                as_t as4 = ps4->path_segment[j];
                if (as4 > AS2_MAX) {
                    as2_t as_trans = AS_TRANS;
                    ps->path_segment.push_back(as_trans);
                } else {
                    ps->path_segment.push_back(static_cast<as2_t>(
                                               as4 & AS2_MAX));
                }
                if (has_4byte_asn)
                    as4_ps->path_segment.push_back(as4);
            }
            new_as_path->path_segments.push_back(ps);
            if (has_4byte_asn)
                new_as4_path->path_segments.push_back(as4_ps);
        }
        attr->set_aspath_4byte(NULL);
    }
    attr->set_as_path(new_as_path.get());
    if (has_4byte_asn)
        attr->set_as4_path(new_as4_path);
}

// Create aspath_4byte by merging as_path and as4_path
void BgpTable::CreateAsPath4Byte(BgpAttr *attr, as_t local_as) const {
    int as2_count = attr->as_path_count();
    int as4_count = attr->as4_path_count();
    if (as2_count < as4_count) {
        as4_count = 0;
        attr->set_as4_path(NULL);
    }
    AsPath4ByteSpec::PathSegment *ps4 = NULL;
    bool part_segment = false;
    scoped_ptr<AsPath4ByteSpec> aspath_4byte(new AsPath4ByteSpec);
    if (attr->as_path()) {
        const AsPathSpec &as_path = attr->as_path()->path();
        int new_as_count = 0;
        for (size_t i = 0; i < as_path.path_segments.size() &&
                    new_as_count < (as2_count - as4_count) ; ++i) {
            ps4 = new AsPath4ByteSpec::PathSegment;
            AsPathSpec::PathSegment *ps = as_path.path_segments[i];
            ps4->path_segment_type = ps->path_segment_type;
            if (ps->path_segment_type == AsPathSpec::PathSegment::AS_SET) {
                new_as_count++;
                for (size_t j = 0; j < ps->path_segment.size(); ++j) {
                    as2_t as = ps->path_segment[j];
                    ps4->path_segment.push_back(as);
                }
            } else {
                for (size_t j = 0; j < ps->path_segment.size() &&
                        new_as_count < (as2_count - as4_count); ++j) {
                    new_as_count++;
                    as2_t as = ps->path_segment[j];
                    ps4->path_segment.push_back(as);
                }
                if (new_as_count == (as2_count - as4_count)) {
                    if (attr->as4_path()) {
                        part_segment = true;
                        break;
                    }
                }
            }
            aspath_4byte->path_segments.push_back(ps4);
        }
        if (attr->as4_path()) {
            const As4PathSpec &as4_path = attr->as4_path()->path();
            for (size_t i = 0; i < as4_path.path_segments.size(); ++i) {
                if (!part_segment) {
                    ps4 = new AsPath4ByteSpec::PathSegment;
                }
                part_segment = false;
                As4PathSpec::PathSegment *ps = as4_path.path_segments[i];
                ps4->path_segment_type = ps->path_segment_type;
                if (ps->path_segment_type == As4PathSpec::PathSegment::AS_SET) {
                    new_as_count++;
                    for (size_t j = 0; j < ps->path_segment.size(); ++j) {
                        as2_t as = ps->path_segment[j];
                        ps4->path_segment.push_back(as);
                    }
                } else {
                    for (size_t j = 0; j < ps->path_segment.size(); ++j) {
                        as_t as = ps->path_segment[j];
                        ps4->path_segment.push_back(as);
                    }
                }
                aspath_4byte->path_segments.push_back(ps4);
            }
            attr->set_as4_path(NULL);
        }
        attr->set_as_path(NULL);
    }
    if (local_as) {
        scoped_ptr<AsPath4ByteSpec> as_path_spec(aspath_4byte->Add(local_as));
        attr->set_aspath_4byte(as_path_spec.get());
    } else {
        attr->set_aspath_4byte(aspath_4byte.get());
    }
}

UpdateInfo *BgpTable::GetUpdateInfo(RibOut *ribout, BgpRoute *route,
        const RibPeerSet &peerset) {
    const BgpPath *path = route->BestPath();

    // Ignore if there is no best-path.
    if (!path)
        return NULL;

    // Don't advertise infeasible paths.
    if (!path->IsFeasible())
        return NULL;

    // Check whether the route is contributing route
    if (IsRouteAggregationSupported() && IsContributingRoute(route))
        return NULL;

    // Needs to be outside the if block so it's not destroyed prematurely.
    BgpAttrPtr attr_ptr;
    const BgpAttr *attr = path->GetAttr();

    RibPeerSet new_peerset = peerset;

    // LocalPref, Med and AsPath manipulation is needed only if the RibOut
    // has BGP encoding. Similarly, well-known communities do not apply if
    // the encoding is not BGP.
    if (ribout->IsEncodingBgp()) {
        // Handle well-known communities.
        if (attr->community() != NULL &&
            attr->community()->communities().size()) {
            BOOST_FOREACH(uint32_t value, attr->community()->communities()) {
                if (value == CommunityType::NoAdvertise)
                    return NULL;

                if ((ribout->peer_type() == BgpProto::EBGP) &&
                    ((value == CommunityType::NoExport) ||
                     (value == CommunityType::NoExportSubconfed))) {
                    return NULL;
                }
            }
        }

        const IPeer *peer = path->GetPeer();
        BgpAttr *clone = NULL;
        bool llgr_stale_comm = attr->community() &&
            attr->community()->ContainsValue(CommunityType::LlgrStale);
        if (ribout->peer_type() == BgpProto::IBGP) {
            // Split horizon check.
            if (peer && peer->CheckSplitHorizon(server()->cluster_id(),
                        ribout->cluster_id()))
                return NULL;

            // Handle route-target filtering.
            if (IsVpnTable() && attr->ext_community() != NULL) {
                server()->rtarget_group_mgr()->GetRibOutInterestedPeers(
                    ribout, attr->ext_community(), peerset, &new_peerset);
                if (new_peerset.empty())
                    return NULL;
            }

            if (server()->cluster_id()) {
                // Check if there is a loop in cluster_list
                if (attr->cluster_list() && attr->cluster_list()->cluster_list()
                        .ClusterListLoop(server()->cluster_id())) {
                    return NULL;
                }
            }

            if (server()->cluster_id() && (family() != Address::RTARGET)) {
                // route reflector should not reflect the route back to the peer
                // from which it received that route, for non rtarget routes
                // This is done by checking peer_router_id of all the feasible
                // paths of this route
                RibPeerSet route_peerset;
                for (Route::PathList::iterator it= route->GetPathList().begin();
                        it != route->GetPathList().end(); it++) {
                    BgpPath *ipath = static_cast<BgpPath *>(it.operator->());
                    if (ipath->IsFeasible() && ipath->GetPeer()) {
                        const IPeer *ipeer = ipath->GetPeer();
                        const BgpPeer *bgp_peer = dynamic_cast<
                                                  const BgpPeer *>(ipeer);
                        if (bgp_peer)
                            route_peerset.set(bgp_peer->GetIndex());
                    }
                }
                RibOut::PeerIterator iter(ribout, new_peerset);
                while (iter.HasNext()) {
                    int current_index = iter.index();
                    IPeerUpdate *peer = iter.Next();
                    const BgpPeer *bgp_peer = dynamic_cast<
                                              const BgpPeer *>(peer);
                    if (bgp_peer && route_peerset.test(bgp_peer->GetIndex()))
                        new_peerset.reset(current_index);
                }
                if (new_peerset.empty())
                    return NULL;
            }

            clone = new BgpAttr(*attr);

            // Retain LocalPref value if set, else set default to 100.
            if (clone->local_pref() == 0)
                clone->set_local_pref(100);

            // Check aggregator attributes to identify which ones to be used
            CheckAggregatorAttr(clone);

            // Should not normally be needed for iBGP, but there could be
            // complex configurations where this is useful.
            ProcessRemovePrivate(ribout, clone);

            // Add Originator_Id if acting as route reflector and cluster_id
            // is not present
            if (server()->cluster_id()) {
                if (clone->originator_id().is_unspecified()) {
                    if (peer && (peer->bgp_identifier() != 0)) {
                        clone->set_originator_id(Ip4Address(
                                peer->bgp_identifier()));
                    } else {
                        clone->set_originator_id(Ip4Address(
                                server()->bgp_identifier()));
                    }
                }
                if (attr->cluster_list()) {
                    const ClusterListSpec &cluster =
                        clone->cluster_list()->cluster_list();
                    ClusterListSpec *cl_ptr = new ClusterListSpec(
                            server()->cluster_id(), &cluster);
                    clone->set_cluster_list(cl_ptr);
                    delete cl_ptr;
                } else {
                    ClusterListSpec *cl_ptr = new ClusterListSpec(
                            server()->cluster_id(), NULL);
                    clone->set_cluster_list(cl_ptr);
                    delete cl_ptr;
                }
            }
            // If the route is locally originated i.e. there's no AsPath,
            // then generate a Nil AsPath i.e. one with 0 length. No need
            // to modify the AsPath if it already exists since this is an
            // iBGP RibOut.
            if (ribout->as4_supported() && !clone->aspath_4byte()) {
                if (attr->as_path()) {
                    CreateAsPath4Byte(clone, 0);
                } else {
                    AsPath4ByteSpec as_path;
                    clone->set_aspath_4byte(&as_path);
                }
            }
            if (!ribout->as4_supported() && !clone->as_path()) {
                if (attr->aspath_4byte()) {
                    CreateAsPath2Byte(clone);
                } else {
                    AsPathSpec as_path;
                    clone->set_as_path(&as_path);
                }
            }
        } else if (ribout->peer_type() == BgpProto::EBGP) {
            // Don't advertise routes from non-master instances if there's
            // no nexthop. The ribout has to be for bgpaas-clients because
            // that's the only case with bgp peers in non-master instance.
            if (!rtinstance_->IsMasterRoutingInstance() &&
                ribout->nexthop().is_unspecified()) {
                return NULL;
            }

            // Handle route-target filtering.
            if (IsVpnTable() && attr->ext_community() != NULL) {
                server()->rtarget_group_mgr()->GetRibOutInterestedPeers(
                    ribout, attr->ext_community(), peerset, &new_peerset);
                if (new_peerset.empty())
                    return NULL;
            }

            // Sender side AS path loop check and split horizon within RibOut.
            if (!ribout->as_override()) {
                if (attr->IsAsPathLoop(ribout->peer_as()))
                    return NULL;
            } else {
                if (peer && peer->PeerType() == BgpProto::EBGP) {
                    ribout->GetSubsetPeerSet(&new_peerset, peer);
                    if (new_peerset.empty())
                        return NULL;
                }
            }

            clone = new BgpAttr(*attr);

            // Remove non-transitive attributes.
            // Note that med is handled further down.
            clone->set_originator_id(Ip4Address());
            clone->set_cluster_list(NULL);

            // Update nexthop.
            if (!ribout->nexthop().is_unspecified())
                clone->set_nexthop(ribout->nexthop());

            // Reset LocalPref.
            if (clone->local_pref())
                clone->set_local_pref(0);

            // Reset Med if the path did not originate from an xmpp peer.
            // The AS path is NULL if the originating xmpp peer is locally
            // connected. It's non-NULL but empty if the originating xmpp
            // peer is connected to another bgp speaker in the iBGP mesh.
            if (clone->med() && !clone->IsAsPathEmpty())
                clone->set_med(0);

            // Override the peer AS with local AS in AsPath.
            ProcessAsOverride(ribout, clone);

            // Remove private processing must happen before local AS prepend.
            ProcessRemovePrivate(ribout, clone);

            // Prepend the local AS to AsPath.
            PrependLocalAs(ribout, clone, peer);
        }

        assert(clone);

        // Update with the Default tunnel Encapsulation ordered List if
        // configured on the peer.
        // Note that all peers with the same list share the same Ribout, this is
        // ensured by making the Default Encapsulation List part of the Rib
        // Export policy.Note that, if there is Default Tunnel Encapsulation
        // configuration any tunnel encapsulation present is removed.
        ProcessDefaultTunnelEncapsulation(ribout, server()->extcomm_db(),
                                          clone);

        ProcessLlgrState(ribout, path, clone, llgr_stale_comm);

        // Locate the new BgpAttrPtr.
        attr_ptr = clone->attr_db()->Locate(clone);
        attr = attr_ptr.get();
    }

    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->target = new_peerset;
    uinfo->roattr = RibOutAttr(route, attr, ribout->IsEncodingXmpp());
    return uinfo;
}

// Bgp Path selection..
// Based Attribute weight
bool BgpTable::PathSelection(const Path &path1, const Path &path2) {
    const BgpPath &l_path = dynamic_cast<const BgpPath &> (path1);
    const BgpPath &r_path = dynamic_cast<const BgpPath &> (path2);

    // Check the weight of Path
    bool res = l_path.PathCompare(r_path, false) < 0;

    return res;
}

bool BgpTable::DeletePath(DBTablePartBase *root, BgpRoute *rt, BgpPath *path) {
    return InputCommon(root, rt, path, path->GetPeer(), NULL,
        DBRequest::DB_ENTRY_DELETE, NULL, path->GetPathId(), 0, 0, 0);
}

bool BgpTable::InputCommon(DBTablePartBase *root, BgpRoute *rt, BgpPath *path,
                           const IPeer *peer, DBRequest *req,
                           DBRequest::DBOperation oper, BgpAttrPtr attrs,
                           uint32_t path_id, uint32_t flags, uint32_t label,
                           uint32_t l3_label) {
    bool notify_rt = false;

    switch (oper) {
    case DBRequest::DB_ENTRY_ADD_CHANGE: {
        assert(rt);

        // The entry may currently be marked as deleted.
        rt->ClearDelete();
        if (peer)
            peer->UpdateCloseRouteStats(family(), path, flags);

        // Check whether peer already has a path.
        if (path != NULL) {
            if ((path->GetAttr() != attrs.get()) ||
                (path->GetFlags() != flags) ||
                (path->GetLabel() != label) ||
                (path->GetL3Label() != l3_label)) {
                // Update Attributes and notify (if needed)
                if (path->NeedsResolution())
                    path_resolver_->StopPathResolution(root->index(), path);
                rt->DeletePath(path);
            } else {
                // Ignore duplicate update.
                break;
            }
        }

        BgpPath *new_path;
        new_path = new BgpPath(
            peer, path_id, BgpPath::BGP_XMPP, attrs, flags, label, l3_label);

        if (new_path->NeedsResolution()) {
            Address::Family family = new_path->GetAttr()->nexthop_family();
            BgpTable *table = rtinstance_->GetTable(family);
            path_resolver_->StartPathResolution(rt, new_path, table);
        }
        rt->InsertPath(new_path);
        notify_rt = true;
        break;
    }

    case DBRequest::DB_ENTRY_DELETE: {
        if (rt && !rt->IsDeleted()) {
            BGP_LOG_ROUTE(this, const_cast<IPeer *>(peer), rt,
                          "Delete BGP path");

            // Remove the Path from the route
            if (path->NeedsResolution())
                path_resolver_->StopPathResolution(root->index(), path);
            rt->RemovePath(BgpPath::BGP_XMPP, peer, path_id);
            notify_rt = true;
        }
        break;
    }

    default: {
        assert(false);
        break;
    }
    }
    return notify_rt;
}

void BgpTable::Input(DBTablePartition *root, DBClient *client,
                     DBRequest *req) {
    const IPeer *peer = (static_cast<RequestKey *>(req->key.get()))->GetPeer();
    RequestData *data = static_cast<RequestData *>(req->data.get());

    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE && peer) {
        // Skip if this peer is down.
        if (!peer->IsReady())
            return;

        // For xmpp peers, verify that agent is subscribed to the table and
        // the route add is from the same incarnation of table subscription.
        if (peer->IsXmppPeer() && peer->IsRegistrationRequired()) {
            BgpMembershipManager *mgr = rtinstance_->server()->membership_mgr();
            int instance_id = -1;
            uint64_t subscription_gen_id = 0;
            bool is_registered =
                mgr->GetRegistrationInfo(const_cast<IPeer *>(peer), this,
                                         &instance_id, &subscription_gen_id);
            if ((!is_registered && (family() != Address::RTARGET)) ||
                (is_registered &&
                 (subscription_gen_id != data->subscription_gen_id()))) {
                return;
            }
        }
    }

    // Create route if it's not already present in case of add/change.
    BgpRoute *rt = TableFind(root, req->key.get());
    if (!rt) {
        if ((req->oper == DBRequest::DB_ENTRY_DELETE) ||
            (req->oper == DBRequest::DB_ENTRY_NOTIFY))
            return;

        rt = static_cast<BgpRoute *>(Add(req));
        static_cast<DBTablePartition *>(root)->Add(rt);
        BGP_LOG_ROUTE(this, const_cast<IPeer *>(peer), rt,
                      "Insert new BGP path");
    }

    if (req->oper == DBRequest::DB_ENTRY_NOTIFY) {
        root->Notify(rt);
        return;
    }

    uint32_t path_id = 0;
    uint32_t flags = 0;
    uint32_t label = 0;
    uint32_t l3_label = 0;
    BgpPath *path = rt->FindPath(peer);
    BgpAttrPtr attr = data ? data->attrs() : NULL;
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        assert(data);
        const RequestData::NextHop &nexthop = data->nexthop();
        if (nexthop.address_.is_v4())
            path_id = nexthop.address_.to_v4().to_ulong();
        flags = nexthop.flags_;
        label = nexthop.label_;
        l3_label = nexthop.l3_label_;

        attr = GetAttributes(rt, attr, peer);
    } else {
        if (!path)
            return;
        path_id = path->GetPathId();
    }

    bool notify_rt = InputCommon(root, rt, path, peer, req, req->oper,
        attr, path_id, flags, label, l3_label);
    InputCommonPostProcess(root, rt, notify_rt);
}

void BgpTable::InputCommonPostProcess(DBTablePartBase *root,
                                      BgpRoute *rt, bool notify_rt) {
    if (!notify_rt)
        return;

    if (rt->front() == NULL)
        root->Delete(rt);
    else
        root->Notify(rt);
}

bool BgpTable::MayDelete() const {
    CHECK_CONCURRENCY("bgp::Config");

    // Bail if the table has listeners.
    if (HasListeners()) {
        BGP_LOG_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                      "Paused table deletion due to pending listeners");
        return false;
    }

    // Bail if the table has walkers.
    if (HasWalkers()) {
        BGP_LOG_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                      "Paused table deletion due to pending walkers");
        return false;
    }

    // Bail if the table is not empty.
    size_t size = Size();
    if (size > 0) {
        BGP_LOG_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                      "Paused table deletion due to " << size <<
                      " pending routes");
        return false;
    }

    // Check the base class at the end so that we add custom checks
    // before this if needed and to get more informative log message.
    if (!DBTableBase::MayDelete())
        return false;

    return true;
}

void BgpTable::Shutdown() {
    CHECK_CONCURRENCY("bgp::PeerMembership", "bgp::Config");
}

void BgpTable::ManagedDelete() {
    BGP_LOG_TABLE(this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                  "Received request for table deletion");
    deleter_->Delete();
}

//
// Retry deletion of the table if it is pending.
//
void BgpTable::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

PathResolver *BgpTable::CreatePathResolver() {
    return NULL;
}

void BgpTable::LocatePathResolver() {
    if (path_resolver_)
        return;
    assert(!deleter()->IsDeleted());
    path_resolver_ = CreatePathResolver();
}

void BgpTable::DestroyPathResolver() {
    if (!path_resolver_)
        return;
    delete path_resolver_;
    path_resolver_ = NULL;
}

size_t BgpTable::GetPendingRiboutsCount(size_t *markers) const {
    CHECK_CONCURRENCY("bgp::ShowCommand", "bgp::Config");
    size_t count = 0;
    *markers = 0;

    BOOST_FOREACH(const RibOutMap::value_type &value, ribout_map_) {
        const RibOut *ribout = value.second;
        for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
            const RibOutUpdates *updates = ribout->updates(idx);
            for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                 ++qid) {
                count += updates->queue_size(qid);
                *markers += updates->queue_marker_count(qid);
            }
        }
    }

    return count;
}

LifetimeActor *BgpTable::deleter() {
    return deleter_.get();
}

const LifetimeActor *BgpTable::deleter() const {
    return deleter_.get();
}

void BgpTable::UpdatePathCount(const BgpPath *path, int count) {
    if (dynamic_cast<const BgpSecondaryPath *>(path)) {
        secondary_path_count_ += count;
    } else {
        primary_path_count_ += count;
    }

    if (!path->IsFeasible()) {
        infeasible_path_count_ += count;
    }

    if (path->IsStale()) {
        stale_path_count_ += count;
    }

    if (path->IsLlgrStale()) {
        llgr_stale_path_count_ += count;
    }
}

// Check whether the route is aggregate route
bool BgpTable::IsAggregateRoute(const BgpRoute *route) const {
    return routing_instance()->IsAggregateRoute(this, route);
}

// Check whether the route is contributing route to aggregate route
bool BgpTable::IsContributingRoute(const BgpRoute *route) const {
    return routing_instance()->IsContributingRoute(this, route);
}

void BgpTable::FillRibOutStatisticsInfo(
    vector<ShowRibOutStatistics> *sros_list) const {
    BOOST_FOREACH(const RibOutMap::value_type &value, ribout_map_) {
        const RibOut *ribout = value.second;
        ribout->FillStatisticsInfo(sros_list);
    }
}
