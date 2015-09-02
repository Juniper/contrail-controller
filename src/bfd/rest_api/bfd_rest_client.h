/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef BFD_CONFIG_CLIENT_H_
#define BFD_CONFIG_CLIENT_H_

#include <string>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <http/client/http_client.h>

#include "base/util.h"
#include "io/event_manager.h"
#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {

class RESTClient {
 public:
    typedef boost::function<void(boost::system::error_code)> AddBFDHostCb;
    typedef boost::function<void(boost::system::error_code)> DeleteBFDHostCb;
    typedef boost::function<void(const REST::JsonState &state,
        boost::system::error_code)> GetSessionCb;
    typedef boost::function<void(const REST::JsonStateMap &new_states,
        boost::system::error_code)> MonitorCb;

    RESTClient(const boost::asio::ip::tcp::endpoint& ep,
        EventManager* evm);
    virtual ~RESTClient();

    void Init();
    void Stop();

    bool AddBFDHost(const boost::asio::ip::address& remote_address,
                    AddBFDHostCb cb);
    bool DeleteBFDHost(const boost::asio::ip::address& remote_address,
                        DeleteBFDHostCb cb);
    bool GetSession(const boost::asio::ip::address& remote_address,
                        GetSessionCb cb);
    bool Monitor(MonitorCb cb);

    bool is_initialized();
    bool IsStopped();

 private:
    static void CreateSessionCallback(const std::string& response,
            const boost::system::error_code& ec, HttpConnection* connection,
            AddBFDHostCb cb);
    static void DeleteSessionCallback(const std::string& response,
            const boost::system::error_code& ec, HttpConnection* connection,
            DeleteBFDHostCb cb);
    static void GetBFDConnectionCallback(const std::string& response,
            const boost::system::error_code& ec, HttpConnection* connection,
            GetSessionCb cb);
    static void MonitorCallback(const std::string& response,
            const boost::system::error_code& ec, HttpConnection* connection,
            MonitorCb cb);

    static const REST::JsonConfig kDefaultConfig;

    void CreateRESTClientSessionCallback(const std::string& response,
                                         const boost::system::error_code& ec);

    void SetError();
    bool is_initialized_non_locking();

    HttpClient *http_client_;
    boost::asio::ip::tcp::endpoint ep_;
    HttpConnection *long_poll_connection_;
    boost::optional<ClientId> client_id_;
    bool error_;
    tbb::mutex mutex_;
    tbb::interface5::condition_variable cond_var_;
    bool stopped_;
};

}  // namespace BFD

#endif  // SRC_BFD_CONFIG_CLIENT_H_
