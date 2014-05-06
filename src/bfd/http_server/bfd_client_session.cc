/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <base/logging.h>

#include <http/http_session.h>
#include "bfd_client_session.h"
#include <bfd/http_server/bfd_json_config.h>
#include <bfd/bfd_session.h>

namespace BFD {

ClientSession::ClientSession(BFDServer* server, ClientId client_id) :
        client_id_(client_id), server_(server), changed_(true) {
}

BFDSession* ClientSession::GetSession(const boost::asio::ip::address& ip) {
    if (bfd_sessions_.find(ip) == bfd_sessions_.end())
        return NULL;

    return server_->SessionByAddress(ip);
}

void ClientSession::Notify() {
    LOG(DEBUG, "Notify: " << client_id_);
    if (!bfd_sessions_.empty() && http_sessions_.empty()) {
        changed_ = true;
        return;
    } else {
        changed_ = false;
    }

    BfdJsonStateMap map;
    for (BFDSessions::iterator it = bfd_sessions_.begin(); it != bfd_sessions_.end(); ++it) {
        BFDSession *session = GetSession(*it);
        map.states[session->RemoteHost()] = session->LocalState();
    }

    std::string json;
    map.EncodeJsonString(&json);

    for (HttpSessionSet::iterator it = http_sessions_.begin(); it != http_sessions_.end(); ++it) {
        if (false == it->get()->IsClosed()) {
            LOG(DEBUG, "Notify: " << client_id_ << " Send notification to " << it->get()->ToString());
            SendResponse(it->get(), json);
        }
    }

    http_sessions_.clear();
}

void ClientSession::AddMonitoringHttpSession(HttpSession* session) {
    http_sessions_.insert(session);
    if (changed_)
        Notify();
}

ResultCode ClientSession::AddBFDConnection(const boost::asio::ip::address& remoteHost, const BFDSessionConfig& config) {
    if (bfd_sessions_.find(remoteHost) != bfd_sessions_.end()) {
        //TODO update
        return kResultCode_Error;
    }
    Discriminator discriminator;
    ResultCode result = server_->CreateSession(remoteHost, config, &discriminator);
    bfd_sessions_.insert(remoteHost);

    BFDSession *session = GetSession(remoteHost);
    if (NULL == session)
        return kResultCode_Error;
    session->RegisterChangeCallback(client_id_, boost::bind(&ClientSession::Notify, this));
    Notify();

    return result;
}

ResultCode ClientSession::DeleteBFDConnection(const boost::asio::ip::address& remoteHost) {
    if (bfd_sessions_.find(remoteHost) == bfd_sessions_.end()) {
        return kResultCode_UnkownSession;
    }
    ResultCode result = server_->RemoveSession(remoteHost);
    bfd_sessions_.erase(remoteHost);

    return result;
}

ClientSession::~ClientSession() {
    for (BFDSessions::iterator it = bfd_sessions_.begin(); it != bfd_sessions_.end(); ++it) {
        BFDSession *session = server_->SessionByAddress(*it);
        session->UnregisterChangeCallback(client_id_);
        server_->RemoveSession(*it);
    }
}

}  // namespace BFD
