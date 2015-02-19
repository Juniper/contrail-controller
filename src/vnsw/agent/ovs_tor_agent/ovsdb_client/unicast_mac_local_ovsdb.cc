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
#include <unicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using OVSDB::UnicastMacLocalOvsdb;
using OVSDB::UnicastMacLocalEntry;
using OVSDB::OvsdbClientSession;
using std::string;

UnicastMacLocalEntry::UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
        const UnicastMacLocalEntry *key) : OvsdbEntry(table), mac_(key->mac_),
    logical_switch_name_(key->logical_switch_name_), dest_ip_(key->dest_ip_) {
}

UnicastMacLocalEntry::UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
        struct ovsdb_idl_row *row) : OvsdbEntry(table),
    mac_(ovsdb_wrapper_ucast_mac_local_mac(row)),
    logical_switch_name_(ovsdb_wrapper_ucast_mac_local_logical_switch(row)),
    dest_ip_() {
    if (ovsdb_wrapper_ucast_mac_local_dst_ip(row))
        dest_ip_ = ovsdb_wrapper_ucast_mac_local_dst_ip(row);
}

UnicastMacLocalEntry::~UnicastMacLocalEntry() {
}

bool UnicastMacLocalEntry::Add() {
    UnicastMacLocalOvsdb *table = static_cast<UnicastMacLocalOvsdb *>(table_);
    OVSDB_TRACE(Trace, "Adding Route " + mac_ + " VN uuid " +
            logical_switch_name_ + " destination IP " + dest_ip_);
    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry l_key(l_table, logical_switch_name_.c_str());
    LogicalSwitchEntry *ls_entry =
        static_cast<LogicalSwitchEntry *>(l_table->Find(&l_key));
    const PhysicalDeviceVn *dev_vn =
        static_cast<const PhysicalDeviceVn *>(ls_entry->GetDBEntry());
    // Take vrf reference to genrate withdraw/delete route request
    vrf_ = dev_vn->vn()->GetVrf();
    // Add vrf dep in UnicastMacLocalOvsdb
    table->vrf_dep_list_.insert(UnicastMacLocalOvsdb::VrfDepEntry(vrf_.get(), this));
    vxlan_id_ = dev_vn->vxlan_id();
    boost::system::error_code err;
    Ip4Address dest = Ip4Address::from_string(dest_ip_, err);
    table->peer()->AddOvsRoute(dev_vn->vn(), MacAddress(mac_), dest);
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
    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(l_table, logical_switch_name_.c_str());
    LogicalSwitchEntry *l_switch =
        static_cast<LogicalSwitchEntry *>(l_table->GetReference(&key));
    if (!l_switch->IsResolved()) {
        OVSDB_TRACE(Trace, "Skipping route add " + mac_ + " VN uuid " +
                logical_switch_name_ + " destination IP " + dest_ip_ +
                " due to unavailable Logical Switch ");
        return l_switch;
    }

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
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&UnicastMacLocalOvsdb::VrfReEval, this, _1));
    idl->Register(OvsdbClientIdl::OVSDB_UCAST_MAC_LOCAL,
            boost::bind(&UnicastMacLocalOvsdb::Notify, this, _1, _2));
}

UnicastMacLocalOvsdb::~UnicastMacLocalOvsdb() {
    vrf_reeval_queue_->Shutdown();
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

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class UnicastMacLocalSandeshTask : public Task {
public:
    UnicastMacLocalSandeshTask(std::string resp_ctx, std::string ip,
                               uint32_t port) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
        resp_(new OvsdbUnicastMacLocalResp()), resp_data_(resp_ctx),
        ip_(ip), port_(port) {
    }
    virtual ~UnicastMacLocalSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbUnicastMacLocalEntry> macs;
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
            UnicastMacLocalOvsdb *table =
                session->client_idl()->unicast_mac_local_ovsdb();
            UnicastMacLocalEntry *entry =
                static_cast<UnicastMacLocalEntry *>(table->Next(NULL));
            while (entry != NULL) {
                OvsdbUnicastMacLocalEntry oentry;
                oentry.set_state(entry->StateString());
                oentry.set_mac(entry->mac());
                oentry.set_logical_switch(entry->logical_switch_name());
                oentry.set_dest_ip(entry->dest_ip());
                macs.push_back(oentry);
                entry = static_cast<UnicastMacLocalEntry *>(table->Next(entry));
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

    OvsdbUnicastMacLocalResp *resp_;
    std::string resp_data_;
    std::string ip_;
    uint32_t port_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacLocalSandeshTask);
};

void OvsdbUnicastMacLocalReq::HandleRequest() const {
    UnicastMacLocalSandeshTask *task =
        new UnicastMacLocalSandeshTask(context(), get_session_remote_ip(),
                                       get_session_remote_port());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

