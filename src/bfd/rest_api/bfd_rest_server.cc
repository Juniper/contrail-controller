/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/rest_api/bfd_rest_server.h"

#include <vector>
#include <string>

#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/random.hpp>
#include <boost/assign.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include "bfd/bfd_common.h"
#include "base/logging.h"
#include "bfd/bfd_session.h"
#include "bfd/rest_api/bfd_json_config.h"
#include "bfd/rest_api/bfd_rest_common.h"

namespace BFD {

void RESTServer::SessionHandler(const struct RESTData& data) {
    CreateRESTClientSession(data.session, data.request);
}

void RESTServer::ClientHandler(const struct RESTData& data) {
    const std::string& client_id = (*data.match)[1];
    DeleteRESTClientSession(atoi(client_id.c_str()), data.session,
                            data.request);
}

void RESTServer::ClientIPConnectionHandler(const struct RESTData& data) {
    const std::string& client_id = (*data.match)[1];
    CreateBFDConnection(atoi(client_id.c_str()), data.session,
                             data.request);
}

void RESTServer::ClientIPAddressHandlerGet(const struct RESTData& data) {
    const std::string& client_id = (*data.match)[1];
    boost::asio::ip::address ip =
        boost::asio::ip::address::from_string((*data.match)[2]);
    GetBFDConnection(atoi(client_id.c_str()), ip, data.session, data.request);
}

void RESTServer::ClientIPAddressHandlerDelete(const struct RESTData& data) {
    const std::string& client_id = (*data.match)[1];
    boost::asio::ip::address ip =
        boost::asio::ip::address::from_string((*data.match)[2]);
    DeleteBFDConnection(atoi(client_id.c_str()), ip,
                        data.session, data.request);
}

void RESTServer::ClientMonitorHandler(const struct RESTData& data) {
    const std::string& client_id = (*data.match)[1];
    MonitorRESTClientSession(atoi(client_id.c_str()), data.session,
                                  data.request);
}

const std::vector<RESTServer::HandlerSpecifier> RESTServer::RESTHandlers_ =
    boost::assign::list_of
    (HandlerSpecifier(
        boost::regex("/Session"),
        HTTP_PUT,
        &RESTServer::SessionHandler))
    (HandlerSpecifier(
        boost::regex("/Session/([[:digit:]]{1,9})"),
        HTTP_DELETE,
        &RESTServer::ClientHandler))
    (HandlerSpecifier(
        boost::regex("/Session/([[:digit:]]{1,9})/IpConnection"),
        HTTP_PUT,
        &RESTServer::ClientIPConnectionHandler))
    (HandlerSpecifier(
        boost::regex("/Session/([[:digit:]]{1,9})/IpConnection/"
                     "([[:digit:]]{1,3}\\.[[:digit:]]{1,3}\\."
                     "[[:digit:]]{1,3}\\.[[:digit:]]{1,3})"),
        HTTP_GET,
        &RESTServer::ClientIPAddressHandlerGet))
    (HandlerSpecifier(
        boost::regex("/Session/([[:digit:]]{1,9})/IpConnection/"
                     "([[:digit:]]{1,3}\\.[[:digit:]]{1,3}\\."
                     "[[:digit:]]{1,3}\\.[[:digit:]]{1,3})"),
        HTTP_DELETE,
        &RESTServer::ClientIPAddressHandlerDelete))
    (HandlerSpecifier(
        boost::regex("/Session/([[:digit:]]{1,9})/Monitor"),
        HTTP_GET,
        &RESTServer::ClientMonitorHandler));

RESTServer::RESTServer(Server* bfd_server) :
        bfd_server_(bfd_server) {
}

void RESTServer::OnHttpSessionEvent(HttpSession* session,
                                    enum TcpSession::Event event) {
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

RESTClientSession* RESTServer::GetClientSession(ClientId client_id,
                                                HttpSession* session) {
    ClientMap::iterator it = client_sessions_.find(client_id);
    if (it != client_sessions_.end())
        return it->second;

    REST::SendErrorResponse(session, "Unknown client-id: " +
                        boost::lexical_cast<std::string>(client_id), 404);
    return NULL;
}

void RESTServer::CreateRESTClientSession(HttpSession* session,
                                         const HttpRequest* request) {
    ClientId client_id = UniqClientId();
    RESTClientSession* client_session =
        new RESTClientSession(bfd_server_, client_id);
    client_sessions_[client_id] = client_session;
    http_session_map_[session] = client_id;
    session->RegisterEventCb(boost::bind(&RESTServer::OnHttpSessionEvent,
                                         this, _1, _2));

    rapidjson::Document document;
    document.SetObject();
    document.AddMember("client-id", client_id, document.GetAllocator());
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    REST::SendResponse(session, strbuf.GetString());
}

void RESTServer::DeleteRESTClientSession(ClientId client_id,
                                            HttpSession* session,
                                            const HttpRequest* request) {
    RESTClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        return;
    }

