/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_table.h"

#include <boost/foreach.hpp>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/routing-instance/iroute_aggregator.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "net/community_type.h"

using std::map;
using std::make_pair;
using std::ostringstream;
using std::string;

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

//
// Process Remove Private information.
//
void BgpTable::ProcessRemovePrivate(const RibOut *ribout, BgpAttr *attr) const {
    if (!ribout->IsEncodingBgp())
        return;
    if (!ribout->remove_private_enabled())
        return;

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

    AsPathSpec *new_spec = spec.RemovePrivate(all, replace_asn, peer_asn);
    attr->set_as_path(new_spec);
    delete new_spec;
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
            if (peer && peer->PeerType() == BgpProto::IBGP)
                return NULL;

            // Handle route-target filtering.
            if (attr->ext_community() != NULL) {
                server()->rtarget_group_mgr()->GetRibOutInterestedPeers(
                    ribout, attr->ext_community(), peerset, &new_peerset);
                if (new_peerset.empty())
                    return NULL;
            }

            clone = new BgpAttr(*attr);

            // Retain LocalPref value if set, else set default to 100.
            if (clone->local_pref() == 0)
                clone->set_local_pref(100);

            // Should not normally be needed for iBGP, but there could be
            // complex configurations where this is useful.
            ProcessRemovePrivate(ribout, clone);

            // If the route is locally originated i.e. there's no AsPath,
            // then generate a Nil AsPath i.e. one with 0 length. No need
            // to modify the AsPath if it already exists since this is an
            // iBGP RibOut.
            if (clone->as_path() == NULL) {
                AsPathSpec as_path;
                clone->set_as_path(&as_path);
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
                if (attr->as_path() &&
                    attr->as_path()->path().AsPathLoop(ribout->peer_as())) {
                    return NULL;
                }
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
            if (clone->med() && clone->as_path() && !clone->as_path()->empty())
                clone->set_med(0);

            as_t local_as =
                clone->attr_db()->server()->local_autonomous_system();

            // Override the peer AS with local AS in AsPath.
            if (ribout->as_override() && clone->as_path() != NULL) {
                const AsPathSpec &as_path = clone->as_path()->path();
                AsPathSpec *as_path_ptr =
                    as_path.Replace(ribout->peer_as(), local_as);
                clone->set_as_path(as_path_ptr);
                delete as_path_ptr;
            }

            // Remove private processing must happen before local AS prepend.
            ProcessRemovePrivate(ribout, clone);

            // Prepend the local AS to AsPath.
            if (clone->as_path() != NULL) {
                const AsPathSpec &as_path = clone->as_path()->path();
                AsPathSpec *as_path_ptr = as_path.Add(local_as);
                clone->set_as_path(as_path_ptr);
                delete as_path_ptr;
            } else {
                AsPathSpec as_path;
                AsPathSpec *as_path_ptr = as_path.Add(local_as);
                clone->set_as_path(as_path_ptr);
                delete as_path_ptr;
            }
        }

        assert(clone);
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
        DBRequest::DB_ENTRY_DELETE, NULL, path->GetPathId(), 0, 0);
}

bool BgpTable::InputCommon(DBTablePartBase *root, BgpRoute *rt, BgpPath *path,
                           const IPeer *peer, DBRequest *req,
                           DBRequest::DBOperation oper, BgpAttrPtr attrs,
                           uint32_t path_id, uint32_t flags, uint32_t label) {
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
                (path->GetLabel() != label)) {
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
        new_path =
            new BgpPath(peer, path_id, BgpPath::BGP_XMPP, attrs, flags, label);

        if (new_path->NeedsResolution()) {
            Address::Family family = new_path->GetAttr()->nexthop_family();
            BgpTable *table = rtinstance_->GetTable(family);
            path_resolver_->StartPathResolution(root->index(), new_path, rt,
                table);
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
    const IPeer *peer =
        (static_cast<RequestKey *>(req->key.get()))->GetPeer();

    RequestData *data = static_cast<RequestData *>(req->data.get());
    // Skip if this peer is down
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE && peer) {
        if (!peer->IsReady()) {
            return;
        }
        //
        // For XMPP peer, verify that agent is subscribed to the VRF
        // and route add is from the same incarnation of VRF subscription
        //
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

    BgpRoute *rt = TableFind(root, req->key.get());
    BgpPath *path = NULL;

    // First mark all paths from this request source as deleted.
    // Apply all paths provided in this request data and add them. If path
    // already exists, reset from it getting deleted. Finally walk the paths
    // list again to purge any stale paths originated from this peer.

    // Create rt if it is not already there for adds/updates.
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

    // Use a map to mark and sweep deleted paths, update the rest.
    map<BgpPath *, bool> deleted_paths;

    // Mark this peer's all paths as deleted.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());

        // Skip resolved paths.
        if (path->IsResolved())
            continue;

        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(path))
            continue;

        if (path->GetPeer() == peer && path->GetSource() == BgpPath::BGP_XMPP) {
            deleted_paths.insert(make_pair(path, true));
        }
    }

    int count = 0;
    ExtCommunityDB *extcomm_db = rtinstance_->server()->extcomm_db();
    BgpAttrPtr attr = data ? data->attrs() : NULL;
    bool notify_rt = false;

    // Process each of the paths sourced and create/update paths accordingly.
    if (data) {
        for (RequestData::NextHops::iterator iter = data->nexthops().begin(),
             next = iter;
             iter != data->nexthops().end(); iter = next, ++count) {
            next++;
            RequestData::NextHop nexthop = *iter;

            // Don't support multi path with v6 nexthops for the time being.
            uint32_t path_id = 0;
            if (nexthop.address_.is_v4()) {
                path_id = nexthop.address_.to_v4().to_ulong();
            } else if (count > 0) {
                break;
            }

            path = rt->FindPath(BgpPath::BGP_XMPP, peer, path_id);
            deleted_paths.erase(path);

            if (data->attrs() && count > 0) {
                BgpAttr *clone = new BgpAttr(*data->attrs());
                clone->set_ext_community(
                    extcomm_db->ReplaceTunnelEncapsulationAndLocate(
                        clone->ext_community(),
                        nexthop.tunnel_encapsulations_));
                clone->set_nexthop(nexthop.address_);
                clone->set_source_rd(nexthop.source_rd_);
                attr = data->attrs()->attr_db()->Locate(clone);
            }

            notify_rt |= InputCommon(root, rt, path, peer, req, req->oper,
                                     attr, path_id, nexthop.flags_,
                                     nexthop.label_);
        }
    }

    // Flush remaining paths that remain marked for deletion.
    for (map<BgpPath *, bool>::iterator it = deleted_paths.begin();
         it != deleted_paths.end(); it++) {
        BgpPath *path = it->first;
        notify_rt |= InputCommon(root, rt, path, peer, req,
                                 DBRequest::DB_ENTRY_DELETE, NULL,
                                 path->GetPathId(), 0, 0);
    }

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
