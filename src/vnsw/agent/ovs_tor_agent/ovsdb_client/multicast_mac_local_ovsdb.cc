/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
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
#include <oper/agent_sandesh.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_route_peer.h>
#include <logical_switch_ovsdb.h>
#include <multicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using namespace OVSDB;
using std::string;

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        const MulticastMacLocalEntry *key) : OvsdbEntry(table),
    logical_switch_name_(key->logical_switch_name_), vrf_(NULL, this),
    vxlan_id_(key->vxlan_id_), row_list_(key->row_list_) {
}

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        const std::string &logical_switch_name)
    : OvsdbEntry(table), logical_switch_name_(logical_switch_name),
    vrf_(NULL, this), vxlan_id_(0), row_list_() {
}

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        const std::string &logical_switch_name, struct ovsdb_idl_row *row)
    : OvsdbEntry(table), logical_switch_name_(logical_switch_name),
    vrf_(NULL, this), vxlan_id_(0), row_list_() {
    if (row) {
        row_list_.insert(row);
    }
}

OVSDB::VnOvsdbEntry *MulticastMacLocalEntry::GetVnEntry() const {
    VnOvsdbObject *vn_object = table_->client_idl()->vn_ovsdb();
    VnOvsdbEntry vn_key(vn_object, StringToUuid(logical_switch_name_));
    VnOvsdbEntry *vn_entry =
        static_cast<VnOvsdbEntry *>(vn_object->GetReference(&vn_key));
    return vn_entry;
}

void MulticastMacLocalEntry::EvaluateVrfDependency(VrfEntry *vrf) {
    OVSDB::VnOvsdbEntry *vn_entry = GetVnEntry();
    if ((vn_entry == NULL) || (vn_entry->vrf() == NULL)) {
        OnVrfDelete();
        // Issue a ADD_CHANGE_REQ to push entry into defer state.
        // Whenever VN entry is back this will be updated.
        table_->NotifyEvent(this, KSyncEntry::ADD_CHANGE_REQ);
    }
}

void MulticastMacLocalEntry::OnVrfDelete() {
    if (vrf_ == NULL)
        return;

    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Deleting Multicast Route VN uuid " + logical_switch_name_);
    TorIpList::iterator it_ip = tor_ip_list_.begin();
    for (; it_ip != tor_ip_list_.end(); it_ip++) {
        table->peer()->DeleteOvsPeerMulticastRoute(vrf_.get(), vxlan_id_,
                                                   (*it_ip));
    }
    tor_ip_list_.clear();
    table->vrf_dep_list_.erase(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                   this));
    // remove vrf reference after deleting route
    vrf_ = NULL;
    return;
}

bool MulticastMacLocalEntry::Add() {
    OvsdbIdlRowList::iterator it = row_list_.begin();
    TorIpList old_tor_ip_list = tor_ip_list_;
    // clear the list to hold new entries
    tor_ip_list_.clear();

    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB::VnOvsdbEntry *vn_entry = GetVnEntry();
    // Take vrf reference to genrate withdraw/delete route request
    vrf_ = vn_entry->vrf();
    OVSDB_TRACE(Trace, "Adding multicast Route VN uuid " + logical_switch_name_);
    vxlan_id_ = vn_entry->vxlan_id();
    table->vrf_dep_list_.insert(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                    this));

    for (; it != row_list_.end(); it++) {
        struct ovsdb_idl_row *l_set =
            ovsdb_wrapper_mcast_mac_local_physical_locator_set(*it);
        std::size_t count =
            ovsdb_wrapper_physical_locator_set_locator_count(l_set);
        for (std::size_t i = 0; i != count; i++) {
            struct ovsdb_idl_row *locator =
                ovsdb_wrapper_physical_locator_set_locators(l_set)[i];
            std::string tor_ip_str =
                ovsdb_wrapper_physical_locator_dst_ip(locator);
            boost::system::error_code ec;
            Ip4Address tor_ip = Ip4Address::from_string(tor_ip_str, ec);
            if (tor_ip.to_ulong() == 0) {
                // 0 ip is not valid ip to export, continue to next row
                continue;
            }
            // insert the current ip to new list
            tor_ip_list_.insert(tor_ip);
            // remove from the old list
            old_tor_ip_list.erase(tor_ip);

            // export the OVS route to Oper Db
            table->peer()->AddOvsPeerMulticastRoute(vrf_.get(), vxlan_id_,
                                                    vn_entry->name(),
                                                    table_->client_idl()->tsn_ip(),
                                                    tor_ip);
        }
    }

    TorIpList::iterator it_ip = old_tor_ip_list.begin();
    for (; it_ip != old_tor_ip_list.end(); it_ip++) {
        table->peer()->DeleteOvsPeerMulticastRoute(vrf_.get(), vxlan_id_,
                                                   (*it_ip));
    }

    return true;
}

