/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/rest_api/bfd_rest_client.h"

#include <string>
#include <boost/asio/ip/address.hpp>
#include <rapidjson/document.h>

#include "base/logging.h"
#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {

const REST::JsonConfig RESTClient::kDefaultConfig(
    /* .address = */ boost::asio::ip::address::from_string("0.0.0.0"),
    /* .desired_min_tx_interval = */ boost::posix_time::milliseconds(100),
    /* .required_min_tx_interval = */ boost::posix_time::milliseconds(500),
    /* .detection_time_multiplier = */ 3
);

RESTClient::RESTClient(const boost::asio::ip::tcp::endpoint& ep,
    EventManager* evm) :
        http_client_(new HttpClient(evm)), ep_(ep), long_poll_connection_(NULL),
        error_(false), stopped_(false) {
}

RESTClient::~RESTClient() {
    Stop();
    // TODO(bfd) clean up inheritance mess in HttpClient
    TcpServerManager::DeleteServer(http_client_);
}

bool RESTClient::is_initialized_non_locking() {
    return !stopped_ && !error_ && client_id_.is_initialized();
}

bool RESTClient::is_initialized() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    return is_initialized_non_locking();
}

void RESTClient::Init() {
    const std::string url = "Session";
    const std::string request = "{}";

    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);

    http_client_->Init();
    long_poll_connection_ = http_client_->CreateConnection(ep_);
    long_poll_connection_->HttpPut(request, url,
            boost::bind(&RESTClient::CreateRESTClientSessionCallback,
                        this, _1, _2));

    while (!error_ && !is_initialized_non_locking()) {
        cond_var_.wait(lock);
    }
}

void RESTClient::Stop() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);

    if (!stopped_) {
        stopped_ = true;
        LOG(DEBUG, "Stopping bfd client: " << ep_);
        http_client_->Shutdown();
    }
}

void RESTClient::SetError() {
    Stop();
    error_ = true;
}

bool RESTClient::AddBFDHost(const boost::asio::ip::address& remote_address,
                                    AddBFDHostCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!is_initialized_non_locking())
        return false;

    std::string url = "Session/" +
        boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection";
    REST::JsonConfig config = kDefaultConfig;
    config.address = remote_address;
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    std::string request;
    config.EncodeJsonString(&request);
    connection->HttpPut(request, url, boost::bind(
        &RESTClient::CreateSessionCallback, _1, _2, connection, cb));
    return true;
}

bool RESTClient::DeleteBFDHost(const boost::asio::ip::address& remote_address,
                                    DeleteBFDHostCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!is_initialized_non_locking())
        return false;

    std::string url = "Session/" +
        boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection/" +
        boost::lexical_cast<std::string>(remote_address);
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpDelete(url,
        boost::bind(&RESTClient::DeleteSessionCallback, _1, _2,
            connection, cb));
    return true;
}

bool RESTClient::GetSession(const boost::asio::ip::address& remote_address,
                                    GetSessionCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!is_initialized_non_locking())
        return false;

    std::string url = "Session/" +
        boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection/" +
        boost::lexical_cast<std::string>(remote_address);
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpGet(url, boost::bind(&RESTClient::GetBFDConnectionCallback,
                        _1, _2, connection, cb));
    return true;
}

bool RESTClient::Monitor(MonitorCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);

    if (!is_initialized_non_locking())
        return false;

    std::string url = "Session/" +
        boost::lexical_cast<std::string>(client_id_.get()) + "/Monitor";
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpGet(url, boost::bind(&RESTClient::MonitorCallback,
                        _1, _2, connection, cb));
    return true;
}

void RESTClient::CreateSessionCallback(const std::string& response,
                                       const boost::system::error_code& ec,
                                       HttpConnection* connection,
                                       AddBFDHostCb cb) {
    if (!ec) {
        LOG(DEBUG, "CreateSession server response: " << response);
    } else {
        LOG(ERROR, "CreateSession: Error: "
                   << ec.value() << " " << ec.message());
    }

    connection->client()->RemoveConnection(connection);
    cb(ec);
}

void RESTClient::DeleteSessionCallback(const std::string& response,
                                       const boost::system::error_code& ec,
                                       HttpConnection* connection,
                                       DeleteBFDHostCb cb) {
    if (!ec) {
        LOG(DEBUG, "DeleteSession server response: " << response);
    } else {
        LOG(ERROR, "DeleteSession: Error: "
                   << ec.value() << " " << ec.message());
    }

    connection->client()->RemoveConnection(connection);
    cb(ec);
}

void RESTClient::GetBFDConnectionCallback(const std::string& response,
                                          const boost::system::error_code& ec,
                                          HttpConnection* connection,
                                          GetSessionCb cb) {
    REST::JsonState state;
    boost::system::error_code return_ec = ec;

    if (!ec) {
        LOG(DEBUG, "GetBFDConnection server response: " << response);
        if (!state.ParseFromJsonString(response)) {
            return_ec =
                boost::system::error_code(boost::system::errc::protocol_error,
                    boost::system::system_category());
        }
    } else {
        LOG(ERROR, "GetBFDConnection Error: "
                   << ec.value() << " " << ec.message());
    }

    connection->client()->RemoveConnection(connection);
    cb(state, return_ec);
}

void RESTClient::CreateRESTClientSessionCallback(const std::string& response,
                                         const boost::system::error_code& ec) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);

    if (!ec) {
        if (!response.empty()) {
            LOG(DEBUG, "Server response: " << response);
            rapidjson::Document document;
            document.Parse<0>(response.c_str());
            if (document.HasParseError() ||
                !document.IsObject() ||
                !document.HasMember("client-id") ||
                !document["client-id"].IsInt()) {
                LOG(ERROR, "Unable to parse server response");
                SetError();
            } else {
                client_id_ = document["client-id"].GetInt();
            }
        }
    } else {
        LOG(ERROR, "CreateRESTClientSessionCallback " << ep_ << " Error: "
                   << ec.value() << " " << ec.message());
        SetError();
    }

    cond_var_.notify_all();
}

void RESTClient::MonitorCallback(const std::string& response,
                                 const boost::system::error_code& ec,
                                 HttpConnection* connection,
                                 MonitorCb cb) {
    REST::JsonStateMap states;
    boost::system::error_code return_ec = ec;

    if (!ec) {
        LOG(DEBUG, "MonitorCallback server response: " << response);
        if (!states.ParseFromJsonString(response)) {
            return_ec =
                boost::system::error_code(boost::system::errc::protocol_error,
                    boost::system::system_category());
        }
    } else {
        LOG(ERROR, "MonitorCallback Error: " << ec.value() << " "
            << ec.message());
    }

    connection->client()->RemoveConnection(connection);
    cb(states, return_ec);
}

}  // namespace BFD
