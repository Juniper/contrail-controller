/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer_membership.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <tbb/mutex.h>

#include "base/task_annotations.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "bgp/bgp_export.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/scheduling_group.h"
#include "db/db.h"

using namespace std;

int PeerRibMembershipManager::membership_task_id_ = -1;
const int PeerRibMembershipManager::kMembershipTaskInstanceId;

MembershipRequest::MembershipRequest() {
    action_mask = INVALID;
    ipeer = NULL;
    instance_id = -1;
}

//
// Return BgpTableClose::CloseType enum in string form
//
std::string MembershipRequest::ActionMaskToString(Action action_mask) {
    std::ostringstream action_str;

    if (action_mask & RIBIN_ADD) {
        action_str << "RibInAdd, ";
    }
    if (action_mask & RIBIN_DELETE) {
        action_str << "RibInDelete, ";
    }
    if (action_mask & RIBIN_STALE) {
        action_str << "RibInStale, ";
    }
    if (action_mask & RIBIN_SWEEP) {
        action_str << "RibInSweep, ";
    }
    if (action_mask & RIBOUT_ADD) {
        action_str << "RibOutAdd, ";
    }
    if (action_mask & RIBOUT_DELETE) {
        action_str << "RibOutDelete, ";
    }

    return action_str.str();
}

//
// Return IPeerRibEvent::EventType enum in string form
//
std::string IPeerRibEvent::EventTypeToString(EventType event_type) {
    static const std::map<EventType, std::string> ToString =
        boost::assign::map_list_of
            (REGISTER_RIB, "RegisterRib") 
            (REGISTER_RIB_COMPLETE, "RegisterRibComplete") 
            (UNREGISTER_RIB, "UnregisterRib") 
            (UNREGISTER_RIB_COMPLETE, "UnregisterRibComplete") 
            (UNREGISTER_PEER, "UnregisterPeer")
            (UNREGISTER_PEER_COMPLETE, "UnregisterPeerComplete");

    return ToString.find(event_type)->second;
}

IPeerRib::IPeerRib(
    IPeer *ipeer, BgpTable *table, PeerRibMembershipManager *membership_mgr)
    : ipeer_(ipeer),
      table_(table),
      membership_mgr_(membership_mgr),
      ribout_(NULL),
      table_delete_ref_(this, NULL),
      ribin_registered_(false),
      ribout_registered_(false),
      stale_(false),
      instance_id_(-1) {
    if (membership_mgr != NULL) {
        LifetimeActor *deleter = table ? table->deleter() : NULL;
        if (deleter) {
            assert(!deleter->IsDeleted());
            table_delete_ref_.Reset(deleter);
        }
    }
}

IPeerRib::~IPeerRib() {
}

