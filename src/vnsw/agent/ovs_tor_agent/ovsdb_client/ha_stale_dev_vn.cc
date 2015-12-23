/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_route_peer.h>
#include <ovsdb_client_connection_state.h>
#include <ha_stale_dev_vn.h>
#include <ha_stale_vn.h>
#include <ha_stale_l2_route.h>
#include <oper/physical_device_vn.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_path.h>
#include <oper/bridge_route.h>

#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

HaStaleDevVnEntry::HaStaleDevVnEntry(OvsdbDBObject *table,
        const boost::uuids::uuid &vn_uuid) : OvsdbDBEntry(table),
    vn_uuid_(vn_uuid), l2_table_(NULL), old_l2_table_(NULL),
    oper_bridge_table_(NULL), dev_ip_(), vn_name_(""), vxlan_id_(0) {
}

HaStaleDevVnEntry::~HaStaleDevVnEntry() {
    assert(l2_table_ == NULL);
    assert(old_l2_table_ == NULL);
}

bool HaStaleDevVnEntry::Add() {
    // if table is scheduled for delete, return from here
    // and wait for delete callback
    if (table_->delete_scheduled()) {
        return true;
    }

    bool ret = true;
    // On device ip, vxlan id or Agent Route Table pointer change delete
    // previous route table and add new with updated vxlan id
    if (l2_table_ != NULL &&
        l2_table_->GetDBTable() != oper_bridge_table_) {
        ret = Delete();
    }

    // create route table and register
    if (l2_table_ == NULL) {
        l2_table_ = new HaStaleL2RouteTable(this, oper_bridge_table_);
    }

    // trigger update params for L2 Table to pick new vxlan id / tor ip
    l2_table_->UpdateParams(this);
    return ret;
}

bool HaStaleDevVnEntry::Change() {
    return Add();
}

bool HaStaleDevVnEntry::Delete() {
    // delete route table.
    if (l2_table_ != NULL) {
        l2_table_->DeleteTable();
        assert(old_l2_table_ == NULL);
        old_l2_table_ = l2_table_;
        l2_table_ = NULL;
        return false;
    }

    return true;
}

bool HaStaleDevVnEntry::Sync(DBEntry *db_entry) {
    bool change = false;
    // check if route table is available.
    const PhysicalDeviceVn *dev_vn =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    const VrfEntry *vrf = dev_vn->vn()->GetVrf();
    if (vrf == NULL) {
        // trigger change and wait for VRF
        oper_bridge_table_ = NULL;
        change = true;
    } else if (oper_bridge_table_ != vrf->GetBridgeRouteTable()) {
        oper_bridge_table_ = vrf->GetBridgeRouteTable();
        change = true;
    }
    if (dev_ip_ != dev_vn->tor_ip()) {
        dev_ip_ = dev_vn->tor_ip();
        change = true;
    }
    if (vn_name_ != dev_vn->vn()->GetName()) {
        vn_name_ = dev_vn->vn()->GetName();
        change = true;
    }
    if (vxlan_id_ != (uint32_t)dev_vn->vxlan_id()) {
        vxlan_id_ = dev_vn->vxlan_id();
        change = true;
    }
    return change;
}

bool HaStaleDevVnEntry::IsLess(const KSyncEntry &entry) const {
    const HaStaleDevVnEntry &dev_vn_entry =
        static_cast<const HaStaleDevVnEntry &>(entry);
    return vn_uuid_ < dev_vn_entry.vn_uuid_;
}

KSyncEntry* HaStaleDevVnEntry::UnresolvedReference() {
    HaStaleDevVnTable *table =
        static_cast<HaStaleDevVnTable *>(table_);
    HaStaleVnEntry key(table->vn_table_, vn_uuid_);
    HaStaleVnEntry *entry = static_cast<HaStaleVnEntry*>
        (table->vn_table_->GetReference(&key));
    // Wait for vn-vrf link
    if (!entry->IsResolved()) {
        return entry;
    }

    if (oper_bridge_table_ == NULL) {
        // update route table when vn-vrf link is available
        oper_bridge_table_ = entry->bridge_table();
    }

    if (vn_name_.empty()) {
        // update vn name from vn if available
        vn_name_ = entry->vn_name();
    }
    return NULL;
}

void HaStaleDevVnEntry::TriggerAck(HaStaleL2RouteTable *table) {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    assert(old_l2_table_ == table);
    if (l2_table_ != NULL) {
        old_l2_table_ = NULL;
        object->NotifyEvent(this, KSyncEntry::ADD_ACK);
    } else {
        old_l2_table_ = NULL;
        object->NotifyEvent(this, KSyncEntry::DEL_ACK);
    }
}

Agent *HaStaleDevVnEntry::agent() const {
    const HaStaleDevVnTable *reflector_table =
        static_cast<const HaStaleDevVnTable *>(table_);
    return reflector_table->agent();
}

