/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <string>

#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/random.hpp>

#include <bfd/bfd_common.h>
#include <base/logging.h>
#include <bfd/bfd_session.h>
#include <bfd/http_server/bfd_json_config.h>
#include "bfd_config_server.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

namespace BFD {

static const boost::regex regex_session("/Session");
static const boost::regex regex_client("/Session/([[:digit:]]{1,9})");
static const boost::regex regex_client_ip("/Session/([[:digit:]]{1,9})/IpConnection");
static const boost::regex regex_client_ip_address(
        "/Session/([[:digit:]]{1,9})/IpConnection/([[:digit:]]{1,3}\\.[[:digit:]]{1,3}\\.[[:digit:]]{1,3}\\.[[:digit:]]{1,3})");
static const boost::regex regex_client_monitor("/Session/([[:digit:]]{1,9})/Monitor");

BFDConfigServer::BFDConfigServer(BFDServer* bfd_server) :
        bfd_server_(bfd_server) {
}

void BFDConfigServer::OnHttpSessionEvent(HttpSession* session, enum TcpSession::Event event) {
    LOG(INFO, "OnHttpSessionEvent: " << session << " " << event);
    tbb::mutex::scoped_lock lock(mutex_);

    if (event == TcpSession::CLOSE) {
        HttpSessionMap::iterator it = http_session_map_.find(session);
        if (it != http_session_map_.end()) {
            ClientId client_id = it->second;
            ClientMap::iterator jt = client_sessions_.find(client_id);
            if (jt != client_sessions_.end()) {
                delete jt->second;
                client_sessions_.erase(jt);
            }
            http_session_map_.erase(it);
        } else {
            LOG(ERROR, "Unable to find client session");
        }
    }
}

ClientSession* BFDConfigServer::GetClientSession(ClientId client_id, HttpSession* session) {
    ClientMap::iterator it = client_sessions_.find(client_id);
    if (it != client_sessions_.end()) {
        return it->second;
    } else {
        SendErrorResponse(session, "Unknown client-id: " + boost::lexical_cast<std::string>(client_id), 404);
        return NULL;
    }
}

void BFDConfigServer::CreateClientSession(HttpSession* session, const HttpRequest* request) {
    ClientId client_id = UniqClientId();
    ClientSession* client_session = new ClientSession(bfd_server_, client_id);
    client_sessions_[client_id] = client_session;
    http_session_map_[session] = client_id;
    session->RegisterEventCb(boost::bind(&BFDConfigServer::OnHttpSessionEvent, this, _1, _2));

    rapidjson::Document document;
    document.SetObject();
    document.AddMember("client-id", client_id, document.GetAllocator());
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    SendResponse(session, strbuf.GetString());
}

void BFDConfigServer::DeleteClientSession(ClientId client_id, HttpSession* session, const HttpRequest* request) {
    ClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL)
        return;

    delete client_session;
    client_sessions_.erase(client_id);
    SendResponse(session, "{}");
}

void BFDConfigServer::CreateBFDConnection(ClientId client_id, HttpSession* session, const HttpRequest* request) {
    ClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        return;
    }

    BfdJsonConfig bfd_session_data;
    if (bfd_session_data.ParseFromJsonString(request->Body())) {
        BFDSessionConfig config;
        config.desiredMinTxInterval = bfd_session_data.desired_min_tx_interval;
        config.requiredMinRxInterval = bfd_session_data.required_min_rx_interval;
        config.detectionTimeMultiplier = bfd_session_data.detection_time_multiplier;
        ResultCode result = client_session->AddBFDConnection(bfd_session_data.address, config);
        if (kResultCode_Ok != result) {
            SendErrorResponse(session, "Unable to create session", 500);
        } else {
            SendResponse(session, "{}");
        }
    } else {
        SendErrorResponse(session, "Invalid arguments", 400);
    }
}

