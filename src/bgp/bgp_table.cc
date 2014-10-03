/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_table.h"

#include <boost/foreach.hpp>
#include "base/task_annotations.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "db/db_table_partition.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"

using namespace std;

class BgpTable::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(BgpTable *table)
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

    virtual void Destroy() {
        //
        // Make sure that all notifications have been processed and all db state
        // have been cleared for all partitions, befor we inform the parent 
        // instance that this table deletion process is complete
        //
        table_->rtinstance_->DestroyDBTable(table_);
    }

  private:
    BgpTable *table_;
};

BgpTable::BgpTable(DB *db, const string &name)
        : RouteTable(db, name),
          rtinstance_(NULL),
          instance_delete_ref_(this, NULL) {
    primary_path_count_ = 0;
    secondary_path_count_ = 0;
    infeasible_path_count_ = 0;
}

BgpTable::~BgpTable() {

    // remove the table from the instance dependents before attempting to
    // destroy the DeleteActor which can have its Delete() method be called
    // via the reference.
    instance_delete_ref_.Reset(NULL);
}

// TODO: Fix BgpTable creation to pass in instance argument in the constructor
void BgpTable::set_routing_instance(RoutingInstance *rtinstance) {
    rtinstance_ = rtinstance;
    assert(rtinstance);
    deleter_.reset(new DeleteActor(this));
    instance_delete_ref_.Reset(rtinstance->deleter());
}

