/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <string>

#include <rapidjson/document.h>
#include <bfd/http_server/bfd_json_config.h>
#include <base/logging.h>

#include "bfd_config_client.h"

namespace BFD {

static std::string str_client_id = "client-id";

BFDConfigClient::BFDConfigClient(const boost::asio::ip::tcp::endpoint& ep, EventManager* evm) :
        http_client_(new HttpClient(evm)), ep_(ep), long_poll_connection_(NULL), error_(false), stopped_(false) {
}

BFDConfigClient::~BFDConfigClient() {
    Stop();
}


bool BFDConfigClient::InitalizedNoLock() {
    return stopped_ == false && error_ == false && client_id_.is_initialized();
}

bool BFDConfigClient::Initalized() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    return InitalizedNoLock();
}

void BFDConfigClient::Init() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    http_client_->Init();
    long_poll_connection_ = http_client_->CreateConnection(ep_);
    std::string url = "Session";
    std::string request = "{}";
    long_poll_connection_->HttpPut(request, url,
            boost::bind(&BFDConfigClient::CreateClientSessionCallback, this, _1, _2));
    while (!error_ && !InitalizedNoLock()) {
        cond_var_.wait(lock);
    }
}

void BFDConfigClient::Stop() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!stopped_) {
        stopped_ = true;
        LOG(DEBUG, "Stopping bfd client: " << ep_);
        http_client_->Shutdown();
        http_client_ = NULL;
    }
    LOG(INFO, "BFDConfigClient: " << ep_ << " stopped");
}

void BFDConfigClient::SetError() {
    Stop();
    error_ = true;
}

bool BFDConfigClient::AddBFDHost(const boost::asio::ip::address& remote_address, AddBFDHostCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!InitalizedNoLock())
        return false;

    std::string url = "Session/" + boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection";
    BfdJsonConfig config = DefaultConfig();
    config.address = remote_address;
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    std::string request;
    config.EncodeJsonString(&request);
    connection->HttpPut(request, url, boost::bind(&BFDConfigClient::CreateBFDSessionCallback, _1, _2, connection, cb));
    return true;
}

bool BFDConfigClient::DeleteBFDHost(const boost::asio::ip::address& remote_address, DeleteBFDHostCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!InitalizedNoLock())
        return false;

    std::string url = "Session/" + boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection/"
            + boost::lexical_cast<std::string>(remote_address);
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpDelete(url, boost::bind(&BFDConfigClient::DeleteBFDSessionCallback, _1, _2, connection, cb));
    return true;
}

bool BFDConfigClient::GetBFDSession(const boost::asio::ip::address& remote_address, GetBFDSessionCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!InitalizedNoLock())
        return false;

    std::string url = "Session/" + boost::lexical_cast<std::string>(client_id_.get()) + "/IpConnection/"
            + boost::lexical_cast<std::string>(remote_address);
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpGet(url, boost::bind(&BFDConfigClient::GetBFDConnectionCallback, _1, _2, connection, cb));
    return true;
}

bool BFDConfigClient::Monitor(MonitorCb cb) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!InitalizedNoLock())
        return false;

    std::string url = "Session/" + boost::lexical_cast<std::string>(client_id_.get()) + "/Monitor";
    HttpConnection* connection = http_client_->CreateConnection(ep_);
    connection->HttpGet(url, boost::bind(&BFDConfigClient::MonitorCallback, _1, _2, connection, cb));
    return true;
}

BfdJsonConfig BFDConfigClient::DefaultConfig() {
    BfdJsonConfig res;
    res.desired_min_tx_interval = boost::posix_time::milliseconds(100);
    res.required_min_rx_interval = boost::posix_time::milliseconds(500);
    res.detection_time_multiplier = 3;
    return res;
}

void BFDConfigClient::CreateBFDSessionCallback(const std::string& response,
        const boost::system::error_code& ec, HttpConnection* connection, AddBFDHostCb cb) {
    if (!ec) {
        LOG(DEBUG, "CreateBFDSession server response: " << response);
    } else {
        LOG(ERROR, "CreateBFDSession: Error: " << ec.value() << " " << ec.message());
    }
    connection->client()->RemoveConnection(connection);
    cb(ec);
}

void BFDConfigClient::DeleteBFDSessionCallback(const std::string& response,
        const boost::system::error_code& ec, HttpConnection* connection, DeleteBFDHostCb cb) {
    if (!ec) {
        LOG(DEBUG, "DeleteBFDSession server response: " << response);
    } else {
        LOG(ERROR, "DeleteBFDSession: Error: " << ec.value() << " " << ec.message());
    }
    connection->client()->RemoveConnection(connection);
    cb(ec);
}

void BFDConfigClient::GetBFDConnectionCallback(const std::string& response,
        const boost::system::error_code& ec, HttpConnection* connection, GetBFDSessionCb cb) {
    BfdJsonState state;
    boost::system::error_code return_ec = ec;
    if (!ec) {
        LOG(DEBUG, "GetBFDConnection server response: " << response);
        if (!state.ParseFromJsonString(response)) {
            return_ec = boost::system::error_code(boost::system::errc::protocol_error,
                    boost::system::system_category());
        }
    } else {
        LOG(ERROR, "GetBFDConnection Error: " << ec.value() << " " << ec.message());
    }
    connection->client()->RemoveConnection(connection);
    cb(state, return_ec);
}

void BFDConfigClient::CreateClientSessionCallback(const std::string& response, const boost::system::error_code& ec) {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    if (!ec) {
        if (!response.empty()) {
            LOG(DEBUG, "Server response: " << response);
            rapidjson::Document document;
            document.Parse<0>(response.c_str());
            if (document.HasParseError() || !document.IsObject() || !document.HasMember(str_client_id.c_str())
                    || !document[str_client_id.c_str()].IsInt()) {
                LOG(ERROR, "Unable to parse server response");
                SetError();
            } else {
                client_id_ = document[str_client_id.c_str()].GetInt();
            }
        }
    } else {
        LOG(ERROR, "CreateClientSessionCallback " << ep_ << " Error: " << ec.value() << " " << ec.message());
        SetError();
    }
    cond_var_.notify_all();
}

void BFDConfigClient::MonitorCallback(const std::string& response,
        const boost::system::error_code& ec, HttpConnection* connection, MonitorCb cb) {
    BfdJsonStateMap states;
    boost::system::error_code return_ec = ec;
    if (!ec) {
        LOG(DEBUG, "MonitorCallback server response: " << response);
        if (!states.ParseFromJsonString(response)) {
            return_ec = boost::system::error_code(boost::system::errc::protocol_error,
                    boost::system::system_category());
        }
    } else {
        LOG(ERROR, "MonitorCallback Error: " << ec.value() << " " << ec.message());
    }
    connection->client()->RemoveConnection(connection);
    cb(states, return_ec);
}

}  // namespace BFD