void BFDConfigServer::GetBFDConnection(ClientId client_id, const boost::asio::ip::address& ip, HttpSession* session,
        const HttpRequest* request) {
    ClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    BFDSession* bfd_session = client_session->GetSession(ip);
    if (bfd_session == NULL) {
        SendErrorResponse(session, "Unknown bfd session", 404);
        return;
    }
    BFDRemoteSessionState remote_state = bfd_session->RemoteState();
    BFDSessionConfig config = bfd_session->Config();
    BfdJsonState session_state;
    session_state.local_discriminator = bfd_session->LocalDiscriminator();
    session_state.remote_discriminator = remote_state.discriminator;
    session_state.bfd_local_state = bfd_session->LocalState();
    session_state.bfd_remote_state = remote_state.state;
    session_state.remote_min_rx_interval = remote_state.minRxInterval;
    BfdJsonConfig& session_data = session_state.session_config;
    session_data.address = bfd_session->RemoteHost();
    session_data.desired_min_tx_interval = config.desiredMinTxInterval;
    session_data.required_min_rx_interval = config.requiredMinRxInterval;
    session_data.detection_time_multiplier = config.detectionTimeMultiplier;
    std::string json;
    session_state.EncodeJsonString(&json);
    SendResponse(session, json);
}

void BFDConfigServer::DeleteBFDConnection(ClientId client_id, const boost::asio::ip::address& ip, HttpSession* session,
        const HttpRequest* request) {
    ClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    ResultCode result = client_session->DeleteBFDConnection(ip);
    if (result != kResultCode_Ok) {
        SendErrorResponse(session, "Unable to delete session");
    } else {
        SendResponse(session, "{}");
    }
}

void BFDConfigServer::MonitorClientSession(ClientId client_id, HttpSession* session, const HttpRequest* request) {
    ClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    client_session->AddMonitoringHttpSession(session);
}

void BFDConfigServer::HandleRequest(HttpSession* session, const HttpRequest* request) {
    tbb::mutex::scoped_lock lock(mutex_);

    std::string path = request->UrlPath();
    boost::smatch what;
    if (boost::regex_match(path, regex_session) && request->GetMethod() == HTTP_POST) {
        CreateClientSession(session, request);
//    } else if (boost::regex_match(path, what, regex_client) && request->GetMethod() == HTTP_DELETE) {
//        std::string client_id = what[1];
//        DeleteClientSession(atoi(client_id.c_str()), session, request);
    } else if (boost::regex_match(path, what, regex_client_ip) && request->GetMethod() == HTTP_POST) {
        std::string client_id = what[1];
        CreateBFDConnection(atoi(client_id.c_str()), session, request);
    } else if (boost::regex_match(path, what, regex_client_ip_address)) {
        std::string client_id = what[1];
        boost::asio::ip::address ip = boost::asio::ip::address::from_string(what[2]);
        if (request->GetMethod() == HTTP_GET) {
            GetBFDConnection(atoi(client_id.c_str()), ip, session, request);
        } else if (request->GetMethod() == HTTP_DELETE) {
            DeleteBFDConnection(atoi(client_id.c_str()), ip, session, request);
        } else {
            SendErrorResponse(session, "Unknown Request", 404);
        }
    } else if (boost::regex_match(path, what, regex_client_monitor)) {
        std::string client_id = what[1];
        MonitorClientSession(atoi(client_id.c_str()), session, request);
    } else {
        SendErrorResponse(session, "Unknown Request", 404);
    }
}

ClientId BFDConfigServer::UniqClientId() {
    ClientId client_id;
    boost::random::uniform_int_distribution<> dist(0, 1e9);
    do {
        client_id = dist(randomGen);
    } while (client_sessions_.find(client_id) != client_sessions_.end());
    return client_id;
}

void SendResponse(HttpSession *session, const std::string &msg, int status_code) {
    const std::string response =
        "HTTP/1.1 " + boost::lexical_cast<std::string>(status_code) + " OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Content-Length: " + boost::lexical_cast<std::string>(msg.length()) + "\r\n"
        "\r\n"
        + msg;
    session->Send(reinterpret_cast<const u_int8_t *>(response.c_str()),
            response.length(), NULL);
}

void SendErrorResponse(HttpSession *session, const std::string &error_msg, int status_code) {
    rapidjson::Document document;
    document.SetObject();
    document.AddMember("error", error_msg.c_str(), document.GetAllocator());
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    SendResponse(session, strbuf.GetString(), status_code);
}

BFDConfigServer::~BFDConfigServer() {
    for (ClientMap::iterator it = client_sessions_.begin(); it != client_sessions_.end(); ++it) {
        delete it->second;
    }
}

}  // namespace BFD
