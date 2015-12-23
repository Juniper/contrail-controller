/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <cmn/agent.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/vxlan.h>
#include <oper/physical_device_vn.h>
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <base/util.h>
#include <base/string_util.h>
#include <net/mac_address.h>
#include <oper/agent_sandesh.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_route_peer.h>
#include <logical_switch_ovsdb.h>
#include <unicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using namespace OVSDB;
using std::string;

UnicastMacLocalEntry::UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
        const std::string &logical_switch, const std::string &mac) :
    OvsdbEntry(table), mac_(mac), logical_switch_name_(logical_switch),
    dest_ip_(""), vrf_(NULL, this) {
}

UnicastMacLocalEntry::UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
        const UnicastMacLocalEntry *key) : OvsdbEntry(table), mac_(key->mac_),
    logical_switch_name_(key->logical_switch_name_), dest_ip_(key->dest_ip_),
    vrf_(NULL, this) {
}

UnicastMacLocalEntry::UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
        struct ovsdb_idl_row *row) : OvsdbEntry(table),
    mac_(ovsdb_wrapper_ucast_mac_local_mac(row)),
    logical_switch_name_(ovsdb_wrapper_ucast_mac_local_logical_switch(row)),
    dest_ip_(), vrf_(NULL, this) {
    if (ovsdb_wrapper_ucast_mac_local_dst_ip(row))
        dest_ip_ = ovsdb_wrapper_ucast_mac_local_dst_ip(row);
}

UnicastMacLocalEntry::~UnicastMacLocalEntry() {
}

bool UnicastMacLocalEntry::Add() {
    UnicastMacLocalOvsdb *table = static_cast<UnicastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Adding Route " + mac_ + " VN uuid " +
            logical_switch_name_ + " destination IP " + dest_ip_);
    VnOvsdbObject *vn_object = table_->client_idl()->vn_ovsdb();
    VnOvsdbEntry vn_key(vn_object, StringToUuid(logical_switch_name_));
    VnOvsdbEntry *vn_entry =
        static_cast<VnOvsdbEntry *>(vn_object->GetReference(&vn_key));

    // Take vrf reference to genrate withdraw/delete route request
    vrf_ = vn_entry->vrf();
    // Add vrf dep in UnicastMacLocalOvsdb
    table->vrf_dep_list_.insert(UnicastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                  this));
    vxlan_id_ = vn_entry->vxlan_id();
    boost::system::error_code err;
    Ip4Address dest = Ip4Address::from_string(dest_ip_, err);
    table->peer()->AddOvsRoute(vrf_.get(), vxlan_id_, vn_entry->name(),
                               MacAddress(mac_), dest);
    return true;
}

bool UnicastMacLocalEntry::Delete() {
    UnicastMacLocalOvsdb *table = static_cast<UnicastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Deleting Route " + mac_ + " VN uuid " +
            logical_switch_name_ + " destination IP " + dest_ip_);
    table->vrf_dep_list_.erase(UnicastMacLocalOvsdb::VrfDepEntry(vrf_.get(), this));
    table->peer()->DeleteOvsRoute(vrf_.get(), vxlan_id_, MacAddress(mac_));
    // remove vrf reference after deleting route
    vrf_ = NULL;
    return true;
}

bool UnicastMacLocalEntry::IsLess(const KSyncEntry& entry) const {
    const UnicastMacLocalEntry &ucast =
        static_cast<const UnicastMacLocalEntry&>(entry);
    if (mac_ != ucast.mac_)
        return mac_ < ucast.mac_;
    return logical_switch_name_ < ucast.logical_switch_name_;
}

KSyncEntry *UnicastMacLocalEntry::UnresolvedReference() {
    VnOvsdbObject *vn_object = table_->client_idl()->vn_ovsdb();
    VnOvsdbEntry vn_key(vn_object, StringToUuid(logical_switch_name_));
    VnOvsdbEntry *vn_entry =
        static_cast<VnOvsdbEntry *>(vn_object->GetReference(&vn_key));
    if (!vn_entry->IsResolved()) {
        OVSDB_TRACE(Trace, "Skipping route add " + mac_ + " VN uuid " +
                logical_switch_name_ + " destination IP " + dest_ip_ +
                " due to unavailable VN ");
        return vn_entry;
    }
    return NULL;
}

const std::string &UnicastMacLocalEntry::mac() const {
    return mac_;
}

const std::string &UnicastMacLocalEntry::logical_switch_name() const {
    return logical_switch_name_;
}

const std::string &UnicastMacLocalEntry::dest_ip() const {
    return dest_ip_;
}

UnicastMacLocalOvsdb::UnicastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer) :
    OvsdbObject(idl), peer_(peer) {
    vrf_reeval_queue_ = new WorkQueue<VrfEntryRef>(
            idl->agent()->task_scheduler()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&UnicastMacLocalOvsdb::VrfReEval, this, _1));
    vrf_reeval_queue_->set_name("OVSDB VRF unicast-mac-local event queue");
    idl->Register(OvsdbClientIdl::OVSDB_UCAST_MAC_LOCAL,
            boost::bind(&UnicastMacLocalOvsdb::Notify, this, _1, _2));
}

UnicastMacLocalOvsdb::~UnicastMacLocalOvsdb() {
    delete vrf_reeval_queue_;
}

OvsPeer *UnicastMacLocalOvsdb::peer() {
    return peer_;
}