OvsPeer *HaStaleDevVnEntry::route_peer() const {
    const HaStaleDevVnTable *reflector_table =
        static_cast<const HaStaleDevVnTable *>(table_);
    return reflector_table->route_peer();
}

const std::string &HaStaleDevVnEntry::dev_name() const {
    const HaStaleDevVnTable *reflector_table =
        static_cast<const HaStaleDevVnTable *>(table_);
    return reflector_table->dev_name();
}

ConnectionStateEntry *HaStaleDevVnEntry::state() const {
    const HaStaleDevVnTable *reflector_table =
        static_cast<const HaStaleDevVnTable *>(table_);
    return reflector_table->state();
}

IpAddress HaStaleDevVnEntry::dev_ip() const {
    return dev_ip_;
}

const std::string &HaStaleDevVnEntry::vn_name() const {
    return vn_name_;
}

uint32_t HaStaleDevVnEntry::vxlan_id() const {
    return vxlan_id_;
}

HaStaleDevVnTable::HaStaleDevVnTable(Agent *agent,
        OvsPeerManager *manager, ConnectionStateEntry *state,
        std::string &dev_name) :
    OvsdbDBObject(NULL, false), agent_(agent), manager_(manager),
    dev_name_(dev_name), state_(state),
    vn_table_(new HaStaleVnTable(agent, this)), time_stamp_(1),
    stale_clear_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Route Replicator cleanup timer",
                agent->task_scheduler()->GetTaskId("Agent::KSync"), 0)) {
    vn_reeval_queue_ = new WorkQueue<boost::uuids::uuid>(
            agent->task_scheduler()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&HaStaleDevVnTable::VnReEval, this, _1));
    vn_reeval_queue_->set_name("OVSDB VN re-evaluation queue");
    Ip4Address zero_ip;
    route_peer_.reset(manager->Allocate(zero_ip));
    route_peer_->set_ha_stale_export(true);
    OvsdbRegisterDBTable((DBTable *)agent->physical_device_vn_table());
}

HaStaleDevVnTable::~HaStaleDevVnTable() {
    // Cancel timer if running
    stale_clear_timer_->Cancel();
    TimerManager::DeleteTimer(stale_clear_timer_);
    vn_reeval_queue_->Shutdown();
    delete vn_reeval_queue_;
    manager_->Free(route_peer_.release());
}

KSyncEntry *HaStaleDevVnTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const HaStaleDevVnEntry *k_entry =
        static_cast<const HaStaleDevVnEntry *>(key);
    HaStaleDevVnEntry *entry = new HaStaleDevVnEntry(this, k_entry->vn_uuid_);
    return entry;
}

KSyncEntry *HaStaleDevVnTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const PhysicalDeviceVn *entry =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    HaStaleDevVnEntry *key =
        new HaStaleDevVnEntry(this, entry->vn()->GetUuid());
    return static_cast<KSyncEntry *>(key);
}

KSyncDBObject::DBFilterResp HaStaleDevVnTable::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const PhysicalDeviceVn *dev_vn =
        static_cast<const PhysicalDeviceVn *>(entry);

    // Delete the entry which has invalid VxLAN id associated.
    if (dev_vn->vxlan_id() == 0) {
        return DBFilterDelete;
    }

    // Accept only devices with name matched to dev_name_
    if (dev_vn->device_display_name() != dev_name_) {
        return DBFilterDelete;
    }

    if (vn_table_ == NULL) {
        // delete table delete notify the entry
        return DBFilterDelete;
    }

    return DBFilterAccept;
}

Agent *HaStaleDevVnTable::agent() const {
    return agent_;
}

void HaStaleDevVnTable::DeleteTableDone() {
    if (vn_table_ != NULL) {
        vn_table_->DeleteTable();
        vn_table_ = NULL;
    }
}

void HaStaleDevVnTable::EmptyTable() {
    OvsdbDBObject::EmptyTable();
    // unregister the object if emptytable is called with
    // object being scheduled for delete
    if (delete_scheduled()) {
        KSyncObjectManager::Unregister(this);
    }
}

void HaStaleDevVnTable::VnReEvalEnqueue(const boost::uuids::uuid &vn_uuid) {
    vn_reeval_queue_->Enqueue(vn_uuid);
}

bool HaStaleDevVnTable::VnReEval(const boost::uuids::uuid &vn_uuid) {
    HaStaleDevVnEntry key(this, vn_uuid);
    HaStaleDevVnEntry *entry =
        static_cast<HaStaleDevVnEntry*>(Find(&key));
    if (entry && !entry->IsDeleted()) {
        Change(entry);
    }
    return true;
}

OvsPeer *HaStaleDevVnTable::route_peer() const {
    return route_peer_.get();
}

const std::string &HaStaleDevVnTable::dev_name() const {
    return dev_name_;
}

ConnectionStateEntry *HaStaleDevVnTable::state() const {
    return state_.get();
}

