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

Session* RESTClientSession::GetSession(const boost::asio::ip::address& ip) {
    if (bfd_sessions_.find(ip) == bfd_sessions_.end())
        return NULL;

    return server_->SessionByAddress(ip);
}

void RESTClientSession::Notify() {
    LOG(DEBUG, "Notify: " << client_id_);
    if (!bfd_sessions_.empty() && http_sessions_.empty()) {
        changed_ = true;
        return;
    } else {
        changed_ = false;
    }

    REST::JsonStateMap map;
    for (Sessions::iterator it = bfd_sessions_.begin();
         it != bfd_sessions_.end(); ++it) {
        Session *session = GetSession(*it);
        map.states[session->remote_host()] = session->local_state();
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

void RESTClientSession::AddMonitoringHttpSession(HttpSession* session) {
    http_sessions_.insert(session);
    if (changed_)
        Notify();
}

ResultCode RESTClientSession::AddBFDConnection(
                                const boost::asio::ip::address& remoteHost,
                                const SessionConfig& config) {
    if (bfd_sessions_.find(remoteHost) != bfd_sessions_.end()) {
        // TODO(bfd) implement REST configuration update
        return kResultCode_Error;
    }

    Discriminator discriminator;
    ResultCode result =
      server_->ConfigureSession(remoteHost, config, &discriminator);
    bfd_sessions_.insert(remoteHost);

    Session *session = GetSession(remoteHost);
    if (NULL == session)
      return kResultCode_Error;
    session->RegisterChangeCallback(client_id_,
      boost::bind(&RESTClientSession::Notify, this));
    Notify();

    return result;
}

ResultCode RESTClientSession::DeleteBFDConnection(
                    const boost::asio::ip::address& remoteHost) {
    if (bfd_sessions_.find(remoteHost) == bfd_sessions_.end()) {
        return kResultCode_UnknownSession;
    }

    ResultCode result = server_->RemoveSessionReference(remoteHost);
    bfd_sessions_.erase(remoteHost);

    return result;
}

RESTClientSession::~RESTClientSession() {
    for (Sessions::iterator it = bfd_sessions_.begin();
         it != bfd_sessions_.end(); ++it) {
      Session *session = server_->SessionByAddress(*it);
      session->UnregisterChangeCallback(client_id_);
      server_->RemoveSessionReference(*it);
    }
}

}  // namespace BFD
