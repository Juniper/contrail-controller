/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/rest_api/bfd_client_session.h"

#include "base/logging.h"
#include "http/http_session.h"
#include "bfd/bfd_session.h"
#include "bfd/rest_api/bfd_rest_common.h"
#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {

RESTClientSession::RESTClientSession(Server* server, ClientId client_id) :
    client_id_(client_id), server_(server), changed_(true) {
}

Session *RESTClientSession::GetSession(const boost::asio::ip::address &ip,
        const SessionIndex &index) const {
    return GetSession(SessionKey(ip, index));
}

Session *RESTClientSession::GetSession(const SessionKey &key) const {
    return server_->SessionByKey(key);
}

void RESTClientSession::Notify() {
    LOG(DEBUG, "Notify: " << client_id_);
    if (!bfd_sessions_.empty() && http_sessions_.empty()) {
        changed_ = true;
        return;
    }
    changed_ = false;

    if (http_sessions_.empty())
        return;

    REST::JsonStateMap map;
    for (Sessions::iterator it = bfd_sessions_.begin();
         it != bfd_sessions_.end(); ++it) {
        Session *session = GetSession(*it);
        map.states[session->key().remote_address] = session->local_state();
    }

    std::string json;
    map.EncodeJsonString(&json);
    for (HttpSessionSet::iterator it = http_sessions_.begin();
         it != http_sessions_.end(); ++it) {
        if (false == it->get()->IsClosed()) {
            LOG(DEBUG, "Notify: " << client_id_ << " Send notification to "
              << it->get()->ToString());
            REST::SendResponse(it->get(), json);
        }
    }
    http_sessions_.clear();
}

// typedef std::map<HttpSession *, ClientId> HttpSessionMap;
// HttpSessionMap http_session_map_;
void RESTClientSession::OnHttpSessionEvent(HttpSession* session,
                                           enum TcpSession::Event event) {
    if (event == TcpSession::CLOSE) {
        if (!http_sessions_.erase(session))
            LOG(ERROR, "Unable to find client session");
    }
}

void RESTClientSession::AddMonitoringHttpSession(HttpSession* session) {
    http_sessions_.insert(session);
    session->RegisterEventCb(boost::bind(&RESTClientSession::OnHttpSessionEvent,
                                         this, _1, _2));
    if (changed_)
        Notify();
}

ResultCode RESTClientSession::AddBFDConnection(const SessionKey &key,
                                               const SessionConfig &config) {
    Session *session = GetSession(key);
    if (session)
        return kResultCode_Ok;
    Discriminator discriminator;
    ResultCode result = server_->ConfigureSession(key, config, &discriminator);
    session = GetSession(key);
    if (!session)
      return kResultCode_Error;
    session->RegisterChangeCallback(client_id_,
        boost::bind(&RESTClientSession::Notify, this));
    Notify();
    return result;
}

ResultCode RESTClientSession::DeleteBFDConnection(const SessionKey &key) {
    Session *session = GetSession(key);
    if (!session)
        return kResultCode_UnknownSession;
    ResultCode result = server_->RemoveSessionReference(key);
    return result;
}

RESTClientSession::~RESTClientSession() {
    for (Sessions::iterator it = bfd_sessions_.begin();
         it != bfd_sessions_.end(); ++it) {
      Session *session = server_->SessionByKey(*it);
      session->UnregisterChangeCallback(client_id_);
      server_->RemoveSessionReference(*it);
    }
}

}  // namespace BFD