void HaStaleDevVnTable::StaleClearAddEntry(uint64_t time_stamp,
                                           HaStaleL2RouteEntry *entry,
                                           StaleClearL2EntryCb cb) {
    if (stale_l2_entry_map_.empty()) {
        // start the timer while adding the first entry
        stale_clear_timer_->Start(kStaleTimerJobInterval,
                boost::bind(&HaStaleDevVnTable::StaleClearTimerCb, this));
    }
    StaleL2Entry l2_entry(time_stamp, entry);
    stale_l2_entry_map_[l2_entry] = cb;
}

void HaStaleDevVnTable::StaleClearDelEntry(uint64_t time_stamp,
                                           HaStaleL2RouteEntry *entry) {
    StaleL2Entry l2_entry(time_stamp, entry);
    stale_l2_entry_map_.erase(l2_entry);
    if (stale_l2_entry_map_.empty()) {
        // stop the timer on last entry removal
        stale_clear_timer_->Cancel();
    }
}

bool HaStaleDevVnTable::StaleClearTimerCb() {
    uint32_t count = 0;
    time_stamp_++;
    uint32_t timer =
        agent_->ovsdb_client()->ha_stale_route_interval()/kStaleTimerJobInterval;
    while (!stale_l2_entry_map_.empty() && count < kNumEntriesPerIteration) {
        CbMap::iterator it = stale_l2_entry_map_.begin();
        if (time_stamp_ - it->first.time_stamp < timer) {
            // first entry is yet to age
            break;
        }
        StaleClearL2EntryCb cb = it->second;
        cb();
        count++;
    }

    if (stale_l2_entry_map_.empty()) {
        // do not restart the timer if all entries are removed
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
HaStaleDevVnSandeshTask::HaStaleDevVnSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args), dev_name_(""), vn_uuid_("") {
    if (false == args.Get("dev_name", &dev_name_)) {
        dev_name_ = "";
    }
    if (false == args.Get("vn_uuid", &vn_uuid_)) {
        vn_uuid_ = "";
    }
}

HaStaleDevVnSandeshTask::HaStaleDevVnSandeshTask(std::string resp_ctx,
                                                 const std::string &dev_name,
                                                 const std::string &vn_uuid) :
    OvsdbSandeshTask(resp_ctx, "0.0.0.0", 0), dev_name_(dev_name), vn_uuid_(vn_uuid) {
}

HaStaleDevVnSandeshTask::~HaStaleDevVnSandeshTask() {
}

void HaStaleDevVnSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!dev_name_.empty()) {
        args.Add("dev_name", dev_name_);
    }
    if (!vn_uuid_.empty()) {
        args.Add("vn_uuid", vn_uuid_);
    }
}

OvsdbSandeshTask::FilterResp
HaStaleDevVnSandeshTask::Filter(KSyncEntry *kentry) {
    if (!vn_uuid_.empty()) {
        HaStaleDevVnEntry *entry = static_cast<HaStaleDevVnEntry *>(kentry);
        if (UuidToString(entry->vn_uuid()).find(vn_uuid_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void HaStaleDevVnSandeshTask::UpdateResp(KSyncEntry *kentry,
                                         SandeshResponse *resp) {
    HaStaleDevVnEntry *entry = static_cast<HaStaleDevVnEntry *>(kentry);
    OvsdbHaStaleDevVnExport dentry;
    dentry.set_state(entry->StateString());
    dentry.set_dev_name(entry->dev_name());
    dentry.set_dev_ip(entry->dev_ip().to_string());
    dentry.set_vn_uuid(UuidToString(entry->vn_uuid()));
    dentry.set_vn_name(entry->vn_name());
    dentry.set_vxlan_id(entry->vxlan_id());
    HaStaleL2RouteSandeshTask task("", entry->dev_name(),
                                   UuidToString(entry->vn_uuid()), "");
    dentry.set_l2_route_table(task.EncodeFirstPage());

    OvsdbHaStaleDevVnExportResp *d_resp =
        static_cast<OvsdbHaStaleDevVnExportResp *>(resp);
    std::vector<OvsdbHaStaleDevVnExport> &dev_vn =
        const_cast<std::vector<OvsdbHaStaleDevVnExport>&>(
                d_resp->get_dev_vn());
    dev_vn.push_back(dentry);
}

SandeshResponse *HaStaleDevVnSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbHaStaleDevVnExportResp());
}

KSyncObject *HaStaleDevVnSandeshTask::GetObject(OvsdbClientSession *session) {
    ConnectionStateTable *con_table =
        Agent::GetInstance()->ovsdb_client()->connection_table();
    ConnectionStateEntry *con_entry = con_table->Find(dev_name_);
    if (con_entry == NULL) {
        return NULL;
    }
    return static_cast<KSyncObject *>(con_entry->ha_stale_dev_vn_table());
}

void OvsdbHaStaleDevVnExportReq::HandleRequest() const {
    HaStaleDevVnSandeshTask *task =
        new HaStaleDevVnSandeshTask(context(), get_dev_name(),
                                    get_vn_uuid());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

