/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include <base/connection_info.h>
#include <base/sandesh/connection_info_constants.h>

// ConnectionState
boost::scoped_ptr<ConnectionState> ConnectionState::instance_;

ConnectionState::ConnectionState() {
}

ConnectionState* ConnectionState::GetInstance() {
    if (instance_ == NULL) {
        instance_.reset(new ConnectionState());
    }
    return instance_.get();
}

void ConnectionState::Update(ConnectionType::type ctype,
    const std::string &name, ConnectionStatus::type status,
    Endpoint server, std::string message) {
    // Populate key
    ConnectionInfoKey key(ctype, name);
    // Populate info
    ConnectionInfo info;
    info.set_type(
        g_connection_info_constants.ConnectionTypeNames.find(ctype)->second);
    info.set_name(name);
    boost::system::error_code ec;
    std::string saddr(server.address().to_string(ec));
    int sport(server.port());
    std::string server_address(saddr + ":" + integerToString(sport));
    std::vector<std::string> server_addrs = boost::assign::list_of
        (server_address);
    info.set_server_addrs(server_addrs);
    info.set_status(
        g_connection_info_constants.ConnectionStatusNames.find(status)->second);
    info.set_description(message);
    // Lookup connection info map
    tbb::mutex::scoped_lock lock(mutex_);
    ConnectionInfoMap::iterator it = connection_map_.find(key);
    if (it != connection_map_.end()) {
        // Update
        it->second = info;
    } else {
        // Add
        connection_map_[key] = info;
    }    
}

void ConnectionState::Delete(ConnectionType::type ctype,
    const std::string &name) {
    // Construct key
    ConnectionInfoKey key(ctype, name);
    // Delete
    tbb::mutex::scoped_lock lock(mutex_);
    connection_map_.erase(key);
}

std::vector<ConnectionInfo> ConnectionState::GetInfos() const {
    tbb::mutex::scoped_lock lock(mutex_);
    std::vector<ConnectionInfo> infos;
    for (ConnectionInfoMap::const_iterator it = connection_map_.begin();
         it != connection_map_.end(); it++) {
        infos.push_back(it->second);
    }
    return infos;
}
