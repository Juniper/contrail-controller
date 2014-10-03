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

#include <base/timer.h>
#include <base/sandesh/process_info_constants.h>
#include <base/sandesh/process_info_types.h>

namespace process {

typedef boost::asio::ip::tcp::endpoint Endpoint;
typedef boost::function<void (const std::vector<ConnectionInfo> &,
    ProcessState::type &, std::string &)> ProcessStateFn;

void GetProcessStateCb(const std::vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, std::string &message,
    size_t expected_connections);

// ConnectionState
class ConnectionState {
public:
    static ConnectionState* GetInstance();
    void Update(ConnectionType::type ctype, const std::string &name,
        ConnectionStatus::type status, Endpoint server,
        std::string message);
    void Delete(ConnectionType::type ctype, const std::string &name);
    std::vector<ConnectionInfo> GetInfos() const;

private:
    typedef boost::tuple<ConnectionType::type, std::string> ConnectionInfoKey;
    typedef std::map<ConnectionInfoKey, ConnectionInfo> ConnectionInfoMap;

    // Singleton
    ConnectionState();

    static boost::scoped_ptr<ConnectionState> instance_;
    mutable tbb::mutex  mutex_;
    ConnectionInfoMap connection_map_;
};

// ConnectionStateManager
template <typename UVEType, typename UVEDataType>
class ConnectionStateManager {
public:
    static ConnectionStateManager* GetInstance() {
        if (instance_ == NULL) {
            instance_.reset(
                new ConnectionStateManager());
        }
        return instance_.get();
    }

    void Init(boost::asio::io_service &service, const std::string &hostname,
        const std::string &module, const std::string &instance_id,
        ProcessStateFn status_cb) {
        timer_ = TimerManager::CreateTimer(service, "Connection State Timer",
                    TaskScheduler::GetInstance()->GetTaskId("connState::Timer"),
                    0);
        timer_->Start(kTimerInterval, boost::bind(
            &ConnectionStateManager<UVEType, UVEDataType>::ConnectionStateTimerExpired,
            this), NULL);
        data_.set_name(hostname);
        process_status_.set_module_id(module);
        process_status_.set_instance_id(instance_id);
        status_cb_ = status_cb;
    }

    void Shutdown() {
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;       
    }

private:
    // Singleton
    ConnectionStateManager() :
        timer_(NULL),
        status_cb_(NULL) {
    }

    bool ConnectionStateTimerExpired() {
        if (status_cb_.empty()) {
            // Keep running
            return true;
        }
        // Update
        process_status_.set_connection_infos(
            ConnectionState::GetInstance()->GetInfos());
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
        UVEType::Send(data_);
        // Keep running
        return true;
    }

    static const int kTimerInterval = 60 * 1000; // 60 seconds
    static boost::scoped_ptr<ConnectionStateManager> instance_;

    Timer *timer_;
    ProcessStateFn status_cb_;
    ProcessStatus process_status_;
    UVEDataType data_;
};

template <typename UVEType, typename UVEDataType>
boost::scoped_ptr<ConnectionStateManager<UVEType, UVEDataType> > 
    ConnectionStateManager<UVEType, UVEDataType>::instance_;

} // namespace process

#endif // __CONNECTION_INFO_H__     