bool MulticastMacLocalEntry::Change() {
    return Add();
}

bool MulticastMacLocalEntry::Delete() {
    OnVrfDelete();
    return true;
}

bool MulticastMacLocalEntry::IsLess(const KSyncEntry& entry) const {
    const MulticastMacLocalEntry &mcast =
        static_cast<const MulticastMacLocalEntry&>(entry);
    return logical_switch_name_ < mcast.logical_switch_name_;
}

KSyncEntry *MulticastMacLocalEntry::UnresolvedReference() {
    VnOvsdbEntry *vn_entry = GetVnEntry();
    if (!vn_entry->IsResolved()) {
        OVSDB_TRACE(Trace, "Skipping multicast route add " +
                logical_switch_name_ + " due to unavailable VN ");
        return vn_entry;
    }

    return NULL;
}

const MulticastMacLocalEntry::TorIpList &
MulticastMacLocalEntry::tor_ip_list() const {
    return tor_ip_list_;
}

const std::string &MulticastMacLocalEntry::logical_switch_name() const {
    return logical_switch_name_;
}

MulticastMacLocalOvsdb::MulticastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer) :
    OvsdbObject(idl), peer_(peer) {
    vrf_reeval_queue_ = new WorkQueue<VrfEntryRef>(
              idl->agent()->task_scheduler()->GetTaskId("Agent::KSync"), 0,
              boost::bind(&MulticastMacLocalOvsdb::VrfReEval, this, _1));
    vrf_reeval_queue_->set_name("OVSDB VRF Multicast local mac re-evaluation "
                                "queue");
    // register to listen updates for multicast mac local
    idl->Register(OvsdbClientIdl::OVSDB_MCAST_MAC_LOCAL,
                  boost::bind(&MulticastMacLocalOvsdb::Notify, this, _1, _2));
    // registeration to physical locator set is also required, since
    // an update for physical locators in the set can show up after
    // multicast row update // during which we need to trigger
    // re-evaluation of the exported multicast route
    idl->Register(OvsdbClientIdl::OVSDB_PHYSICAL_LOCATOR_SET,
                  boost::bind(&MulticastMacLocalOvsdb::LocatorSetNotify, this,
                              _1, _2));
}

MulticastMacLocalOvsdb::~MulticastMacLocalOvsdb() {
    vrf_reeval_queue_->Shutdown();
    delete vrf_reeval_queue_;
}

OvsPeer *MulticastMacLocalOvsdb::peer() {
    return peer_;
}

void MulticastMacLocalOvsdb::VrfReEvalEnqueue(VrfEntry *vrf) {
    vrf_reeval_queue_->Enqueue(vrf);
}

//Only executed for VRF delete
bool MulticastMacLocalOvsdb::VrfReEval(VrfEntryRef vrf_ref) {
    // iterate through dependency list and trigger del and add
    VrfDepList::iterator it =
        vrf_dep_list_.upper_bound(VrfDepEntry(vrf_ref.get(), NULL));
    while (it != vrf_dep_list_.end()) {
        if (it->first != vrf_ref.get()) {
            break;
        }
        MulticastMacLocalEntry *m_entry = it->second;
        it++;
        m_entry->EvaluateVrfDependency(vrf_ref.get());
    }
    return true;
}

