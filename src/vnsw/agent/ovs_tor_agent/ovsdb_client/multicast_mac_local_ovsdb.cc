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
#include <ovsdb_types.h>
#include <ovsdb_route_peer.h>
#include <logical_switch_ovsdb.h>
#include <multicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using OVSDB::MulticastMacLocalOvsdb;
using OVSDB::MulticastMacLocalEntry;
using OVSDB::OvsdbClient;
using OVSDB::OvsdbClientSession;
using std::string;

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
        const MulticastMacLocalEntry *key) : OvsdbEntry(table),
    logical_switch_name_(key->logical_switch_name_),
    logical_switch_(key->logical_switch_) {
}

MulticastMacLocalEntry::MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
                                    const LogicalSwitchEntry *logical_switch) :
    OvsdbEntry(table), logical_switch_name_(logical_switch->name()),
    logical_switch_(logical_switch) {
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
        //Issue a ADD_CHANGE_REQ to push entry into defer state.
        //Whenever VN entry is back this will be updated.
        table_->NotifyEvent(this, KSyncEntry::ADD_CHANGE_REQ);
    }
}

void MulticastMacLocalEntry::OnVrfDelete() {
    if (vrf_ == NULL)
        return;

    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Deleting Multicast Route VN uuid " + logical_switch_name_);
    table->peer()->DeleteOvsPeerMulticastRoute(vrf_.get(), vxlan_id_);
    table->vrf_dep_list_.erase(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                   this));
    // remove vrf reference after deleting route
    vrf_ = NULL;
    return;
}

bool MulticastMacLocalEntry::Add() {
    MulticastMacLocalOvsdb *table = static_cast<MulticastMacLocalOvsdb *>(table_);
    OVSDB::VnOvsdbEntry *vn_entry = GetVnEntry();
    // Take vrf reference to genrate withdraw/delete route request
    vrf_ = vn_entry->vrf();
    OVSDB_TRACE(Trace, "Adding multicast Route VN uuid " + logical_switch_name_);
    vxlan_id_ = logical_switch_->vxlan_id();
    table->vrf_dep_list_.insert(MulticastMacLocalOvsdb::VrfDepEntry(vrf_.get(),
                                                                    this));
    table->peer()->AddOvsPeerMulticastRoute(vrf_.get(), vxlan_id_,
                                            vn_entry->name(),
                                            table_->client_idl()->tsn_ip(),
                                            logical_switch_->tor_ip().to_v4());
    return true;
}

bool MulticastMacLocalEntry::Change() {
    return Add();
}

bool MulticastMacLocalEntry::Delete() {
    OnVrfDelete();
    logical_switch_ = NULL;
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

const std::string &MulticastMacLocalEntry::logical_switch_name() const {
    return logical_switch_name_;
}

MulticastMacLocalOvsdb::MulticastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer) :
    OvsdbObject(idl), peer_(peer) {
        vrf_reeval_queue_ = new WorkQueue<VrfEntryRef>(
                  TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
                  boost::bind(&MulticastMacLocalOvsdb::VrfReEval, this, _1));
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

KSyncEntry *MulticastMacLocalOvsdb::Alloc(const KSyncEntry *key, uint32_t index) {
    const MulticastMacLocalEntry *k_entry =
        static_cast<const MulticastMacLocalEntry *>(key);
    MulticastMacLocalEntry *entry = new MulticastMacLocalEntry(this, k_entry);
    return entry;
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
        OvsdbClient *ovsdb_client = Agent::GetInstance()->ovsdb_client();
        OvsdbClientSession *session;
        if (ip_.empty()) {
            session = ovsdb_client->NextSession(NULL);
        } else {
            boost::system::error_code ec;
            Ip4Address ip_addr = Ip4Address::from_string(ip_, ec);
            session = ovsdb_client->FindSession(ip_addr, port_);
        }
        if (session != NULL && session->client_idl() != NULL) {
            MulticastMacLocalOvsdb *table =
                session->client_idl()->multicast_mac_local_ovsdb();
            MulticastMacLocalEntry *entry =
                static_cast<MulticastMacLocalEntry *>(table->Next(NULL));
            while (entry != NULL) {
                OvsdbMulticastMacLocalEntry oentry;
                oentry.set_state(entry->StateString());
                oentry.set_mac("ff:ff:ff:ff:ff:ff");
                oentry.set_logical_switch(entry->logical_switch_name());
                oentry.set_vxlan_id(entry->vxlan_id());
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
