/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <base/string_util.h>
#include "ovsdb_client_connection_state.h"

using namespace OVSDB;
using namespace process;
using namespace std;

ConnectionStateEntry::ConnectionStateEntry(const std::string &device_name) :
    device_name_(device_name), device_entry_(NULL) {
}

ConnectionStateEntry::~ConnectionStateEntry() {
}

ConnectionStateTable::ConnectionStateTable(Agent *agent)
    : agent_(agent), table_(agent->physical_device_table()) {
    id_ = table_->Register(
            boost::bind(&ConnectionStateTable::PhysicalDeviceNotify,
                        this, _1, _2));
}

ConnectionStateTable::~ConnectionStateTable() {
    table_->Unregister(id_);
}

void ConnectionStateTable::AddIdlToConnectionState(const std::string &dev_name,
                                                   OvsdbClientIdl *idl) {
    ConnectionStateEntry *state = new ConnectionStateEntry(dev_name);
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
        if (it->second->device_entry_ == NULL) {
            UpdateConnectionInfo(it->second, true);
            delete it->second;
            entry_map_.erase(it);
        } else {
            UpdateConnectionInfo(it->second, false);
        }
    }
}

void ConnectionStateTable::PhysicalDeviceNotify(DBTablePartBase *part,
                                                DBEntryBase *e) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(e);
    ConnectionStateEntry *state =
        static_cast<ConnectionStateEntry *>(dev->GetState(table_, id_));
    if (dev->IsDeleted()) {
        if (state) {
            state->device_entry_ = NULL;
            if (state->idl_list_.empty()) {
                UpdateConnectionInfo(state, true);
                entry_map_.erase(state->device_name_);
                delete state;
            } else {
                UpdateConnectionInfo(state, false);
            }
            dev->ClearState(table_, id_);
        }
        return;
    }
    if (!state) {
        state = new ConnectionStateEntry(dev->name());
        pair<EntryMap::iterator, bool> ret;
        ret = entry_map_.insert(pair<string, ConnectionStateEntry*>(dev->name(),
                                                                    state));
        if (ret.second == false) {
            // entry already existed, delete allocated memory
            delete state;
        }
        ret.first->second->device_entry_ = dev;
        dev->SetState(table_, id_, ret.first->second);
        UpdateConnectionInfo(ret.first->second, false);
    }
}

void ConnectionStateTable::UpdateConnectionInfo(ConnectionStateEntry *entry,
                                                bool deleted) {
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