    delete client_session;
    client_sessions_.erase(client_id);
    REST::SendResponse(session, "{}");
}

void RESTServer::CreateBFDConnection(ClientId client_id,
                HttpSession* session, const HttpRequest* request) {
    RESTClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        return;
    }

    REST::JsonConfig bfd_session_data;
    if (bfd_session_data.ParseFromJsonString(request->Body())) {
        SessionConfig config;
        config.desiredMinTxInterval =
            bfd_session_data.desired_min_tx_interval;
        config.requiredMinRxInterval =
            bfd_session_data.required_min_rx_interval;
        config.detectionTimeMultiplier =
            bfd_session_data.detection_time_multiplier;
        ResultCode result = client_session->AddBFDConnection(
            bfd_session_data.address, config);
        if (kResultCode_Ok != result) {
            REST::SendErrorResponse(session, "Unable to create session", 500);
        } else {
            REST::SendResponse(session, "{}");
        }
    } else {
        REST::SendErrorResponse(session, "Invalid arguments", 400);
    }
}

void RESTServer::GetBFDConnection(ClientId client_id,
            const boost::asio::ip::address& ip, HttpSession* session,
            const HttpRequest* request) {
    RESTClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        REST::SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    Session* bfd_session = client_session->GetSession(ip);
    if (bfd_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        REST::SendErrorResponse(session, "Unknown bfd session", 404);
        return;
    }

    BFDRemoteSessionState remote_state = bfd_session->remote_state();
    SessionConfig config = bfd_session->config();
    REST::JsonState session_state;
    session_state.local_discriminator = bfd_session->local_discriminator();
    session_state.remote_discriminator = remote_state.discriminator;
    session_state.bfd_local_state = bfd_session->local_state();
    session_state.bfd_remote_state = remote_state.state;
    session_state.remote_min_rx_interval = remote_state.minRxInterval;
    REST::JsonConfig& session_data = session_state.session_config;
    session_data.address = bfd_session->remote_host();
    session_data.desired_min_tx_interval = config.desiredMinTxInterval;
    session_data.required_min_rx_interval = config.requiredMinRxInterval;
    session_data.detection_time_multiplier = config.detectionTimeMultiplier;
    std::string json;
    session_state.EncodeJsonString(&json);
    REST::SendResponse(session, json);
}

void RESTServer::DeleteBFDConnection(ClientId client_id,
            const boost::asio::ip::address& ip, HttpSession* session,
            const HttpRequest* request) {
    RESTClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        REST::SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    ResultCode result = client_session->DeleteBFDConnection(ip);
    if (result != kResultCode_Ok) {
        REST::SendErrorResponse(session, "Unable to delete session");
    } else {
        REST::SendResponse(session, "{}");
    }
}

void RESTServer::MonitorRESTClientSession(ClientId client_id,
            HttpSession* session, const HttpRequest* request) {
    RESTClientSession* client_session = GetClientSession(client_id, session);
    if (client_session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": Couldn't get session.");
        REST::SendErrorResponse(session, "Unknown client session", 404);
        return;
    }
    client_session->AddMonitoringHttpSession(session);
}

void RESTServer::HandleRequest(HttpSession* session,
                                    const HttpRequest* request) {
    tbb::mutex::scoped_lock lock(mutex_);

    std::string path = request->UrlPath();
    BOOST_FOREACH(const RESTServer::HandlerSpecifier& hs, RESTHandlers_) {
        boost::smatch match;
        if (boost::regex_match(path, match, hs.request_regex) &&
            request->GetMethod() == hs.method) {
            struct RESTData data = {
                &match,
                request->GetMethod(),
                session,
                request
            };
            LOG(INFO, "Handling request: " << path);
            (this->*hs.handler_func)(data);
            // Though IMHO it contradicts most programmers' intuition,
            // currently handlers are responsible for freeing handled
            // requests.
            delete request;
            return;
        }
    }
    LOG(ERROR, "Couldn't handle request: " << path);
    REST::SendErrorResponse(session, "Unknown Request", 404);
}

ClientId RESTServer::UniqClientId() {
    ClientId client_id;
    boost::random::uniform_int_distribution<> dist(0, 1e9);
    do {
        client_id = dist(randomGen);
    } while (client_sessions_.find(client_id) != client_sessions_.end());
    return client_id;
}

RESTServer::~RESTServer() {
    for (ClientMap::iterator it = client_sessions_.begin();
        it != client_sessions_.end(); ++it) {
        delete it->second;
    }
}

}  // namespace BFD
