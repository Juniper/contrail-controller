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
#include <ha_stale_l2_route.h>

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_path.h>
#include <oper/bridge_route.h>

#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;
std::string empty_string("");

HaStaleL2RouteEntry::HaStaleL2RouteEntry(HaStaleL2RouteTable *table,
        const std::string &mac) : OvsdbDBEntry(table), mac_(mac),
    path_preference_(0), vxlan_id_(0), time_stamp_(0) {
}

HaStaleL2RouteEntry::~HaStaleL2RouteEntry() {
}

bool HaStaleL2RouteEntry::Add() {
    // Stop stale clear timer on add/change/delete req
    StopStaleClearTimer();
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(table_);
    if (table->vn_name_.empty()) {
        // donot reexport the route if dest VN is not available
        OVSDB_TRACE(Trace, std::string("Skipping Route export dest VN name ") +
                    "not available vrf " + table->vrf_->GetName() +
                    ", VxlanId " + integerToString(table->vxlan_id_) + ", MAC "+
                    mac_ + ", dest ip " + table->dev_ip_.to_string());
        return true;
    }

    if (path_preference_ < PathPreference::LOW) {
        // donot reexport the route if path preference is less than LOW
        return true;
    }
    HaStaleDevVnEntry *dev_vn = table->dev_vn_;
    vxlan_id_ = table->vxlan_id_;
    dev_vn->route_peer()->AddOvsRoute(table->vrf_.get(), table->vxlan_id_,
                                      table->vn_name_, MacAddress(mac_),
                                      table->dev_ip_);
    OVSDB_TRACE(Trace, std::string("Adding Ha Stale route vrf ") +
                table->vrf_->GetName() + ", VxlanId " +
                integerToString(vxlan_id_) + ", MAC " + mac_ + ", dest ip " +
                table->dev_ip_.to_string());
    return true;
}

bool HaStaleL2RouteEntry::Change() {
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(table_);
    if (vxlan_id_ != 0 && vxlan_id_ != table->vxlan_id_) {
        // for change in vxlan id delete the previously exported
        // route before calling AddMsg
        Delete();
        vxlan_id_ = 0;
    }
    return Add();
}

bool HaStaleL2RouteEntry::Delete() {
    // Stop stale clear timer on add/change/delete req
    StopStaleClearTimer();
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(table_);
    HaStaleDevVnEntry *dev_vn = table->dev_vn_;
    OVSDB_TRACE(Trace, std::string("withdrawing Ha Stale route vrf ") +
                table->vrf_->GetName() + ", VxlanId " +
                integerToString(vxlan_id_) + ", MAC " + mac_);
    dev_vn->route_peer()->DeleteOvsRoute(table->vrf_.get(), vxlan_id_,
                                         MacAddress(mac_));
    return true;
}

void HaStaleL2RouteEntry::StopStaleClearTimer() {
    // Cancel timer if running
    if (time_stamp_ != 0) {
        HaStaleL2RouteTable *table =
            static_cast<HaStaleL2RouteTable *>(table_);
        HaStaleDevVnEntry *dev_vn = table->dev_vn_;
        HaStaleDevVnTable *dev_vn_table =
            static_cast<HaStaleDevVnTable*>(dev_vn->table());
        dev_vn_table->StaleClearDelEntry(time_stamp_, this);
        time_stamp_ = 0;
    }
}

bool HaStaleL2RouteEntry::Sync(DBEntry *db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    const AgentPath *path = entry->GetActivePath();
    bool change = false;
    if (path != NULL && path_preference_ != path->preference()) {
        path_preference_ = path->preference();
        if (path_preference_ < PathPreference::LOW) {
            // Active Path gone, start the timer to delete path
            if (time_stamp_ == 0) {
                HaStaleL2RouteTable *table =
                    static_cast<HaStaleL2RouteTable *>(table_);
                HaStaleDevVnEntry *dev_vn = table->dev_vn_;
                HaStaleDevVnTable *dev_vn_table =
                    static_cast<HaStaleDevVnTable*>(dev_vn->table());
                time_stamp_ = dev_vn_table->time_stamp();
                dev_vn_table->StaleClearAddEntry(time_stamp_, this,
                        boost::bind(&HaStaleL2RouteEntry::StaleClearCb, this));
            }
        } else {
            change = true;
        }
    }

    HaStaleL2RouteTable *table = static_cast<HaStaleL2RouteTable *>(table_);
    if (vxlan_id_ != table->vxlan_id_) {
        // update of vxlan id is done after withdrawing previously
        // exported route
        change = true;
    }

    return change;
}

