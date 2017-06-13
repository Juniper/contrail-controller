/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/random.hpp>
#include <set>

#include "base/logging.h"
#include "bfd/bfd_client.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"

using namespace BFD;
using boost::bind;
using std::make_pair;
using std::pair;

Client::Client(Connection *cm, ClientId id) : id_(id), cm_(cm) {
}

Client::~Client() {
    DeleteClientConnections();
}

void Client::DeleteClientConnections() {
    cm_->GetServer()->DeleteClientConnections(id_);
}

Session *Client::GetSession(const SessionKey &key) const {
    return cm_->GetServer()->SessionByKey(key);
}

bool Client::Up(const SessionKey &key) const {
    Session *session = GetSession(key);
    return session && session->Up();
}

void Client::AddConnection(const SessionKey &key, const SessionConfig &config) {
    cm_->GetServer()->AddConnection(key, config, bind(&Client::Notify, this, _1,
                                                      _2));
}

void Client::DeleteConnection(const SessionKey &key) {
    cm_->GetServer()->DeleteConnection(key);
}

void Client::Notify(const SessionKey &key, const BFD::BFDState &new_state) {
    cm_->NotifyStateChange(key, new_state == kUp);
}
