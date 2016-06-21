/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONNECTION_INFO_H__
#define __CONNECTION_INFO_H__

#include <map>
#include <vector>

#include <boost/tuple/tuple.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/scoped_ptr.hpp>

#include <tbb/mutex.h>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include <base/sandesh/process_info_constants.h>
#include <base/sandesh/process_info_types.h>
#include <base/sandesh/cpuinfo_constants.h>
#include <base/sandesh/cpuinfo_types.h>
#include <base/sandesh/nodeinfo_constants.h>
#include <base/sandesh/nodeinfo_types.h>

class ConnectionInfoTest;

namespace process {

typedef boost::asio::ip::tcp::endpoint Endpoint;
typedef boost::function<void (const std::vector<ConnectionInfo> &,
    ProcessState::type &, std::string &)> ProcessStateFn;

typedef std::pair<std::string, std::string> ConnectionTypeName;
void GetProcessStateCb(const std::vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, std::string &message,
    const std::vector<ConnectionTypeName> &expected_connections);
void GetConnectionInfoMessage(const std::vector<ConnectionInfo> &cinfos,
    const std::vector<ConnectionTypeName> &expected_connections,
    std::string &message);

// ConnectionState
class ConnectionState {
public:
    static ConnectionState* GetInstance();
    void Update();
    void Update(ConnectionType::type ctype, const std::string &name,
        ConnectionStatus::type status, Endpoint server,
        std::string message);
    void Update(ConnectionType::type ctype, const std::string &name,
        ConnectionStatus::type status, const std::vector<Endpoint> &servers,
        std::string message);
    void Delete(ConnectionType::type ctype, const std::string &name);
    std::vector<ConnectionInfo> GetInfos() const;

private:
    void UpdateInternal(ConnectionType::type ctype,
        const std::string &name, ConnectionStatus::type status,
        const std::vector<Endpoint> &servers, std::string message);

    friend class
        ConnectionStateManager;

    typedef boost::tuple<ConnectionType::type, std::string> ConnectionInfoKey;
    typedef std::map<ConnectionInfoKey, ConnectionInfo> ConnectionInfoMap;
    typedef boost::function<void (void)> SendUveCb;

    std::vector<ConnectionInfo> GetInfosUnlocked() const;
    // Singleton
    ConnectionState(SendUveCb send_uve_cb);
    static void CreateInstance(SendUveCb send_uve_cb);

    static boost::scoped_ptr<ConnectionState> instance_;
    mutable tbb::mutex  mutex_;
    ConnectionInfoMap connection_map_;
    SendUveCb send_uve_cb_;
};

// ConnectionStateManager
class ConnectionStateManager {
public:
    static ConnectionStateManager* GetInstance() {
        if (instance_ == NULL) {
            instance_.reset(
                new ConnectionStateManager());
            // Create ConnectionState instance and bind the send UVE function
            assert(ConnectionState::instance_ == NULL);
            ConnectionState::CreateInstance(boost::bind(
                &ConnectionStateManager::
                    SendProcessStateUve, instance_.get(), false));
        }
        return instance_.get();
    }

    void Init(boost::asio::io_service &service, const std::string &hostname,
        const std::string &module, const std::string &instance_id,
        ProcessStateFn status_cb, std::string table) {
        data_.set_name(hostname);
        process_status_.set_module_id(module);
        process_status_.set_instance_id(instance_id);
        status_cb_ = status_cb;
        data_.table_ = table;
    }

    void Shutdown() {
    }

private:
    friend class ConnectionState;
    friend class ::ConnectionInfoTest;

    // Singleton
    ConnectionStateManager() :
        status_cb_(NULL) {
    }

    void SetProcessStateCb(ProcessStateFn status_cb) {
        status_cb_ = status_cb;
    }

    bool SendProcessStateUve(bool lock) {
        if (status_cb_.empty()) {
            return false;
        }
        // Update
        process_status_.set_connection_infos(lock ?
            ConnectionState::GetInstance()->GetInfos() :
            ConnectionState::GetInstance()->GetInfosUnlocked());
        ProcessState::type pstate;
        std::string message;
        status_cb_(process_status_.get_connection_infos(), pstate, message);
        process_status_.set_state(g_process_info_constants.
            ProcessStateNames.find(pstate)->second);
        process_status_.set_description(message);
        // Send
        std::vector<ProcessStatus> vps = boost::assign::list_of
            (process_status_);
        data_.set_process_status(vps);
        NodeStatusUVE::Send(data_);
        return true;
    }
    static boost::scoped_ptr<ConnectionStateManager> instance_;

    ProcessStateFn status_cb_;
    ProcessStatus process_status_;
    NodeStatus data_;
};

} // namespace process

#endif // __CONNECTION_INFO_H__     