void UnicastMacLocalOvsdb::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *ls_name = ovsdb_wrapper_ucast_mac_local_logical_switch(row);
    const char *dest_ip = ovsdb_wrapper_ucast_mac_local_dst_ip(row);
    /* ignore if ls_name is not present */
    if (ls_name == NULL) {
        return;
    }

    LogicalSwitchTable *l_table = client_idl_->logical_switch_table();
    l_table->OvsdbUcastLocalMacNotify(op, row);

    UnicastMacLocalEntry key(this, row);
    UnicastMacLocalEntry *entry =
        static_cast<UnicastMacLocalEntry *>(FindActiveEntry(&key));
    /* trigger delete if dest ip is not available */
    if (op == OvsdbClientIdl::OVSDB_DEL || dest_ip == NULL) {
        if (entry != NULL) {
            entry->ovs_entry_ = NULL;
            Delete(entry);
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            entry = static_cast<UnicastMacLocalEntry *>(Create(&key));
            entry->ovs_entry_ = row;
        }
    } else {
        assert(0);
    }
}

KSyncEntry *UnicastMacLocalOvsdb::Alloc(const KSyncEntry *key, uint32_t index) {
    const UnicastMacLocalEntry *k_entry =
        static_cast<const UnicastMacLocalEntry *>(key);
    UnicastMacLocalEntry *entry = new UnicastMacLocalEntry(this, k_entry);
    return entry;
}

void UnicastMacLocalOvsdb::VrfReEvalEnqueue(VrfEntry *vrf) {
    if (vrf_reeval_queue_->deleted()) {
        // skip Enqueuing entry to deleted workqueue
        return;
    }

    vrf_reeval_queue_->Enqueue(vrf);
}

bool UnicastMacLocalOvsdb::VrfReEval(VrfEntryRef vrf) {
    // iterate through dependency list and trigger del and add
    VrfDepList::iterator it =
        vrf_dep_list_.upper_bound(VrfDepEntry(vrf.get(), NULL));
    while (it != vrf_dep_list_.end()) {
        if (it->first != vrf.get()) {
            break;
        }
        UnicastMacLocalEntry *u_entry = it->second;
        it++;
        if (u_entry->ovs_entry() != NULL && client_idl() != NULL) {
            // vrf re-eval for unicast mac local is a catastrophic change
            // trigger NotifyAddDel on ovs row.
            client_idl()->NotifyDelAdd(u_entry->ovs_entry());
        }
    }
    return true;
}

bool UnicastMacLocalOvsdb::IsVrfReEvalQueueActive() const {
    return !(vrf_reeval_queue_->deleted());
}

void UnicastMacLocalOvsdb::DeleteTableDone(void) {
    // Shutdown the reeval queue on table deletion,
    // all the unicast MAC Local entries will get
    // deleted and clear eventually
    vrf_reeval_queue_->Shutdown();
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
UnicastMacLocalSandeshTask::UnicastMacLocalSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args) {
    if (args.Get("ls_name", &ls_name_) == false) {
        ls_name_ = "";
    }
    if (args.Get("mac", &mac_) == false) {
        mac_ = "";
    }
}

UnicastMacLocalSandeshTask::UnicastMacLocalSandeshTask(
        std::string resp_ctx, const std::string &ip, uint32_t port,
        const std::string &ls, const std::string &mac) :
    OvsdbSandeshTask(resp_ctx, ip, port), ls_name_(ls), mac_(mac) {
}

UnicastMacLocalSandeshTask::~UnicastMacLocalSandeshTask() {
}

void UnicastMacLocalSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!ls_name_.empty()) {
        args.Add("ls_name", ls_name_);
    }
    if (!mac_.empty()) {
        args.Add("mac", mac_);
    }
}

OvsdbSandeshTask::FilterResp
UnicastMacLocalSandeshTask::Filter(KSyncEntry *kentry) {
    UnicastMacLocalEntry *entry = static_cast<UnicastMacLocalEntry *>(kentry);
    if (!ls_name_.empty()) {
        if (entry->logical_switch_name().find(ls_name_) == std::string::npos) {
            return FilterDeny;
        }
    }
    if (!mac_.empty()) {
        if (entry->mac().find(mac_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void UnicastMacLocalSandeshTask::UpdateResp(KSyncEntry *kentry,
                                            SandeshResponse *resp) {
    UnicastMacLocalEntry *entry = static_cast<UnicastMacLocalEntry *>(kentry);
    OvsdbUnicastMacLocalEntry oentry;
    oentry.set_state(entry->StateString());
    oentry.set_mac(entry->mac());
    oentry.set_logical_switch(entry->logical_switch_name());
    oentry.set_dest_ip(entry->dest_ip());
    OvsdbUnicastMacLocalResp *u_resp =
        static_cast<OvsdbUnicastMacLocalResp *>(resp);
    std::vector<OvsdbUnicastMacLocalEntry> &macs =
        const_cast<std::vector<OvsdbUnicastMacLocalEntry>&>(u_resp->get_macs());
    macs.push_back(oentry);
}

SandeshResponse *UnicastMacLocalSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbUnicastMacLocalResp());
}

KSyncObject *UnicastMacLocalSandeshTask::GetObject(
        OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(
            session->client_idl()->unicast_mac_local_ovsdb());
}

void OvsdbUnicastMacLocalReq::HandleRequest() const {
    UnicastMacLocalSandeshTask *task =
        new UnicastMacLocalSandeshTask(context(), get_session_remote_ip(),
                                       get_session_remote_port(),
                                       get_logical_switch(), get_mac());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

