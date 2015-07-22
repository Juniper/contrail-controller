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

using namespace OVSDB;

HaStaleL2RouteEntry::HaStaleL2RouteEntry(HaStaleL2RouteTable *table,
        const std::string &mac) : OvsdbDBEntry(table), mac_(mac),
    stale_clear_timer_(TimerManager::CreateTimer(
                *(table->GetAgentPtr()->event_manager())->io_service(),
                "OVSDB Route Replicator cleanup timer",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"),
                0, true)) {
}

HaStaleL2RouteEntry::HaStaleL2RouteEntry(HaStaleL2RouteTable *table,
        const BridgeRouteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac().ToString()),
    stale_clear_timer_(TimerManager::CreateTimer(
                *(table->GetAgentPtr()->event_manager())->io_service(),
                "OVSDB Route Replicator cleanup timer",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"),
                0, true)) {
}

HaStaleL2RouteEntry::HaStaleL2RouteEntry(HaStaleL2RouteTable *table,
        const HaStaleL2RouteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac_),
    stale_clear_timer_(TimerManager::CreateTimer(
                *(table->GetAgentPtr()->event_manager())->io_service(),
                "OVSDB Route Replicator cleanup timer",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"),
                0, true)) {
}

HaStaleL2RouteEntry::~HaStaleL2RouteEntry() {
    if (stale_clear_timer_ != NULL) {
        TimerManager::DeleteTimer(stale_clear_timer_);
    }
}

void HaStaleL2RouteEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(table_);
    HaStaleDevVnEntry *dev_vn =
        static_cast<HaStaleDevVnEntry *>(table->dev_vn_ref_.get());
    dev_vn->route_peer()->AddOvsRoute(table->vrf_.get(), table->vxlan_id_,
                                      table->vn_name_, MacAddress(mac_),
                                      table->dev_ip_);

    // Cancel timer if running
    stale_clear_timer_->Cancel();
}

void HaStaleL2RouteEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void HaStaleL2RouteEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    HaStaleL2RouteTable *table =
        static_cast<HaStaleL2RouteTable *>(table_);
    HaStaleDevVnEntry *dev_vn =
        static_cast<HaStaleDevVnEntry *>(table->dev_vn_ref_.get());
    dev_vn->route_peer()->DeleteOvsRoute(table->vrf_.get(), table->vxlan_id_,
                                         MacAddress(mac_));

    if (stale_clear_timer_ != NULL) {
        // Cancel timer if running
        stale_clear_timer_->Cancel();
    }
}

bool HaStaleL2RouteEntry::Sync(DBEntry *db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    const AgentPath *path = entry->GetActivePath();
    if (path != NULL && path_preference_ != path->preference()) {
        path_preference_ = path->preference();
        if (path_preference_ < 100) {
            // Active Path gone, start the timer to delete path
            int timer =
                table_->GetAgentPtr()->ovsdb_client()->ha_stale_route_interval();
            assert(timer > 0);
            stale_clear_timer_->Start(timer,
                 boost::bind(&HaStaleL2RouteEntry::StaleClearTimerCb, this));
        } else {
            return true;
        }
    }

    return false;
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

bool HaStaleL2RouteEntry::StaleClearTimerCb() {
    // set timer to NULL as it will be deleted due to completion
    stale_clear_timer_ = NULL;
    table_->Delete(this);
    return false;
}

HaStaleL2RouteTable::HaStaleL2RouteTable(
        HaStaleDevVnEntry *dev_vn, AgentRouteTable *table) :
    OvsdbDBObject(NULL, false), table_delete_ref_(this, table->deleter()),
    state_(dev_vn->state()), dev_ip_(dev_vn->dev_ip().to_v4()),
    vxlan_id_(dev_vn->vxlan_id()), vrf_(table->vrf_entry()),
    vn_name_(dev_vn->vn_name()) {
    HaStaleDevVnTable *dev_table =
        static_cast<HaStaleDevVnTable*>(dev_vn->table());
    HaStaleDevVnEntry dev_key(dev_table, "");
    dev_vn_ref_ = dev_table->GetReference(&dev_key);
    OvsdbRegisterDBTable(table);
}

HaStaleL2RouteTable::~HaStaleL2RouteTable() {
    // Table unregister will be done by Destructor of KSyncDBObject
    table_delete_ref_.Reset(NULL);
}

KSyncEntry *HaStaleL2RouteTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const HaStaleL2RouteEntry *k_entry =
        static_cast<const HaStaleL2RouteEntry *>(key);
    HaStaleL2RouteEntry *entry = new HaStaleL2RouteEntry(this, k_entry);
    return entry;
}

KSyncEntry *HaStaleL2RouteTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    HaStaleL2RouteEntry *key = new HaStaleL2RouteEntry(this, entry);
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
        return DBFilterDelete;
    }

    const AgentPath *path = entry->GetActivePath();
    if (path != NULL) {
        // if connection is active and preference is less than 100
        // trigger delete
        if (path->preference() < 100 && state_->IsConnectionActive()) {
            return DBFilterDelete;
        }
    } else {
        return DBFilterDelete;
    }

    return DBFilterAccept;
}

Agent *HaStaleL2RouteTable::GetAgentPtr() {
    HaStaleDevVnEntry *dev_vn =
        static_cast<HaStaleDevVnEntry *>(dev_vn_ref_.get());
    return dev_vn->GetAgentPtr();
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
        KSyncObjectManager::Unregister(this);
    }
}

uint32_t HaStaleL2RouteTable::vxlan_id() {
    return vxlan_id_;
}

Ip4Address HaStaleL2RouteTable::dev_ip() {
    return dev_ip_;
}