BgpServer *BgpTable::server() {
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
RibOut *BgpTable::RibOutLocate(SchedulingGroupManager *mgr,
                               const RibExportPolicy &policy) {
    RibOutMap::iterator loc = ribout_map_.find(policy);
    if (loc == ribout_map_.end()) {
        RibOut *ribout = new RibOut(this, mgr, policy);
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

UpdateInfo *BgpTable::GetUpdateInfo(RibOut *ribout, BgpRoute *route,
        const RibPeerSet &peerset) {
    const BgpPath *path = route->BestPath();

    // Ignore if there is no best-path.
    if (!path)
        return NULL;

    // Don't advertise infeasible paths.
    if (!path->IsFeasible())
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
                if (value == Community::NoAdvertise)
                    return NULL;

                if ((ribout->peer_type() == BgpProto::EBGP) &&
                    ((value == Community::NoExport) ||
                     (value == Community::NoExportSubconfed))) {
                    return NULL;
                }
            }
        }

        if (attr->ext_community() != NULL) {
            // Handle route-target filtering
            rtinstance_->server()->rtarget_group_mgr()->GetRibOutInterestedPeers
                (ribout, attr->ext_community(), peerset, new_peerset);
            if (new_peerset.empty()) return NULL;
        }

        if (ribout->peer_type() == BgpProto::IBGP) {

            // Split horizon check.
            const IPeer *peer = path->GetPeer();
            if (peer && peer->PeerType() == BgpProto::IBGP)
                return NULL;

            BgpAttr *clone = new BgpAttr(*attr);

            // Retain LocalPref value if set, else set default to 100.
            if (attr->local_pref() == 0)
                clone->set_local_pref(100);

            // If the route is locally originated i.e. there's no AsPath,
            // then generate a Nil AsPath i.e. one with 0 length. No need
            // to modify the AsPath if it already exists since this is an
            // iBGP RibOut.
            if (attr->as_path() == NULL) {
                AsPathSpec as_path;
                clone->set_as_path(&as_path);
            }

            attr_ptr = attr->attr_db()->Locate(clone);
            attr = attr_ptr.get();

        } else if (ribout->peer_type() == BgpProto::EBGP) {

            // Sender side AS path loop check.
            if (attr->as_path() &&
                attr->as_path()->path().AsPathLoop(ribout->peer_as())) {
                return NULL;
            }

            BgpAttr *clone = new BgpAttr(*attr);

            // Reset LocalPref and Med.
            if (attr->local_pref() || attr->med()) {
                clone->set_local_pref(0);
                clone->set_med(0);
            }

            // Prepend the local AS to AsPath.
            as_t local_as = attr->attr_db()->server()->autonomous_system();
            if (attr->as_path() != NULL) {
                const AsPathSpec &as_path = attr->as_path()->path();
                AsPathSpec *as_path_ptr = as_path.Add(local_as);
                clone->set_as_path(as_path_ptr);
                delete as_path_ptr;
            } else {
                AsPathSpec as_path;
                AsPathSpec *as_path_ptr = as_path.Add(local_as);
                clone->set_as_path(as_path_ptr);
                delete as_path_ptr;
            }

            attr_ptr = attr->attr_db()->Locate(clone);
            attr = attr_ptr.get();
        }
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

void BgpTable::InputCommon(DBTablePartBase *root, BgpRoute *rt, BgpPath *path,
                           const IPeer *peer, DBRequest *req,
                           DBRequest::DBOperation oper, BgpAttrPtr attrs,
                           uint32_t path_id, uint32_t flags, uint32_t label) {
    bool is_stale = false;

    switch (oper) {
    case DBRequest::DB_ENTRY_ADD_CHANGE: {

        assert(rt);

        // The entry may currently be marked as deleted.
        rt->ClearDelete();

        // Check whether peer already has a path
        if (path != NULL) {
            if ((path->GetAttr() != attrs.get()) ||
                (path->GetFlags() != flags) ||
                (path->GetLabel() != label)) {
                // Update Attributes and notify (if needed)
                is_stale = path->IsStale();
                rt->DeletePath(path);
            } else {

                //
                // Ignore duplicate update
                //
                break;
            }
        }

        BgpPath *new_path;
        new_path = new BgpPath(peer, path_id, BgpPath::BGP_XMPP, attrs, flags, label);

        //
        // If the path is being staled (by bringing down the local pref,
        // mark the same in the new path created
        //
        if (is_stale) {
            new_path->SetStale();
        }

        rt->InsertPath(new_path);
        root->Notify(rt);
        break;
    }

    case DBRequest::DB_ENTRY_DELETE: {
        if (rt && !rt->IsDeleted()) {
            BGP_LOG_ROUTE(this, const_cast<IPeer *>(peer), rt,
                          "Delete BGP path");

            // Remove the Path from the route
            rt->RemovePath(BgpPath::BGP_XMPP, peer, path_id);

            if (rt->front() == NULL) {
                // Delete the route only if all paths are gone
                root->Delete(rt);
            } else {
               root->Notify(rt);
            }
        }
        break;
    }

    default: {
        assert(false);
        break;
    }
    }
}

void BgpTable::Input(DBTablePartition *root, DBClient *client,
                     DBRequest *req) {
    const IPeer *peer =
        (static_cast<RequestKey *>(req->key.get()))->GetPeer();
    // Skip if this peer is down
    if ((req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) && 
        peer && !peer->IsReady())
        return;

    BgpRoute *rt = TableFind(root, req->key.get());
    RequestData *data = static_cast<RequestData *>(req->data.get());
    BgpPath *path = NULL;

    // First mark all paths from this request source as deleted.
    // Apply all paths provided in this request data and add them. If path
    // already exists, reset from it getting deleted. Finally walk the paths
    // list again to purge any stale paths originated from this peer.

    // Create rt if it is not already there for adds/updates.
    if (!rt) {
        if (req->oper == DBRequest::DB_ENTRY_DELETE) return;

        rt = static_cast<BgpRoute *>(Add(req));
        static_cast<DBTablePartition *>(root)->Add(rt);
        BGP_LOG_ROUTE(this, const_cast<IPeer *>(peer), rt,
                      "Insert new BGP path");
    }

    // Use a map to mark and sweep deleted paths, update the rest.
    map<BgpPath *, bool> deleted_paths;

    // Mark this peer's all paths as deleted.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); ++it) {

        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) continue;

        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetPeer() == peer &&
                path->GetSource() == BgpPath::BGP_XMPP) {
            deleted_paths.insert(make_pair(path, true));
        }
    }

    int count = 0;
    ExtCommunityDB *extcomm_db = rtinstance_->server()->extcomm_db();
    BgpAttrPtr attr = data ? data->attrs() : NULL;

    // Process each of the paths sourced and create/update paths accordingly.
    if (data) {
        for (RequestData::NextHops::iterator iter = data->nexthops().begin(),
                next = iter;
                iter != data->nexthops().end(); iter = next, ++count) {
            next++;
            RequestData::NextHop nexthop = *iter;
            path = rt->FindPath(BgpPath::BGP_XMPP, peer,
                                nexthop.address_.to_v4().to_ulong());

            if (path && req->oper != DBRequest::DB_ENTRY_DELETE) {
                if (path->IsStale()) {
                    path->ResetStale();
                }
                deleted_paths.erase(path);
            }
            if (data && data->attrs() && count > 0) {
                BgpAttr *clone = new BgpAttr(*data->attrs());
                clone->set_ext_community(
                    extcomm_db->ReplaceTunnelEncapsulationAndLocate(
                        clone->ext_community(),
                        nexthop.tunnel_encapsulations_));
                clone->set_nexthop(IpAddress(Ip4Address(
                                    nexthop.address_.to_v4().to_ulong())));
                clone->set_source_rd(nexthop.source_rd_);
                attr = data->attrs()->attr_db()->Locate(clone);
            }

            InputCommon(root, rt, path, peer, req, req->oper, attr,
                        nexthop.address_.to_v4().to_ulong(), nexthop.flags_,
                        nexthop.label_);
        }
    }

    // Flush remaining paths that remain marked for deletion.
    for (std::map<BgpPath *, bool>::iterator it = deleted_paths.begin();
            it != deleted_paths.end(); it++) {
        BgpPath *path = it->first;
        InputCommon(root, rt, path, peer, req, DBRequest::DB_ENTRY_DELETE,
                    NULL, path->GetPathId(), 0, 0);
    }
}

bool BgpTable::MayDelete() const {
    CHECK_CONCURRENCY("bgp::Config");

    //
    // The route replicator may be still in the processes of cleaning up
    // the table.
    if (HasListeners()) {
        BGP_LOG_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                      "Paused table deletion due to pending listeners");
        return false;
    }

    //
    // This table cannot be deleted yet if any route is still present.
    //
    size_t size = Size();
    if (size > 0) {
        BGP_LOG_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                      "Paused table deletion due to " << size <<
                      " pending entries");
        return false;
    }
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

size_t BgpTable::GetPendingRiboutsCount(size_t &markers) {
    CHECK_CONCURRENCY("bgp::ShowCommand", "bgp::Config");
    size_t count = 0;
    markers = 0;

    BOOST_FOREACH(RibOutMap::value_type &i, ribout_map_) {
        RibOut *ribout = i.second;
        if (ribout->updates()) {
            BOOST_FOREACH(UpdateQueue *queue, ribout->updates()->queue_vec()) {
                count += queue->size();
                markers += queue->marker_count();
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
}
