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
#include <ovsdb_types.h>
#include <ovsdb_route_peer.h>
#include <logical_switch_ovsdb.h>
#include <multicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using OVSDB::MulticastMacLocalOvsdb;
using OVSDB::MulticastMacLocalEntry;
using OVSDB::OvsdbClientSession;
using std::string;

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        const MulticastMacLocalEntry *key) : OvsdbEntry(table), mac_(key->mac_),
    logical_switch_name_(key->logical_switch_name_), dip_str_(key->dip_str_) {
}

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        struct ovsdb_idl_row *row) : OvsdbEntry(table),
    mac_(ovsdb_wrapper_mcast_mac_local_mac(row)),
    logical_switch_name_(ovsdb_wrapper_mcast_mac_local_logical_switch(row)),
    dip_str_(ovsdb_wrapper_mcast_mac_remote_dst_ip(row)) {
}

MulticastMacLocalEntry::~MulticastMacLocalEntry() {
}

bool MulticastMacLocalEntry::Add() {
    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Adding Route " + mac_ + " VN uuid " +
            logical_switch_name_);
    VnOvsdbObject *vn_object = table_->client_idl()->vn_ovsdb();
    VnOvsdbEntry vn_key(vn_object, StringToUuid(logical_switch_name_));
    VnOvsdbEntry *vn_entry =
        static_cast<VnOvsdbEntry *>(vn_object->GetReference(&vn_key));

    // Take vrf reference to genrate withdraw/delete route request
    vrf_ = vn_entry->vrf();
    // Add vrf dep in MulticastMacLocalOvsdb
    table->vrf_dep_list_.insert(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                  this));
    vxlan_id_ = vn_entry->vxlan_id();
    if (dip_str_.empty() == false) {
        boost::system::error_code err;
        Ip4Address dest = Ip4Address::from_string(dip_str_, err);
        table->peer()->AddOvsPeerMulticastRoute(vrf_.get(), vxlan_id_,
                                                table_->client_idl()->tsn_ip(),
                                                dest);
    }
    return true;
}

bool MulticastMacLocalEntry::Change() {
    return Add();
}

bool MulticastMacLocalEntry::Delete() {
    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Deleting Route " + mac_ + " VN uuid " +
            logical_switch_name_);
    table->vrf_dep_list_.erase(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(), this));
    table->peer()->DeleteOvsPeerMulticastRoute(vrf_.get(), vxlan_id_);
    // remove vrf reference after deleting route
    vrf_ = NULL;
    return true;
}

bool MulticastMacLocalEntry::IsLess(const KSyncEntry& entry) const {
    const MulticastMacLocalEntry &mcast =
        static_cast<const MulticastMacLocalEntry&>(entry);
    if (mac_ != mcast.mac_)
        return mac_ < mcast.mac_;
    return logical_switch_name_ < mcast.logical_switch_name_;
}

KSyncEntry *MulticastMacLocalEntry::UnresolvedReference() {
    VnOvsdbObject *vn_object = table_->client_idl()->vn_ovsdb();
    VnOvsdbEntry vn_key(vn_object, StringToUuid(logical_switch_name_));
    VnOvsdbEntry *vn_entry =
        static_cast<VnOvsdbEntry *>(vn_object->GetReference(&vn_key));
    if (!vn_entry->IsResolved()) {
        OVSDB_TRACE(Trace, "Skipping route add " + mac_ + " VN uuid " +
                logical_switch_name_ + " due to unavailable VN ");
        return vn_entry;
    }
    return NULL;
}

const std::string &MulticastMacLocalEntry::mac() const {
    return mac_;
}

const std::string &MulticastMacLocalEntry::logical_switch_name() const {
    return logical_switch_name_;
}

MulticastMacLocalOvsdb::MulticastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer) :
    OvsdbObject(idl), peer_(peer) {
    vrf_reeval_queue_ = new WorkQueue<VrfEntryRef>(
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&MulticastMacLocalOvsdb::VrfReEval, this, _1));
    idl->Register(OvsdbClientIdl::OVSDB_MCAST_MAC_LOCAL,
            boost::bind(&MulticastMacLocalOvsdb::Notify, this, _1, _2));
}

MulticastMacLocalOvsdb::~MulticastMacLocalOvsdb() {
    vrf_reeval_queue_->Shutdown();
    delete vrf_reeval_queue_;
}

OvsPeer *MulticastMacLocalOvsdb::peer() {
    return peer_;
}

