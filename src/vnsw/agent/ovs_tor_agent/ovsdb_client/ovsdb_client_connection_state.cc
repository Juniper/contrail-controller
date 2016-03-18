/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <base/string_util.h>
#include "ovsdb_client_connection_state.h"
#include "ha_stale_dev_vn.h"

using namespace OVSDB;
using namespace process;
using namespace std;

ConnectionStateEntry::ConnectionStateEntry(ConnectionStateTable *table,
                                           const std::string &device_name,
                                           const boost::uuids::uuid &u) :
    table_(table), device_name_(device_name), device_uuid_(u),
    device_entry_(NULL), ha_stale_dev_vn_table_(NULL) {
    refcount_ = 0;
}

ConnectionStateEntry::~ConnectionStateEntry() {
    if (ha_stale_dev_vn_table_ != NULL) {
        ha_stale_dev_vn_table_->DeleteTable();
        ha_stale_dev_vn_table_ = NULL;
    }
}

bool ConnectionStateEntry::IsConnectionActive() {
    if (idl_list_.empty()) {
        return false;
    }

    IdlList::iterator it = idl_list_.begin();
    for (; it != idl_list_.end(); ++it) {
        // if there is any non deleted IDL available
        // return true
        if (!(*it)->IsDeleted()) {
            return true;
        }
    }

    return false;
}

void OVSDB::intrusive_ptr_add_ref(ConnectionStateEntry *p) {
    assert(p->device_entry_ != NULL || !p->idl_list_.empty());
    p->refcount_++;
}

void OVSDB::intrusive_ptr_release(ConnectionStateEntry *p) {
    int count = --p->refcount_;
    if (count == 0 && p->idl_list_.empty() && p->device_entry_ == NULL) {
        // delete the entry and free the state
        p->table_->UpdateConnectionInfo(p, true);
        p->table_->entry_map_.erase(p->device_name_);
        delete p;
    }
}

ConnectionStateTable::ConnectionStateTable(Agent *agent,
                                           OvsPeerManager *manager)
    : agent_(agent), table_(agent->physical_device_table()),
    manager_(manager) {
    id_ = table_->Register(
            boost::bind(&ConnectionStateTable::PhysicalDeviceNotify,
                        this, _1, _2));
}

ConnectionStateTable::~ConnectionStateTable() {
    table_->Unregister(id_);
}

void ConnectionStateTable::AddIdlToConnectionState(const std::string &dev_name,
                                                   OvsdbClientIdl *idl) {
    ConnectionStateEntry *state = new ConnectionStateEntry(this, dev_name,
                                                     boost::uuids::nil_uuid());
    pair<EntryMap::iterator, bool> ret;
    ret = entry_map_.insert(pair<string, ConnectionStateEntry*>(dev_name,
                                                                state));
    if (ret.second == false) {
        // entry already existed, delete allocated memory
        delete state;
    }
    ret.first->second->idl_list_.insert(idl);
    UpdateConnectionInfo(ret.first->second, false);
}

void ConnectionStateTable::DelIdlToConnectionState(const std::string &dev_name,
                                                   OvsdbClientIdl *idl) {
    EntryMap::iterator it = entry_map_.find(dev_name);
    if (it == entry_map_.end()) {
        return;
    }
    it->second->idl_list_.erase(idl);
    if (it->second->idl_list_.empty()) {
        if (it->second->device_entry_ == NULL && it->second->refcount_ == 0) {
            UpdateConnectionInfo(it->second, true);
            delete it->second;
            entry_map_.erase(it);
        } else {
            UpdateConnectionInfo(it->second, false);
        }
    }
}

ConnectionStateEntry *ConnectionStateTable::Find(const std::string &dev_name) {
    EntryMap::iterator it = entry_map_.find(dev_name);
    if (it == entry_map_.end()) {
        return NULL;
    }
    return it->second;
}

void ConnectionStateTable::PhysicalDeviceNotify(DBTablePartBase *part,
                                                DBEntryBase *e) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(e);
    ConnectionStateEntry *state =
        static_cast<ConnectionStateEntry *>(dev->GetState(table_, id_));
    if (dev->IsDeleted()) {
        if (state) {
            state->device_entry_ = NULL;
            if (state->idl_list_.empty() && state->refcount_ == 0) {
                UpdateConnectionInfo(state, true);
                entry_map_.erase(state->device_name_);
                delete state;
            } else {
                UpdateConnectionInfo(state, false);
            }
            dev->ClearState(table_, id_);

            if (state->ha_stale_dev_vn_table_ != NULL) {
                state->ha_stale_dev_vn_table_->DeleteTable();
                state->ha_stale_dev_vn_table_ = NULL;
            }
        }
        return;
    }
    if (!state) {
        if (dev->name().empty())
            return;

        state = new ConnectionStateEntry(this, dev->name(), dev->uuid());
        pair<EntryMap::iterator, bool> ret;
        ret = entry_map_.insert(pair<string, ConnectionStateEntry*>(dev->name(),
                                                                    state));
        if (ret.second == false) {
            // entry already existed, delete allocated memory
            delete state;
        }
        state = ret.first->second;
        ret.first->second->device_uuid_ = dev->uuid();
        ret.first->second->device_entry_ = dev;
        dev->SetState(table_, id_, ret.first->second);
        UpdateConnectionInfo(ret.first->second, false);
    }

    if (state->ha_stale_dev_vn_table_ == NULL) {
        state->ha_stale_dev_vn_table_ =
            new HaStaleDevVnTable(agent_, manager_, state,
                                          state->device_name_);
    }
}

void ConnectionStateTable::UpdateConnectionInfo(ConnectionStateEntry *entry,
                                                bool deleted) {
    NotifyUve(entry, deleted);
    if (agent_->connection_state() == NULL)
        return;

    if (deleted) {
        agent_->connection_state()->Delete(ConnectionType::TOR,
                                           entry->device_name_);
    } else {
        ConnectionStatus::type status = ConnectionStatus::UP;
        string message;
        boost::asio::ip::tcp::endpoint ep;
        if (!entry->idl_list_.empty()) {
            ConnectionStateEntry::IdlList::iterator it =
                entry->idl_list_.begin();
            ep.address((*it)->remote_ip());
            ep.port((*it)->remote_port());
        }

        if (entry->device_entry_ == NULL) {
            status = ConnectionStatus::DOWN;
            message = "Config Not Available";
        } else if (entry->idl_list_.empty()) {
            status = ConnectionStatus::DOWN;
            message = "ToR Not Available";
        } else {
            message = "Active sessions = " +
                      integerToString(entry->idl_list_.size());
        }
        agent_->connection_state()->Update(ConnectionType::TOR,
                                           entry->device_name_, status,
                                           ep, message);
    }
}

void ConnectionStateTable::NotifyUve(ConnectionStateEntry *entry,
                                     bool deleted) {
    /* If device uuid is not available we cannot notify Uve Module */
    if (entry->device_uuid_ == boost::uuids::nil_uuid())
        return;

    if (agent_->uve() == NULL || agent_->uve()->prouter_uve_table() == NULL)
        return;

    ProuterUveTable *ptable = agent_->uve()->prouter_uve_table();
    if (deleted) {
        ptable->UpdateMastership(entry->device_uuid_, false);
    } else {
        bool mastership = true;
        if (entry->device_entry_ == NULL) {
            mastership = false;
        } else if (entry->idl_list_.empty()) {
            mastership = false;
        }
        ptable->UpdateMastership(entry->device_uuid_, mastership);
    }
}