bool HaStaleL2RouteEntry::IsLess(const KSyncEntry &entry) const {
    const HaStaleL2RouteEntry &ucast =
        static_cast<const HaStaleL2RouteEntry&>(entry);
    return mac_ < ucast.mac_;
}

KSyncEntry *HaStaleL2RouteEntry::UnresolvedReference() {
    return NULL;
}

const std::string &HaStaleL2RouteEntry::mac() const {
    return mac_;
}

uint32_t HaStaleL2RouteEntry::vxlan_id() const {
    return vxlan_id_;
}

bool HaStaleL2RouteEntry::IsStale() const {
    return (time_stamp_ != 0);
}

void HaStaleL2RouteEntry::StaleClearCb() {
    // set timer to NULL as it will be deleted due to completion
    table_->Delete(this);
}

HaStaleL2RouteTable::HaStaleL2RouteTable(
        HaStaleDevVnEntry *dev_vn, AgentRouteTable *table) :
    OvsdbDBObject(NULL, false), table_delete_ref_(this, table->deleter()),
    dev_vn_(dev_vn), state_(dev_vn->state()),
    dev_ip_(dev_vn->dev_ip().to_v4()), vxlan_id_(dev_vn->vxlan_id()),
    vrf_(table->vrf_entry(), this), vn_name_(dev_vn->vn_name()) {
    OvsdbRegisterDBTable(table);
}

HaStaleL2RouteTable::~HaStaleL2RouteTable() {
    // Table unregister will be done by Destructor of KSyncDBObject
    table_delete_ref_.Reset(NULL);
}

KSyncEntry *HaStaleL2RouteTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const HaStaleL2RouteEntry *k_entry =
        static_cast<const HaStaleL2RouteEntry *>(key);
    HaStaleL2RouteEntry *entry = new HaStaleL2RouteEntry(this, k_entry->mac_);
    return entry;
}

KSyncEntry *HaStaleL2RouteTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    HaStaleL2RouteEntry *key =
        new HaStaleL2RouteEntry(this, entry->mac().ToString());
    return static_cast<KSyncEntry *>(key);
}

KSyncDBObject::DBFilterResp HaStaleL2RouteTable::OvsdbDBEntryFilter(
        const DBEntry *db_entry, const OvsdbDBEntry *ovsdb_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    // Locally programmed multicast route should not be added in
    // OVS.
    if (entry->is_multicast()) {
        return DBFilterIgnore;
    }

    if (entry->vrf()->IsDeleted()) {
        // if notification comes for a entry with deleted vrf,
        // trigger delete since we donot resue same vrf object
        // so this entry has to be deleted eventually.
        return DBFilterDelete;
    }

    const NextHop *nh = entry->GetActiveNextHop();
    if (nh == NULL || nh->GetType() != NextHop::TUNNEL) {
        return DBFilterDelete;
    }

    const TunnelNH *tunnel = static_cast<const TunnelNH *>(nh);
    if (*tunnel->GetDip() != dev_ip_) {
        // its not a route to replicate, return delete
        // need to return delete to handle MAC move scenarios
        // as well
        return DBFilterDelete;
    }

    const AgentPath *path = entry->GetActivePath();
    if (path != NULL) {
        // if connection is active and preference is less than
        // PathPreference::LOW trigger delete
        if (path->preference() < PathPreference::LOW
            && state_->IsConnectionActive()) {
            return DBFilterDelete;
        }
    } else {
        return DBFilterDelete;
    }

    return DBFilterAccept;
}

Agent *HaStaleL2RouteTable::agent() const {
    return dev_vn_->agent();
}

void HaStaleL2RouteTable::ManagedDelete() {
    // We do rely on follow up notification of VRF Delete
    // to handle delete of this route table
}

void HaStaleL2RouteTable::EmptyTable() {
    OvsdbDBObject::EmptyTable();
    // unregister the object if emptytable is called with
    // object being scheduled for delete
    if (delete_scheduled()) {
        // trigger Ack for Dev Vn entry and reset to NULL
        dev_vn_->TriggerAck(this);
        dev_vn_ = NULL;
        KSyncObjectManager::Unregister(this);
    }
}

Ip4Address HaStaleL2RouteTable::dev_ip() const {
    return dev_ip_;
}

uint32_t HaStaleL2RouteTable::vxlan_id() const {
    return vxlan_id_;
}

const std::string &HaStaleL2RouteTable::vn_name() const {
    return vn_name_;
}

const std::string &HaStaleL2RouteTable::vrf_name() const {
    if (vrf_ == NULL) {
        return empty_string;
    }
    return vrf_->GetName();
}