//
// Implement operator< for IPeerRib.  Compares the names for the underlying
// IPeers, and if they are the same, compares the BgpTable pointers (TODO:
// should we compare the table names instead?
//
bool IPeerRib::operator<(const IPeerRib &rhs) const {
    // TBD: revert back to comparing the names returned by ToString()
    if (ipeer_ < rhs.ipeer_) return true;
    if (ipeer_ > rhs.ipeer_) return false;

    if (table_ < rhs.table_) return true;
    if (table_ > rhs.table_) return false;

    return false;
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Register RibIn with this IPeerRib. There is nothing much to be done at the
// moment other than to note down that registration has been done
//
void IPeerRib::RegisterRibIn() {
    SetRibInRegistered(true);
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Unregister RibIn from this IPeerRib. There is nothing much to be done at the
// moment other than to note down that unregistration has been done
//
void IPeerRib::UnregisterRibIn() {
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Check whether RibIn has been registered in this IPeerRib
bool IPeerRib::IsRibInRegistered() {
    return ribin_registered_;
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Set/Reset RibIn registered flag
void IPeerRib::SetRibInRegistered(bool set) {
    ribin_registered_ = set;
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Per prefix based RibIn specific action necessary shall be performed here.
// At the moment, we don't have any thing to do
//
void IPeerRib::RibInJoin(DBTablePartBase *root, DBEntryBase *db_entry,
                         BgpTable *table,
                         MembershipRequest::Action action_mask) {
    return;
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Close RibIn of this IPeerRib for a particular prefix. Based on the action
// we may mark the route as stale, sweep the route if it is stale or just
// delete the route altogether.
//
void IPeerRib::RibInLeave(DBTablePartBase *root, DBEntryBase *db_entry,
                          BgpTable *table,
                          MembershipRequest::Action action_mask) {
    BgpRoute *rt = static_cast<BgpRoute *>(db_entry);

    if (!IsRibInRegistered()) return;
    ipeer_->peer_close()->close_manager()->
        ProcessRibIn(root, rt, table, action_mask);
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Register the IPeer to the BgpTable.  Creates the RibOut itself if needed
// and registers the RibOut as a listener for the BgpTable.
//
// Registers the IPeer to the RibOut. Also joins the IPeer to the UPDATE and
// BULK queues for the RibOutUpdates associated with the RibOut. Finally, post
// a JOIN event to the BGP peer membership task to cause a table walk to get
// kicked off.
//
void IPeerRib::RegisterRibOut(RibExportPolicy policy) {
    BgpServer *server = membership_mgr_->server();
    SchedulingGroupManager *mgr = server->scheduling_group_manager();
    ribout_ = table_->RibOutLocate(mgr, policy);
    ribout_->RegisterListener();
    ribout_->Register(ipeer_);

    RibOutUpdates *updates = ribout_->updates();
    updates->QueueJoin(RibOutUpdates::QUPDATE, ribout_->GetPeerIndex(ipeer_));
    updates->QueueJoin(RibOutUpdates::QBULK, ribout_->GetPeerIndex(ipeer_));

    SetRibOutRegistered(true);
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Unregister the IPeer from the BgpTable.  First leave the IPeer from the
// UPDATE and BULK queues for the RibOutUpdates associated with the RibOut.
// Then unregister the IPeer from the RibOut, which may result in deletion
// of the RibOut itself.
//
void IPeerRib::UnregisterRibOut() {
    RibOutUpdates *updates = ribout_->updates();
    updates->QueueLeave(RibOutUpdates::QUPDATE, ribout_->GetPeerIndex(ipeer_));
    updates->QueueLeave(RibOutUpdates::QBULK, ribout_->GetPeerIndex(ipeer_));

    ribout_->Unregister(ipeer_);
    ribout_ = NULL;
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Deactivate the IPeer in the RibOut. This ensures that the peer will stop
// exporting routes from now onwards.
//
void IPeerRib::DeactivateRibOut() {
    if (IsRibOutRegistered()) {
        ribout_->Deactivate(ipeer_);
    }
}

bool IPeerRib::IsRibOutRegistered() const {
    return ribout_registered_;
}

bool IPeerRib::IsRibOutActive() const {
    if (IsRibOutRegistered()) {
        return ribout_->IsActive(ipeer_);
    } else {
        return false;
    }
}

//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
// Set/Reset RibOut registered flag
void IPeerRib::SetRibOutRegistered(bool set) {
    ribout_registered_ = set;
}

//
// Process RibOut creation for a particular prefix
//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
void IPeerRib::RibOutJoin(DBTablePartBase *root, DBEntryBase *db_entry,
                          BgpTable *table,
                          MembershipRequest::Action action_mask) {
    if (!(action_mask & MembershipRequest::RIBOUT_ADD)) {
        return;
    }

    RibPeerSet jmask;

    //TODO: For peers with the same export policy, we could combine and
    // launch one BgpExport::Join
    jmask.set(ribout_->GetPeerIndex(ipeer_));
    ribout_->bgp_export()->Join(root, jmask, db_entry);
}

//
// Process RibOut deletion for a particular prefix
//
// Concurrency: Runs in the context of db-walker launched from the BGP peer
// membership task.
//
void IPeerRib::RibOutLeave(DBTablePartBase *root, DBEntryBase *db_entry,
                           BgpTable *table,
                           MembershipRequest::Action action_mask) {
    if (!(action_mask & MembershipRequest::RIBOUT_DELETE)) {
        return;
    }

    RibPeerSet lmask;
    lmask.set(ribout_->GetPeerIndex(ipeer_));

    ribout_->bgp_export()->Leave(root, lmask, db_entry);
}

void IPeerRib::ManagedDelete() {

    //
    // TODO: We let IPeerRib to be deleted as part of the peer cleanups. Once
    // all the IPeerRibs are deleted, the table gets deleted automatically
    //
    // membership_mgr_->Unregister(ipeer_, table_);
}

//
// Constructor for the PeerMembershipMgr. Create the peer membership task if
// required.  Also create a WorkQueue to handle IPeerRibEvents.
//
PeerRibMembershipManager::PeerRibMembershipManager(BgpServer *server) :
        server_(server) {
    if (membership_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        membership_task_id_ = scheduler->GetTaskId("bgp::PeerMembership");
    }

    event_queue_ = new WorkQueue<IPeerRibEvent *>(
        membership_task_id_, kMembershipTaskInstanceId,
        boost::bind(&PeerRibMembershipManager::IPeerRibEventCallback, this,
                    _1));
}

//
// Destructor for PeerMembershipMgr. Delete the WorkQueue of IPeerRibEvents.
//
PeerRibMembershipManager::~PeerRibMembershipManager() {
    delete event_queue_;
}

int PeerRibMembershipManager::RegisterPeerRegistrationCallback(
    PeerRegistrationCallback callback) {
    tbb::mutex::scoped_lock lock(registration_mutex_);

    size_t id = registration_bmap_.find_first();
    if (id == registration_bmap_.npos) {
        id = registration_callbacks_.size();
        registration_callbacks_.push_back(callback);
    } else {
        registration_bmap_.reset(id);
        if (registration_bmap_.none()) {
            registration_bmap_.clear();
        }
        registration_callbacks_[id] = callback;
    }
    return id;
}

void PeerRibMembershipManager::UnregisterPeerRegistrationCallback(int id) {
    tbb::mutex::scoped_lock lock(registration_mutex_);

    registration_callbacks_[id] = NULL;
    if ((size_t) id == registration_callbacks_.size() - 1) {
        while (!registration_callbacks_.empty() &&
               registration_callbacks_.back() == NULL) {
            registration_callbacks_.pop_back();
        }
        if (registration_bmap_.size() > registration_callbacks_.size()) {
            registration_bmap_.resize(registration_callbacks_.size());
        }
    } else {
        if ((size_t) id >= registration_bmap_.size()) {
            registration_bmap_.resize(id + 1);
        }
        registration_bmap_.set(id);
    }
}

void PeerRibMembershipManager::NotifyPeerRegistration(IPeer *ipeer,
    BgpTable *table, bool unregister) {
    tbb::mutex::scoped_lock lock(registration_mutex_);

    for (PeerRegistrationListenerList::iterator iter =
         registration_callbacks_.begin();
         iter != registration_callbacks_.end(); ++iter) {
        if (*iter != NULL) {
            PeerRegistrationCallback callback = *iter;
            (callback)(ipeer, table, unregister);
        }
    }
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Start the process to join the Ipeer to the BgpTable. This consists mainly
// of starting a table walk so that all existing routes can be advertised to
// the IPeer.
//
// Assumes that the IPeer is already registered with the BgpTable.
//
void PeerRibMembershipManager::Join(BgpTable *table,
                                    MembershipRequestList *request_list) {
    DB *db = table->database();
    DBTableWalker *walker = db->GetWalker();

    walker->WalkTable(table, NULL,
        // _1: DBTablePartition, _2: DBEntry
        boost::bind(&PeerRibMembershipManager::RouteJoin, this, _1, _2, table,
                    request_list),

        // _1: DBTablePartition
        boost::bind(&PeerRibMembershipManager::JoinDone, this, _1,
                    request_list));
}

//
// Concurrency: Runs in the context of the db walker task triggered from
// BGP peer membership task.
//
// Handle RibIna and RibOut join for a particular prefix to a set of peers
//
bool PeerRibMembershipManager::RouteJoin(DBTablePartBase *root,
                                         DBEntryBase *db_entry, BgpTable *table,
                                         MembershipRequestList *request_list) {

    // Iterate through each of the peers in the request list and process RibIn
    // and RibOut for this peer
    //
    // TODO: RibOut can be aggregated into a single call for all peers with the
    // same export policy
    for (MembershipRequestList::iterator iter = request_list->begin();
             iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();

        IPeerRib  *peer_rib = IPeerRibFind(request->ipeer, table);

        if (peer_rib) {
            peer_rib->RibInJoin(root, db_entry, table, request->action_mask);
            peer_rib->RibOutJoin(root, db_entry, table, request->action_mask);
        }
    }

    return true;
}

//
// Concurrency: Runs in the context of the DB partition task.
//
// Process the table walk done notification from the DB infrastructure. This
// table walk was started from the Join method.
//
void PeerRibMembershipManager::JoinDone(DBTableBase *db,
                                        MembershipRequestList *request_list) {
    BgpTable *table = static_cast<BgpTable *>(db);

    IPeerRibEvent *event =
        new IPeerRibEvent(IPeerRibEvent::REGISTER_RIB_COMPLETE, NULL, table,
                          request_list);
    Enqueue(event);
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Kick off the process to unregister the IPeer from the BgpTable. We first
// need to do a table walk and clean up state for all routes from the IPeer.
// We can actually unregister only after the state has been cleaned up.
//
// In the meantime, we deactivate the IPeer in the RibOut to ensure that it
// does not export any more routes.
//
void PeerRibMembershipManager::Leave(BgpTable *table,
                              MembershipRequestList *request_list) {

    DB *db = table->database();

    for (MembershipRequestList::iterator iter = request_list->begin();
             iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();

        if (!(request->action_mask & MembershipRequest::RIBOUT_DELETE)) {
            continue;
        }

        IPeerRib *peer_rib = IPeerRibFind(request->ipeer, table);

        if (peer_rib) {

            // 
            // Ignore peer ribs which are already in close process
            //
            if (peer_rib->IsRibOutActive()) peer_rib->DeactivateRibOut();
        }
    }

    DBTableWalker *walker = db->GetWalker();
    walker->WalkTable(table, NULL,
        // _1: DBTablePartBase, _2: DBEntry
        boost::bind(&PeerRibMembershipManager::RouteLeave, this, _1, _2, table,
                    request_list),
        // _1: DBTableBase
        boost::bind(&PeerRibMembershipManager::LeaveDone, this, _1,
                    request_list));
}

//
// Concurrency: Runs in the context of the db walker task triggered from
// BGP peer membership task.
//
// Leave the route from RibIn and from RibOut
//
bool PeerRibMembershipManager::RouteLeave(DBTablePartBase *root,
                                          DBEntryBase *db_entry,
                                          BgpTable *table,
                                          MembershipRequestList *request_list) {
    for (MembershipRequestList::iterator iter = request_list->begin();
             iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();

        IPeerRib  *peer_rib = IPeerRibFind(request->ipeer, table);
        if (peer_rib == NULL) {
            continue;
        }
        peer_rib->RibOutLeave(root, db_entry, table, request->action_mask);
        peer_rib->RibInLeave(root, db_entry, table, request->action_mask);
    }
    return true;
}

void PeerRibMembershipManager::MembershipRequestListDebug(
    const char *function, int line, BgpTable *table,
    MembershipRequestList *request_list) {

    std::ostringstream ostream;

    ostream << "";
    for (MembershipRequestList::iterator iter = (request_list)->begin();
            iter != (request_list)->end(); iter++) {
        MembershipRequest *request = iter.operator->();
        ostream << request->ipeer->ToString() << ", ";
    }
}

//
// Concurrency: Runs in the context of the DB partition task.
//
// Process the table walk done notification from the DB infrastructure.  This
// walk was started from the Leave method. Since the table walk is complete,
// we can post an IPeerRib UNREGISTER_RIB event for the BGP peer membership
// task.
//
void PeerRibMembershipManager::LeaveDone(DBTableBase *db,
                                         MembershipRequestList *request_list) {
    BgpTable *table = static_cast<BgpTable *>(db);

    IPeerRibEvent *event =
        new IPeerRibEvent(IPeerRibEvent::UNREGISTER_RIB_COMPLETE, NULL, table,
                          request_list);
    Enqueue(event);
}

//
// Process Register/Unregister request for a peer with a particular rib
//
IPeerRibEvent *PeerRibMembershipManager::ProcessRequest(
        IPeerRibEvent::EventType event_type, BgpTable *table,
        const MembershipRequest &request) {
    TableMembershipRequestMap *request_map;

    if (event_type == IPeerRibEvent::REGISTER_RIB) {
        request_map = &register_request_map_;
    } else {
        request_map = &unregister_request_map_;
    }

    //
    // Check whether a request in this table is still pending to be processed.
    // If so, just append this new request to the list of pending requests
    //
    // All such requests will be collected and handled in one db walk
    //
    TableMembershipRequestMap::iterator it = request_map->find(table);

    // Check if there is already a pending task. If so, just append this
    // peer to the list
    if (it != request_map->end()) {

        //
        // A similar request for other peers is pending in this table.
        //
        MembershipRequestList *request_list = it->second;
        if (!request_list) {
            it->second = request_list = new MembershipRequestList();
        }
        for (MembershipRequestList::iterator rqiter = request_list->begin();
             rqiter != request_list->end(); ++rqiter) {
            if (rqiter->ipeer == request.ipeer &&
                rqiter->action_mask == request.action_mask) {
                return NULL;
            }
        }
        request_list->push_back(request);

        return NULL;
    }

    //
    // Create a new list, and enqueue an event with this peer list
    //
    MembershipRequestList *request_list = new MembershipRequestList();
    request_list->push_back(request);

    request_map->insert(std::make_pair(table, request_list));

    IPeerRibEvent *event = new IPeerRibEvent(event_type, NULL, table);
    event->request_list = request_list;

    return event;
}

//
// Concurrency: Runs in the context of the BGP state machine task.
//
// Register the IPeer to the BgpTable. We post a REGISTER_RIB event to the BGP
// peer membership task to deal with concurrency issues.
//
void PeerRibMembershipManager::Register(
        IPeer *ipeer, BgpTable *table, const RibExportPolicy &policy,
        int instance_id, NotifyCompletionFn notify_completion_fn) {
    MembershipRequest request;
    
    request.ipeer = ipeer;
    request.action_mask = static_cast<MembershipRequest::Action>(
        MembershipRequest::RIBIN_ADD | MembershipRequest::RIBOUT_ADD);
    request.instance_id = instance_id;
    request.policy = policy;
    request.notify_completion_fn = notify_completion_fn;

    tbb::mutex::scoped_lock lock(mutex_);
    IPeerRibEvent *event = ProcessRequest(IPeerRibEvent::REGISTER_RIB, table,
                                          request);

    if (event) {
        Enqueue(event);
    }
}

//
// Concurrency: Runs in the context of the BGP state machine task.
//
// Unregister the IPeer from the BgpTable. We first post a LEAVE event to
// the BGP peer membership task, which will start a table walk. Once that's
// done we will post a UNREGISTER_RIB event to the BGP peer membership task.
//
void PeerRibMembershipManager::Unregister(IPeer *ipeer, BgpTable *table,
                                          MembershipRequest::NotifyCompletionFn
                                              notify_completion_fn) {
    MembershipRequest request;
    request.ipeer = ipeer;
    request.action_mask = static_cast<MembershipRequest::Action>(
        MembershipRequest::RIBIN_DELETE | MembershipRequest::RIBOUT_DELETE);
    request.notify_completion_fn = notify_completion_fn;
    
    tbb::mutex::scoped_lock lock(mutex_);

    IPeerRibEvent *event = ProcessRequest(IPeerRibEvent::UNREGISTER_RIB,
                                          table, request);
    if (event) {
        Enqueue(event);
    }
}

//
// Concurrency: Runs in the context of the BGP state machine task.
//
// Unregister request for an ipeer from all the ribs it has registered to.
//
// Enqueue a request to peer rib membership manager
//
void PeerRibMembershipManager::UnregisterPeer(IPeer *ipeer,
        MembershipRequest::ActionGetFn action_get_fn,
        MembershipRequest::NotifyCompletionFn notify_completion_fn) {
    IPeerRibEvent *event = new IPeerRibEvent(IPeerRibEvent::UNREGISTER_PEER,
                                             ipeer, NULL);

    event->request.action_get_fn = action_get_fn;
    event->request.notify_completion_fn = notify_completion_fn;
    Enqueue(event);
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Callback where in we can start unregistering a peer from all of its ribs
//
void PeerRibMembershipManager::UnregisterPeerCallback(IPeerRibEvent *event) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerRibMap::const_iterator it;
    int *count = NULL;

    // Walk the set and find all the ribs this peer has registered to.
    // Unregister the peer from each of those ribs
    for (it = peer_rib_map_.find(event->ipeer); it != peer_rib_map_.end();
            it++) {
        if (it->first != event->ipeer) break;
        IPeerRib *peer_rib = it->second;

        // 
        // We need to track the completion of each of these rib close process
        // before we can inform the requester that unregistration is complete
        // 
        // Use a simple counter to manage this part
        //
        if (!count) count = new int(0);
        ++*count;

        MembershipRequest request;
        request.ipeer = event->ipeer;
        request.action_mask = static_cast<MembershipRequest::Action>(
            event->request.action_get_fn(peer_rib));
        request.notify_completion_fn = boost::bind(
            &PeerRibMembershipManager::UnregisterPeerDone,
            this, _1, _2, count, event->request.notify_completion_fn);

        IPeerRibEvent *process_event;
        process_event = ProcessRequest(IPeerRibEvent::UNREGISTER_RIB,
                                       peer_rib->table(), request);

        //
        // We are already running in membership task. Invoke the callback
        // right away. This is required also to serialize rib membership
        // requests. Unregister peer request should not get mangled with other
        // explicit register/unregister requests
        //
        if (process_event) {
            IPeerRibEventCallbackUnlocked(process_event);
        }
    }

    //
    // If there are no peer_ribs to unregister, notify completion right away
    //
    if (!count) {
        event->request.notify_completion_fn(event->ipeer, NULL);
    }
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Unregister for a peer against a particular rib is complete
//
void PeerRibMembershipManager::UnregisterPeerDone(
        IPeer *ipeer, BgpTable *table, int *count,
        MembershipRequest::NotifyCompletionFn notify_completion_fn) {

    // If there are other rib closes pending, wait
    //
    if (--*count) return;
    delete count;

    //
    // Enqueue a notification to signal the requestor that unregistration
    // of this peer is complete
    //
    IPeerRibEvent *event = new IPeerRibEvent(
            IPeerRibEvent::UNREGISTER_PEER_COMPLETE, ipeer, NULL);
    event->ipeer = ipeer;
    event->table = NULL;
    event->request.notify_completion_fn = notify_completion_fn;
    Enqueue(event);
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// All ribs unregistration for the peer is complete
//
void PeerRibMembershipManager::UnregisterPeerCompleteCallback(
                                   IPeerRibEvent *event) {

    //
    // Inform the requestor (PeerCloseManager) that this process is complete
    //
    event->request.notify_completion_fn(event->ipeer, NULL);
}

void PeerRibMembershipManager::FillRoutingInstanceInfo(
            ShowRoutingInstanceTable &inst,
            const BgpTable *table) const {
    RibPeerMap::const_iterator it;
    std::vector<std::string> peers;
    for (it = rib_peer_map_.find(table); it != rib_peer_map_.end(); it++) {
        if (it->first != table) break;
        peers.push_back(it->second->ToString());
    }

    inst.set_name(table->name());
    inst.set_deleted(table->IsDeleted());
    if (peers.size()) inst.set_peers(peers);
}

//
// Find the IPeerRib corresponding to the given IPeer and BgpTable.
//
IPeerRib *PeerRibMembershipManager::IPeerRibFind(IPeer *ipeer,
                                                 BgpTable *table) {
    IPeerRib peer_rib(ipeer, table, NULL);
    PeerRibSet::iterator iter = peer_rib_set_.find(&peer_rib);
    return (iter != peer_rib_set_.end() ? *iter : NULL);
}

//
// Return the instance-id of the IPeer for the BgpTable.
//
int PeerRibMembershipManager::GetRegistrationId(const IPeer *ipeer,
                                                BgpTable *table) {
    assert(ipeer->IsXmppPeer());
    IPeerRib *peer_rib = IPeerRibFind(const_cast<IPeer *>(ipeer), table);
    return (peer_rib ? peer_rib->instance_id() : -1);
}

void PeerRibMembershipManager::FillPeerMembershipInfo(const IPeer *peer,
        BgpNeighborResp &resp) {
    assert(resp.get_routing_tables().size() == 0);
    IPeer *nc_peer = const_cast<IPeer *>(peer);

    SchedulingGroupManager *sg_mgr = server_->scheduling_group_manager();
    SchedulingGroup *sg = sg_mgr->PeerGroup(nc_peer);
    resp.set_send_state(sg ?
            (sg->PeerInSync(nc_peer) ? "in sync" : "not in sync") :
            "not advertising");
    IPeerRib peer_rib(nc_peer, NULL, this);
    PeerRibMembershipManager::PeerRibSet::iterator it =
        peer_rib_set_.lower_bound(&peer_rib);
    std::vector<BgpNeighborRoutingTable> table_list;
    for (;it != peer_rib_set_.end(); it++) {
        if ((*it)->ipeer() != peer) break;
        BgpNeighborRoutingTable table;
        table.set_name((*it)->table()->name());
        table.set_current_state("subscribed");
        table_list.push_back(table);
    }
    if (table_list.size()) resp.set_routing_tables(table_list);
}

void PeerRibMembershipManager::FillRegisteredTable(IPeer *peer, 
                                               std::vector<std::string> &list) {
    IPeerRib peer_rib(peer, NULL, this);
    PeerRibMembershipManager::PeerRibSet::iterator it =
        peer_rib_set_.lower_bound(&peer_rib);
    for (;it != peer_rib_set_.end(); it++) {
        if ((*it)->ipeer() != peer) break;
        list.push_back((*it)->table()->name());
    }
}
//
// Insert the IPeerRib into the set.
//
IPeerRib *PeerRibMembershipManager::IPeerRibInsert(IPeer *ipeer,
                                                   BgpTable *table) {
    IPeerRib *peer_rib = new IPeerRib(ipeer, table, this);
    rib_peer_map_.insert(std::make_pair(table, ipeer));
    peer_rib_map_.insert(std::make_pair(ipeer, peer_rib));
    peer_rib_set_.insert(peer_rib);
    NotifyPeerRegistration(ipeer, table, false);
    return peer_rib;
}

//
// Remove the IPeerRib from the set.
//
void PeerRibMembershipManager::IPeerRibRemove(IPeerRib *peer_rib) {
    NotifyPeerRegistration(peer_rib->ipeer(), peer_rib->table(), true);
    peer_rib_set_.erase(peer_rib);
    for (RibPeerMap::iterator it = rib_peer_map_.find(peer_rib->table());
         it != rib_peer_map_.end(); it++) {
        if (it->first != peer_rib->table())
            break;
        if (it->second == peer_rib->ipeer()) {
            rib_peer_map_.erase(it);
            break;
        }
    }

    for (PeerRibMap::iterator it2 = peer_rib_map_.find(peer_rib->ipeer());
         it2 != peer_rib_map_.end(); it2++) {
        if (it2->first != peer_rib->ipeer())
            break;
        if (it2->second == peer_rib) {
            peer_rib_map_.erase(it2);
            break;
        }
    }
}

//
// Notify completion of a peer membership manager work request to the caller
//
void PeerRibMembershipManager::NotifyCompletion(BgpTable *table,
                                         MembershipRequestList *request_list) {

    //
    // We batch multiple requests and process them together in a single db
    // walk. Iterate through each request and notify the party appropriately
    //
    for (MembershipRequestList::iterator iter =
            request_list->begin(); iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();
        if (request->notify_completion_fn) {
            request->notify_completion_fn(request->ipeer, table);
        }
    }
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Process REGISTER_RIB event
//
void PeerRibMembershipManager::ProcessRegisterRibEvent(BgpTable *table,
                                   MembershipRequestList *request_list) {

    // Notify completion right away if the table is marked for deletion
    if (table->IsDeleted()) {
        NotifyCompletion(table, request_list);
        delete request_list;
        register_request_map_.erase(table);
        return;
    }

    //
    // Iterate through each request and prepare for walk
    //
    for (MembershipRequestList::iterator iter =
            request_list->begin(); iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();
        IPeerRib *peer_rib = IPeerRibFind(request->ipeer, table);

        assert(request->action_mask != MembershipRequest::INVALID);

        if (!peer_rib) {
            peer_rib = IPeerRibInsert(request->ipeer, table);
            if (request->instance_id > 0) {
                peer_rib->set_instance_id(request->instance_id);
            }
        }

        //
        // Peer has registered to this table. Reset the stale flag
        //
        if (peer_rib->IsStale()) {
            peer_rib->ResetStale();
        }

        BGP_LOG_PEER_TABLE(peer_rib->ipeer(), SandeshLevel::SYS_DEBUG,
                           BGP_LOG_FLAG_SYSLOG, peer_rib->table(),
                           "Register routing-table for " <<
                               MembershipRequest::ActionMaskToString(
                                   request->action_mask));

        //
        // Register RibOut if requested
        //
        if (request->action_mask & MembershipRequest::RIBOUT_ADD) {
            peer_rib->RegisterRibOut(request->policy);
        }

        //
        // Register RibIn if requested
        //
        if (request->action_mask & MembershipRequest::RIBIN_ADD) {
            peer_rib->RegisterRibIn();
        }
    }

    //
    // Start off a walk to process this list of peer registration requests
    //
    Join(table, request_list);
}


// 
// Registration for a set of peers is complete. Notify the caller indicating
// the completion of this process
//
void PeerRibMembershipManager::ProcessRegisterRibCompleteEvent(
    IPeerRibEvent *event) {

    //
    // Iterate through each request and notify completion
    //
    NotifyCompletion(event->table, event->request_list);

    delete event->request_list;
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Process UNREGISTER_RIB event
//
void PeerRibMembershipManager::ProcessUnregisterRibEvent(BgpTable *table,
                                   MembershipRequestList *request_list) {
    bool complete = true;

    //
    // Iterate through each request and prepare for walk
    //
    for (MembershipRequestList::iterator iter =
            request_list->begin(); iter != request_list->end(); iter++) {
        MembershipRequest *request = iter.operator->();
        IPeerRib *peer_rib = IPeerRibFind(request->ipeer, table);
        if (!peer_rib) continue;
        complete = false;

        BGP_LOG_PEER_TABLE(peer_rib->ipeer(), SandeshLevel::SYS_DEBUG,
                           BGP_LOG_FLAG_SYSLOG, peer_rib->table(),
                           "Unregister routing-table for " <<
                               MembershipRequest::ActionMaskToString(
                                   request->action_mask));
    }

    //
    // If there is no walk necessary, inform the caller
    //
    if (complete) {

        // Notify
        NotifyCompletion(table, request_list);
        delete request_list;
        unregister_request_map_.erase(table);
    } else {

        //
        // Kick off the db walk to start the leave process
        //
        Leave(table, request_list);
    }
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Process UNREGISTER_RIB_COMPLETE event
//
void PeerRibMembershipManager::ProcessUnregisterRibCompleteEvent(
        IPeerRibEvent *event) {

    for (MembershipRequestList::iterator iter =
            event->request_list->begin();
            iter != event->request_list->end(); iter++) {

        MembershipRequest *request = iter.operator->();
        IPeerRib *peer_rib = IPeerRibFind(request->ipeer, event->table);

        if (peer_rib) {

            //
            // If there was unregister for RibOut, do some final cleanup
            // necessary
            //
            if (request->action_mask & MembershipRequest::RIBOUT_DELETE) {
                peer_rib->UnregisterRibOut();
                peer_rib->SetRibOutRegistered(false);
            }

            //
            // If there was unregister for RibOut, do some final cleanup
            // necessary
            //
            if (request->action_mask & MembershipRequest::RIBIN_DELETE) {
                peer_rib->UnregisterRibIn();
                peer_rib->SetRibInRegistered(false);
            }

            //
            // If both RibIn and RibOut have been unregistered, go ahead
            // and remove this peer_rib
            //
            if (!peer_rib->IsRibInRegistered() &&
                !peer_rib->IsRibOutRegistered()) {

                // Both RibIn and RibOut unregistration are complete.
                // This peer_rib should now go away.
                IPeerRibRemove(peer_rib);
                delete peer_rib;
            }
        }

        // 
        // Notify the caller indicating the completion this process
        //
        if (request->notify_completion_fn) {
            request->notify_completion_fn(request->ipeer, event->table);
        }
    }

    delete event->request_list;
}

void PeerRibMembershipManager::IPeerRibEventCallbackUnlocked(
        IPeerRibEvent *event) {
    MembershipRequestList *request_list = NULL;
    TableMembershipRequestMap::iterator map_iter;

    switch (event->event_type) {
    case IPeerRibEvent::REGISTER_RIB:

        // Retrieve the the list of requests from the map, for this event's
        // specific table
        map_iter = register_request_map_.find(event->table);
        request_list = map_iter->second;
        map_iter->second = NULL;
        ProcessRegisterRibEvent(event->table, request_list);
        break;

    case IPeerRibEvent::REGISTER_RIB_COMPLETE:
        ProcessRegisterRibCompleteEvent(event);

        // Check if there are any new registrations pending
        map_iter = register_request_map_.find(event->table);
        request_list = map_iter->second;
        if (!request_list) {
            register_request_map_.erase(event->table);
        } else {
            map_iter->second = NULL;
            ProcessRegisterRibEvent(event->table, request_list);
        }
        break;

    case IPeerRibEvent::UNREGISTER_RIB:

        // Check if there are any new registrations pending
        map_iter = unregister_request_map_.find(event->table);
        request_list = map_iter->second;
        map_iter->second = NULL;
        ProcessUnregisterRibEvent(event->table, request_list);
        break;

    case IPeerRibEvent::UNREGISTER_RIB_COMPLETE:

        // Unregistration for a set of peers from a rib is complete
        ProcessUnregisterRibCompleteEvent(event);

        // Check if there are any new registrations pending
        map_iter = unregister_request_map_.find(event->table);
        request_list = map_iter->second;
        if (!request_list) {
            unregister_request_map_.erase(event->table);
        } else {
            map_iter->second = NULL;
            ProcessUnregisterRibEvent(event->table, request_list);
        }
        break;

    case IPeerRibEvent::UNREGISTER_PEER:
        UnregisterPeerCallback(event);
        break;

    case IPeerRibEvent::UNREGISTER_PEER_COMPLETE:
        UnregisterPeerCompleteCallback(event);
        break;

    default:
        break;
    }

    delete event;
}

//
// Concurrency: Runs in the context of the BGP peer membership task.
//
// Event handler for the IPeerRibEvent.
//
bool PeerRibMembershipManager::IPeerRibEventCallback(IPeerRibEvent *event) {
    tbb::mutex::scoped_lock lock(mutex_);

    IPeerRibEventCallbackUnlocked(event);
    return true;
}
