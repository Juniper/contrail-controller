/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <base/task.h>

#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_client_connection_state.h>
#include <physical_switch_ovsdb.h>
#include <logical_switch_ovsdb.h>
#include <physical_port_ovsdb.h>
#include <ovsdb_types.h>

using OVSDB::OvsdbClient;
using OVSDB::OvsdbClientSession;
using OVSDB::PhysicalSwitchEntry;
using OVSDB::PhysicalSwitchTable;

PhysicalSwitchEntry::PhysicalSwitchEntry(PhysicalSwitchTable *table,
        const std::string &name) : OvsdbEntry(table), name_(name),
    tunnel_ip_() {
}

PhysicalSwitchEntry::~PhysicalSwitchEntry() {
}

bool PhysicalSwitchEntry::Add() {
    table_->client_idl()->connection_table()->AddIdlToConnectionState(
            name_, table_->client_idl());
    return true;
}

bool PhysicalSwitchEntry::Delete() {
    table_->client_idl()->connection_table()->DelIdlToConnectionState(
            name_, table_->client_idl());
    return true;
}

Ip4Address &PhysicalSwitchEntry::tunnel_ip() {
    return tunnel_ip_;
}

const std::string &PhysicalSwitchEntry::name() {
    return name_;
}

void PhysicalSwitchEntry::set_tunnel_ip(std::string ip) {
    boost::system::error_code ec;
    tunnel_ip_ = Ip4Address::from_string(ip, ec);
}

bool PhysicalSwitchEntry::IsLess(const KSyncEntry &entry) const {
    const PhysicalSwitchEntry &ps_entry =
        static_cast<const PhysicalSwitchEntry&>(entry);
    return (name_ < ps_entry.name_);
}

KSyncEntry *PhysicalSwitchEntry::UnresolvedReference() {
    return NULL;
}

void PhysicalSwitchEntry::SendTrace(Trace event) const {
    SandeshPhysicalSwitchInfo info;
    if (event == ADD) {
        info.set_op("Add");
    } else {
        info.set_op("Delete");
    }
    info.set_name(name_);
    OVSDB_TRACE(PhysicalSwitch, info);
}

PhysicalSwitchTable::PhysicalSwitchTable(OvsdbClientIdl *idl) :
    OvsdbObject(idl), update_ports_(false) {
    idl->Register(OvsdbClientIdl::OVSDB_PHYSICAL_SWITCH,
                  boost::bind(&PhysicalSwitchTable::Notify, this, _1, _2));
}

PhysicalSwitchTable::~PhysicalSwitchTable() {
}

void PhysicalSwitchTable::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    PhysicalSwitchEntry key(this, ovsdb_wrapper_physical_switch_name(row));
    PhysicalSwitchEntry *entry =
        static_cast<PhysicalSwitchEntry *>(FindActiveEntry(&key));
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        if (entry != NULL) {
            entry->SendTrace(PhysicalSwitchEntry::DEL);
            entry->ovs_entry_ = NULL;
            UpdatePorts(entry);
            Delete(entry);
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            entry = static_cast<PhysicalSwitchEntry *>(Create(&key));
            entry->SendTrace(PhysicalSwitchEntry::ADD);
        }
        entry->set_tunnel_ip(ovsdb_wrapper_physical_switch_tunnel_ip(row));
        entry->ovs_entry_ = row;

        // check if we need to skip ports updation
        if (update_ports_) {
            UpdatePorts(entry);
        }
    } else {
        assert(0);
    }
}

KSyncEntry *PhysicalSwitchTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const PhysicalSwitchEntry *k_entry =
        static_cast<const PhysicalSwitchEntry *>(key);
    PhysicalSwitchEntry *entry = new PhysicalSwitchEntry(this, k_entry->name_);
    return entry;
}

void PhysicalSwitchTable::StartUpdatePorts() {
    if (update_ports_) {
        return;
    }
    update_ports_ = true;
    PhysicalSwitchEntry *entry = static_cast<PhysicalSwitchEntry *>(Next(NULL));
    while (entry != NULL) {
        // Trigger Update Ports to update pending physical port entries
        UpdatePorts(entry);
        entry = static_cast<PhysicalSwitchEntry *>(Next(entry));
    }
}

void PhysicalSwitchTable::UpdatePorts(PhysicalSwitchEntry *entry) {
    PhysicalPortTable *p_table = client_idl()->physical_port_table();
    PhysicalSwitchEntry::InterfaceList old = entry->intf_list_;
    entry->intf_list_.clear();
    if (entry->ovs_entry_ != NULL) {
        std::size_t count =
            ovsdb_wrapper_physical_switch_ports_count(entry->ovs_entry_);
        struct ovsdb_idl_row *ports[count];
        ovsdb_wrapper_physical_switch_ports(entry->ovs_entry_, ports, count);
        std::size_t i = 0;
        while (i < count) {
            entry->intf_list_.insert(ports[i]);
            if (old.erase(ports[i]) == 0) {
                // if entry was not present eariler trigger Add.
                p_table->CreatePortEntry(ports[i], entry->name());
            }
            i++;
        }
    }
    PhysicalSwitchEntry::InterfaceList::iterator it = old.begin();
    while (it != old.end()) {
        // trigger del notify for ovs idl row
        p_table->DeletePortEntry(*it);
        old.erase(it);
        it = old.begin();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class PhysicalSwitchSandeshTask : public Task {
public:
    PhysicalSwitchSandeshTask(std::string resp_ctx, const std::string &ip,
                              uint32_t port) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
        resp_(new OvsdbPhysicalSwitchResp()), resp_data_(resp_ctx),
        ip_(ip), port_(port) {
    }
    virtual ~PhysicalSwitchSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbPhysicalSwitchEntry> pswitch;
        OvsdbClientSession *session;
        if (ip_.empty()) {
            session = Agent::GetInstance()->ovsdb_client()->NextSession(NULL);
        } else {
            boost::system::error_code ec;
            Ip4Address ip_addr = Ip4Address::from_string(ip_, ec);
            session = Agent::GetInstance()->ovsdb_client()->FindSession(ip_addr,
                                                                        port_);
        }
        if (session != NULL && session->client_idl() != NULL) {
            PhysicalSwitchTable *table =
                session->client_idl()->physical_switch_table();
            PhysicalSwitchEntry *entry =
                static_cast<PhysicalSwitchEntry *>(table->Next(NULL));
            while (entry != NULL) {
                OvsdbPhysicalSwitchEntry pentry;
                pentry.set_state(entry->StateString());
                pentry.set_name(entry->name());
                pentry.set_tunnel_ip(entry->tunnel_ip().to_string());
                pswitch.push_back(pentry);
                entry = static_cast<PhysicalSwitchEntry *>(table->Next(entry));
            }
        }
        resp_->set_pswitch(pswitch);
        SendResponse();
        return true;
    }
    std::string Description() const { return "PhysicalSwitchSandeshTask"; }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbPhysicalSwitchResp *resp_;
    std::string resp_data_;
    std::string ip_;
    uint32_t port_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalSwitchSandeshTask);
};

void OvsdbPhysicalSwitchReq::HandleRequest() const {
    PhysicalSwitchSandeshTask *task =
        new PhysicalSwitchSandeshTask(context(), get_session_remote_ip(),
                                      get_session_remote_port());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