void MulticastMacLocalOvsdb::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *ls_name = ovsdb_wrapper_mcast_mac_local_logical_switch(row);

    LogicalSwitchTable *l_table = client_idl_->logical_switch_table();
    l_table->OvsdbMcastLocalMacNotify(op, row);

    /* ignore if ls_name is not present */
    if (ls_name == NULL) {
        return;
    }

    std::string ls_name_str(ls_name);
    MulticastMacLocalEntry key(this, ls_name, row);
    MulticastMacLocalEntry *entry =
        static_cast<MulticastMacLocalEntry*>(FindActiveEntry(&key));
    struct ovsdb_idl_row *l_set =
        ovsdb_wrapper_mcast_mac_local_physical_locator_set(row);
    // physical locator set is immutable so it will not change
    // for given multicast row, trigger delete for row and
    // wait for locator set to be available
    if (op == OvsdbClientIdl::OVSDB_DEL || l_set == NULL) {
        if (entry != NULL) {
            entry->row_list_.erase(row);
            if (l_set != NULL) {
                locator_dep_list_.erase(l_set);
            }
            if (entry->row_list_.empty()) {
                // delete entry if the last idl row is removed
                Delete(entry);
            } else {
                // trigger change on entry to remove the delete ToR IP
                // from the list
                Change(entry);
            }
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            entry = static_cast<MulticastMacLocalEntry*>(Create(&key));
        } else {
            entry->row_list_.insert(row);
            // trigger change on entry to add the new ToR IP
            // to the list
            Change(entry);
        }
        locator_dep_list_[l_set] = entry;
    }
}

void MulticastMacLocalOvsdb::LocatorSetNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        locator_dep_list_.erase(row);
        return;
    }
    OvsdbIdlDepList::iterator it = locator_dep_list_.find(row);
    if (it != locator_dep_list_.end()) {
        Change(it->second);
    }
}

KSyncEntry *MulticastMacLocalOvsdb::Alloc(const KSyncEntry *key, uint32_t index) {
    const MulticastMacLocalEntry *k_entry =
        static_cast<const MulticastMacLocalEntry *>(key);
    MulticastMacLocalEntry *entry = new MulticastMacLocalEntry(this, k_entry);
    return entry;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
MulticastMacLocalSandeshTask::MulticastMacLocalSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args) {
    if (args.Get("ls_name", &ls_name_) == false) {
        ls_name_ = "";
    }
}

MulticastMacLocalSandeshTask::MulticastMacLocalSandeshTask(
        std::string resp_ctx, const std::string &ip, uint32_t port,
        const std::string &ls) :
    OvsdbSandeshTask(resp_ctx, ip, port), ls_name_(ls) {
}

MulticastMacLocalSandeshTask::~MulticastMacLocalSandeshTask() {
}

void MulticastMacLocalSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!ls_name_.empty()) {
        args.Add("ls_name", ls_name_);
    }
}

OvsdbSandeshTask::FilterResp
MulticastMacLocalSandeshTask::Filter(KSyncEntry *kentry) {
    if (!ls_name_.empty()) {
        MulticastMacLocalEntry *entry =
            static_cast<MulticastMacLocalEntry *>(kentry);
        if (entry->logical_switch_name().find(ls_name_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void MulticastMacLocalSandeshTask::UpdateResp(KSyncEntry *kentry,
                                              SandeshResponse *resp) {
    MulticastMacLocalEntry *entry =
        static_cast<MulticastMacLocalEntry *>(kentry);
    OvsdbMulticastMacLocalEntry oentry;
    oentry.set_state(entry->StateString());
    oentry.set_mac("ff:ff:ff:ff:ff:ff");
    oentry.set_logical_switch(entry->logical_switch_name());
    oentry.set_vxlan_id(entry->vxlan_id());
    std::vector<std::string> &tor_ip =
        const_cast<std::vector<std::string>&>(oentry.get_tor_ip());
    MulticastMacLocalEntry::TorIpList::const_iterator it =
        entry->tor_ip_list().begin();
    for (; it != entry->tor_ip_list().end(); it++) {
        tor_ip.push_back((*it).to_string());
    }
    OvsdbMulticastMacLocalResp *m_resp =
        static_cast<OvsdbMulticastMacLocalResp *>(resp);
    std::vector<OvsdbMulticastMacLocalEntry> &macs =
        const_cast<std::vector<OvsdbMulticastMacLocalEntry>&>(
                m_resp->get_macs());
    macs.push_back(oentry);
}

SandeshResponse *MulticastMacLocalSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbMulticastMacLocalResp());
}

KSyncObject *MulticastMacLocalSandeshTask::GetObject(
        OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(
            session->client_idl()->multicast_mac_local_ovsdb());
}

void OvsdbMulticastMacLocalReq::HandleRequest() const {
    MulticastMacLocalSandeshTask *task =
        new MulticastMacLocalSandeshTask(context(), get_session_remote_ip(),
                                       get_session_remote_port(),
                                       get_logical_switch());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
