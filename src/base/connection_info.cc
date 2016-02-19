/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/connection_info.h"

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/foreach.hpp>

#include "base/string_util.h"
#include "base/sandesh/process_info_constants.h"

namespace process {

// ConnectionState
boost::scoped_ptr<ConnectionState> ConnectionState::instance_;

ConnectionState::ConnectionState(SendUveCb send_uve_cb) :
    send_uve_cb_(send_uve_cb) {
}

// CreateInstance should be called from ConnectionStateManager::Init()
void ConnectionState::CreateInstance(SendUveCb send_uve_cb) {
    // The assert is to catch errors where ConnectionState::GetInstance()
    // is called before ConnectionStateManager::Init()
    assert(instance_ == NULL);
    instance_.reset(new ConnectionState(send_uve_cb));
}

ConnectionState* ConnectionState::GetInstance() {
    if (instance_ == NULL) {
        // This is needed to handle unit tests that do not call
        // ConnectionStateManager::Init()
        instance_.reset(new ConnectionState(NULL));
    }
    return instance_.get();
}

void ConnectionState::Update() {
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

void ConnectionState::UpdateInternal(ConnectionType::type ctype,
    const std::string &name, ConnectionStatus::type status,
    const std::vector<Endpoint> &servers, std::string message) {
    // Populate key
    ConnectionInfoKey key(ctype, name);
    // Populate info
    ConnectionInfo info;
    info.set_type(
        g_process_info_constants.ConnectionTypeNames.find(ctype)->second);
    info.set_name(name);
    std::vector<std::string> server_addrs;
    BOOST_FOREACH(const Endpoint &server, servers) {
        boost::system::error_code ec;
        std::string saddr(server.address().to_string(ec));
        int sport(server.port());
        std::string server_address(saddr + ":" + integerToString(sport));
        server_addrs.push_back(server_address);
    }
    info.set_server_addrs(server_addrs);
    info.set_status(
        g_process_info_constants.ConnectionStatusNames.find(status)->second);
    info.set_description(message);
    // Lookup connection info map
    tbb::mutex::scoped_lock lock(mutex_);
    ConnectionInfoMap::iterator it = connection_map_.find(key);
    if (it != connection_map_.end()) {
        // Update
        if (it->second.server_addrs == info.server_addrs &&
            it->second.status == info.status &&
            it->second.description == info.description) {
            // Do not send UVE if there is no change in the server_addrs,
            // status or description
            return;
        }
        it->second = info;
    } else {
        // Add
        connection_map_[key] = info;
    }
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

void ConnectionState::Update(ConnectionType::type ctype,
    const std::string &name, ConnectionStatus::type status,
    const std::vector<Endpoint> &servers, std::string message) {
    UpdateInternal(ctype, name, status, servers, message);
}

void ConnectionState::Update(ConnectionType::type ctype,
    const std::string &name, ConnectionStatus::type status,
    Endpoint server, std::string message) {
    UpdateInternal(ctype, name, status, boost::assign::list_of(server),
        message);
}

void ConnectionState::Delete(ConnectionType::type ctype,
    const std::string &name) {
    // Construct key
    ConnectionInfoKey key(ctype, name);
    // Delete
    tbb::mutex::scoped_lock lock(mutex_);
    connection_map_.erase(key);
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

std::vector<ConnectionInfo> ConnectionState::GetInfosUnlocked() const {
    std::vector<ConnectionInfo> infos;
    for (ConnectionInfoMap::const_iterator it = connection_map_.begin();
         it != connection_map_.end(); it++) {
        infos.push_back(it->second);
    }
    return infos;
}

std::vector<ConnectionInfo> ConnectionState::GetInfos() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return GetInfosUnlocked();
}

void GetProcessStateCb(const std::vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, std::string &message,
    size_t expected_connections) {
    // Determine if the number of connections is as expected.
    size_t num_connections(cinfos.size());
    if (num_connections != expected_connections) {
        state = ProcessState::NON_FUNCTIONAL;
        message = "Number of connections:" + integerToString(num_connections) +
                  ", Expected:" + integerToString(expected_connections);
        return;
    }
    std::string cup(g_process_info_constants.ConnectionStatusNames.
                    find(ConnectionStatus::UP)->second);
    bool is_cup = true;
    // Iterate to determine process connectivity status
    for (std::vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const std::string &conn_status(cinfo.get_status());
        if (conn_status != cup) {
            is_cup = false;
            if (message.empty()) {
                message = cinfo.get_type();
            } else {
                message += ", " + cinfo.get_type();
            }
            const std::string &name(cinfo.get_name());
            if (!name.empty()) {
                message += ":" + name;
            }
        }
    }
    // All critical connections are in good condition.
    if (is_cup) {
        state = ProcessState::FUNCTIONAL;
    } else {
        state = ProcessState::NON_FUNCTIONAL;
        message += " connection down";
    }
    return;
}

} // namespace process