void HaStaleL2RouteTable::UpdateParams(HaStaleDevVnEntry *dev_vn) {
    // update params and start resync walk
    bool change = false;
    if (vxlan_id_ != dev_vn->vxlan_id()) {
        vxlan_id_ = dev_vn->vxlan_id();
        change = true;
    }

    if (vn_name_ != dev_vn->vn_name()) {
        vn_name_ = dev_vn->vn_name();
        change = true;
    }

    if (dev_ip_ != dev_vn->dev_ip().to_v4()) {
        dev_ip_ = dev_vn->dev_ip().to_v4();
        change = true;
    }

    if (change) {
        OvsdbStartResyncWalk();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
HaStaleL2RouteSandeshTask::HaStaleL2RouteSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args), dev_name_(""), vn_uuid_("") {
    if (false == args.Get("dev_name", &dev_name_)) {
        dev_name_ = "";
    }
    if (false == args.Get("vn_uuid", &vn_uuid_)) {
        vn_uuid_ = "";
    }
    if (false == args.Get("mac", &mac_)) {
        mac_ = "";
    }
}

HaStaleL2RouteSandeshTask::HaStaleL2RouteSandeshTask(std::string resp_ctx,
                                                     const std::string &dev,
                                                     const std::string &vn_uuid,
                                                     const std::string &mac) :
    OvsdbSandeshTask(resp_ctx, "0.0.0.0", 0), dev_name_(dev), vn_uuid_(vn_uuid),
    mac_(mac) {
}

HaStaleL2RouteSandeshTask::~HaStaleL2RouteSandeshTask() {
}

void HaStaleL2RouteSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!dev_name_.empty()) {
        args.Add("dev_name", dev_name_);
    }
    if (!vn_uuid_.empty()) {
        args.Add("vn_uuid", vn_uuid_);
    }
    if (!mac_.empty()) {
        args.Add("mac", mac_);
    }
}

OvsdbSandeshTask::FilterResp
HaStaleL2RouteSandeshTask::Filter(KSyncEntry *kentry) {
    if (!vn_uuid_.empty()) {
        HaStaleL2RouteEntry *entry = static_cast<HaStaleL2RouteEntry *>(kentry);
        if (entry->mac().find(mac_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void HaStaleL2RouteSandeshTask::UpdateResp(KSyncEntry *kentry,
                                           SandeshResponse *resp) {
    HaStaleL2RouteEntry *entry = static_cast<HaStaleL2RouteEntry *>(kentry);
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(entry->table());
    OvsdbHaStaleL2RouteExport lentry;
    lentry.set_state(entry->StateString());
    lentry.set_dev_ip(table->dev_ip().to_string());
    lentry.set_vn_name(table->vn_name());
    lentry.set_vrf_name(table->vrf_name());
    lentry.set_mac(entry->mac());
    lentry.set_vxlan_id(entry->vxlan_id());
    if (entry->IsStale()) {
        lentry.set_status("stale");
    } else {
        lentry.set_status("active");
    }

    OvsdbHaStaleL2RouteExportResp *l_resp =
        static_cast<OvsdbHaStaleL2RouteExportResp *>(resp);
    std::vector<OvsdbHaStaleL2RouteExport> &l2_route =
        const_cast<std::vector<OvsdbHaStaleL2RouteExport>&>(
                l_resp->get_l2_route());
    l2_route.push_back(lentry);
}

SandeshResponse *HaStaleL2RouteSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbHaStaleL2RouteExportResp());
}

KSyncObject *HaStaleL2RouteSandeshTask::GetObject(OvsdbClientSession *session) {
    ConnectionStateTable *con_table =
        Agent::GetInstance()->ovsdb_client()->connection_table();
    ConnectionStateEntry *con_entry = con_table->Find(dev_name_);
    if (con_entry == NULL) {
        return NULL;
    }
    HaStaleDevVnTable *dev_vn_table = con_entry->ha_stale_dev_vn_table();
    HaStaleDevVnEntry dev_vn_key(dev_vn_table, StringToUuid(vn_uuid_));
    HaStaleDevVnEntry *dev_vn_entry =
        static_cast<HaStaleDevVnEntry*>(dev_vn_table->Find(&dev_vn_key));
    if (dev_vn_entry == NULL) {
        return NULL;
    }
    return static_cast<KSyncObject *>(dev_vn_entry->l2_table());
}

void OvsdbHaStaleL2RouteExportReq::HandleRequest() const {
    HaStaleL2RouteSandeshTask *task =
        new HaStaleL2RouteSandeshTask(context(), get_dev_name(),
                                    get_vn_uuid(), get_mac());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