void MulticastMacLocalOvsdb::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *ls_name = ovsdb_wrapper_mcast_mac_local_logical_switch(row);
    /* ignore if ls_name is not present */
    if (ls_name == NULL) {
        return;
    }

    char *dip_ptr = ovsdb_wrapper_mcast_mac_remote_dst_ip(row);
    if (dip_ptr == NULL)
        return;

    LogicalSwitchTable *l_table = client_idl_->logical_switch_table();
    l_table->OvsdbMcastLocalMacNotify(op, row);

    MulticastMacLocalEntry key(this, row);
    MulticastMacLocalEntry *entry =
        static_cast<MulticastMacLocalEntry *>(FindActiveEntry(&key));
    /* trigger delete if dest ip is not available */
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        if (entry != NULL) {
            entry->ovs_entry_ = NULL;
            Delete(entry);
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            entry = static_cast<MulticastMacLocalEntry *>(Create(&key));
            entry->ovs_entry_ = row;
        }
    } else {
        assert(0);
    }
}

KSyncEntry *MulticastMacLocalOvsdb::Alloc(const KSyncEntry *key, uint32_t index) {
    const MulticastMacLocalEntry *k_entry =
        static_cast<const MulticastMacLocalEntry *>(key);
    MulticastMacLocalEntry *entry = new MulticastMacLocalEntry(this, k_entry);
    return entry;
}

void MulticastMacLocalOvsdb::VrfReEvalEnqueue(VrfEntry *vrf) {
    vrf_reeval_queue_->Enqueue(vrf);
}

bool MulticastMacLocalOvsdb::VrfReEval(VrfEntryRef vrf) {
    // iterate through dependency list and trigger del and add
    VrfDepList::iterator it =
        vrf_dep_list_.upper_bound(VrfDepEntry(vrf.get(), NULL));
    while (it != vrf_dep_list_.end()) {
        if (it->first != vrf.get()) {
            break;
        }
        MulticastMacLocalEntry *u_entry = it->second;
        it++;
        if (u_entry->ovs_entry() != NULL && client_idl() != NULL) {
            // vrf re-eval for Multicast mac local is a catastrophic change
            // trigger NotifyAddDel on ovs row.
            client_idl()->NotifyDelAdd(u_entry->ovs_entry());
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class MulticastMacLocalSandeshTask : public Task {
public:
    MulticastMacLocalSandeshTask(std::string resp_ctx, const std::string &ip,
                               uint32_t port) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
        resp_(new OvsdbMulticastMacLocalResp()), resp_data_(resp_ctx),
        ip_(ip), port_(port) {
    }
    virtual ~MulticastMacLocalSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbMulticastMacLocalEntry> macs;
        TorAgentInit *init =
            static_cast<TorAgentInit *>(Agent::GetInstance()->agent_init());
        OvsdbClientSession *session;
        if (ip_.empty()) {
            session = init->ovsdb_client()->NextSession(NULL);
        } else {
            boost::system::error_code ec;
            Ip4Address ip_addr = Ip4Address::from_string(ip_, ec);
            session = init->ovsdb_client()->FindSession(ip_addr, port_);
        }
        if (session != NULL && session->client_idl() != NULL) {
            MulticastMacLocalOvsdb *table =
                session->client_idl()->multicast_mac_local_ovsdb();
            MulticastMacLocalEntry *entry =
                static_cast<MulticastMacLocalEntry *>(table->Next(NULL));
            while (entry != NULL) {
                OvsdbMulticastMacLocalEntry oentry;
                oentry.set_state(entry->StateString());
                oentry.set_mac(entry->mac());
                oentry.set_logical_switch(entry->logical_switch_name());
                oentry.set_dest_ip(entry->dip_str());
                macs.push_back(oentry);
                entry = static_cast<MulticastMacLocalEntry *>(table->Next(entry));
            }
        }
        resp_->set_macs(macs);
        SendResponse();
        return true;
    }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbMulticastMacLocalResp *resp_;
    std::string resp_data_;
    std::string ip_;
    uint32_t port_;
    DISALLOW_COPY_AND_ASSIGN(MulticastMacLocalSandeshTask);
};

void OvsdbMulticastMacLocalReq::HandleRequest() const {
    MulticastMacLocalSandeshTask *task =
        new MulticastMacLocalSandeshTask(context(), get_session_remote_ip(),
                                       get_session_remote_port());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
