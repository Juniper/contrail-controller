/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_state_machine.h"
#include "bfd/bfd_common.h"

#include <boost/foreach.hpp>
#include <boost/random/mersenne_twister.hpp>

#include "base/logging.h"
#include "io/event_manager.h"

namespace BFD {

Server::Server(EventManager *evm, Connection *communicator) :
        evm_(evm),
        communicator_(communicator),
        session_manager_(evm),
        event_queue_(new WorkQueue<Event *>(
                     TaskScheduler::GetInstance()->GetTaskId("BFD"), 0,
                     boost::bind(&Server::EventCallback, this, _1))) {
    communicator->SetServer(this);
}

Server::~Server() {
}

void Server::AddSession(const SessionKey &key, const SessionConfig &config,
                        ChangeCb cb) {
    EnqueueEvent(new Event(ADD_CONNECTION, key, config, cb));
}

void Server::AddSession(Event *event) {
    CHECK_CONCURRENCY("BFD");
    Discriminator discriminator;
    ConfigureSession(event->key, event->config, &discriminator);
    sessions_.insert(event->key);
    Session *session = SessionByKey(event->key);
    if (session) {
        session->RegisterChangeCallback(0, event->cb);
        event->cb(session->key(), session->local_state());
    }
}

void Server::DeleteSession(const SessionKey &key) {
    EnqueueEvent(new Event(DELETE_CONNECTION, key));
}

void Server::DeleteSession(Event *event) {
    CHECK_CONCURRENCY("BFD");
    int erase_size = sessions_.erase(event->key);
    if (erase_size) {
        RemoveSessionReference(event->key);
    } else {
        LOG(ERROR, __func__ <<  "Cannot find session: " <<
            event->key.to_string());
    }
}

void Server::DeleteClientSessions() {
    EnqueueEvent(new Event(DELETE_CLIENT_CONNECTIONS));
}

void Server::DeleteClientSessions(Event *event) {
    CHECK_CONCURRENCY("BFD");
    for (Sessions::iterator it = sessions_.begin(), next;
         it != sessions_.end(); it = next) {
        SessionKey key = *it;
        next = ++it;
        sessions_.erase(key);
        RemoveSessionReference(key);
    }
}

void Server::EnqueueEvent(Event *event) {
    event_queue_->Enqueue(event);
}

bool Server::EventCallback(Event *event) {
    switch (event->type) {
    case ADD_CONNECTION:
        AddSession(event);
        break;
    case DELETE_CONNECTION:
        DeleteSession(event);
        break;
    case DELETE_CLIENT_CONNECTIONS:
        DeleteClientSessions(event);
        break;
    case PROCESS_PACKET:
        ProcessControlPacket(event);
        break;
    }
    delete event;
    return true;
}

Session* Server::GetSession(const ControlPacket *packet) {
    CHECK_CONCURRENCY("BFD");
    if (packet->receiver_discriminator) {
        return session_manager_.SessionByDiscriminator(
                packet->receiver_discriminator);
    }

    SessionIndex session_index;
    if (packet->local_endpoint.port() == kSingleHop) {
        session_index.if_index = packet->session_index.if_index;
    } else {
        session_index.vrf_index = packet->session_index.vrf_index;
    }

    // Use ifindex for single hop and vrfindex for multihop sessions.
    Session *session = session_manager_.SessionByKey(
        SessionKey(packet->remote_endpoint.address(), session_index,
                   packet->local_endpoint.port(),
                   packet->local_endpoint.address()));

    // Try with 0.0.0.0 local address
    if (!session) {
        session = session_manager_.SessionByKey(
            SessionKey(packet->remote_endpoint.address(), session_index,
                       packet->local_endpoint.port()));
    }
    return session;
}

Session *Server::SessionByKey(const boost::asio::ip::address &address,
        const SessionIndex &index) {
    return session_manager_.SessionByKey(SessionKey(address, index));
}

Session *Server::SessionByKey(const SessionKey &key) const {
    return session_manager_.SessionByKey(key);
}

Session *Server::SessionByKey(const SessionKey &key) {
    return session_manager_.SessionByKey(key);
}

void Server::ProcessControlPacket(
        const boost::asio::ip::udp::endpoint &local_endpoint,
        const boost::asio::ip::udp::endpoint &remote_endpoint,
        const SessionIndex &session_index,
        const boost::asio::const_buffer &recv_buffer,
        std::size_t bytes_transferred, const boost::system::error_code& error) {
    EnqueueEvent(new Event(PROCESS_PACKET, local_endpoint, remote_endpoint,
                           session_index, recv_buffer, bytes_transferred));
}

void Server::ProcessControlPacket(Event *event) {
    if (event->bytes_transferred != (std::size_t) kMinimalPacketLength) {
        LOG(ERROR, __func__ <<  "Wrong packet size: " <<
            event->bytes_transferred);
        return;
    }

    boost::scoped_ptr<ControlPacket> packet(ParseControlPacket(
        boost::asio::buffer_cast<const uint8_t *>(event->recv_buffer),
        event->bytes_transferred));
    if (packet == NULL) {
        LOG(ERROR, __func__ <<  "Unable to parse packet");
        return;
    }
    packet->local_endpoint = event->local_endpoint;
    packet->remote_endpoint = event->remote_endpoint;
    packet->session_index = event->session_index;
    ProcessControlPacketActual(packet.get());
    delete[] boost::asio::buffer_cast<const uint8_t *>(event->recv_buffer);
}

ResultCode Server::ProcessControlPacketActual(const ControlPacket *packet) {
    ResultCode result;
    result = packet->Verify();
    if (result != kResultCode_Ok) {
        LOG(ERROR, "Wrong packet: " << result);
        return result;
    }
    Session *session = NULL;
    session = GetSession(packet);
    if (session == NULL) {
        LOG(ERROR, "Unknown session: " <<
            packet->remote_endpoint.address().to_string() << "/" <<
            packet->receiver_discriminator);
        return kResultCode_UnknownSession;
    }
    session->Stats().rx_count++;
    result = session->ProcessControlPacket(packet);
    if (result != kResultCode_Ok) {
        LOG(ERROR, "Unable to process session: result " << result
                << ", session: " << session->toString());
        session->Stats().rx_error_count++;
        return result;
    }
    return kResultCode_Ok;
}

ResultCode Server::ConfigureSession(const SessionKey &key,
                                    const SessionConfig &config,
                                    Discriminator *assignedDiscriminator) {
    return session_manager_.ConfigureSession(key, config, communicator_,
                                             assignedDiscriminator);
}

ResultCode Server::RemoveSessionReference(const SessionKey &key) {
    return session_manager_.RemoveSessionReference(key);
}

Session *Server::SessionManager::SessionByDiscriminator(
    Discriminator discriminator) {
    DiscriminatorSessionMap::const_iterator it =
            by_discriminator_.find(discriminator);
    if (it == by_discriminator_.end())
        return NULL;
    return it->second;
}

Session *Server::SessionManager::SessionByKey(const SessionKey &key) {
    KeySessionMap::const_iterator it = by_key_.find(key);
    return it != by_key_.end() ? it->second : NULL;
}

Session *Server::SessionManager::SessionByKey(const SessionKey &key) const {
    KeySessionMap::const_iterator it = by_key_.find(key);
    return it != by_key_.end() ? it->second : NULL;
}

ResultCode Server::SessionManager::RemoveSessionReference(
        const SessionKey &key) {
    Session *session = SessionByKey(key);
    if (session == NULL) {
        LOG(DEBUG, __FUNCTION__ << " No such session: " << key.to_string());
        return kResultCode_UnknownSession;
    }

    if (!--refcounts_[session]) {
        by_discriminator_.erase(session->local_discriminator());
        by_key_.erase(key);
        delete session;
    }

    return kResultCode_Ok;
}

ResultCode Server::SessionManager::ConfigureSession(const SessionKey &key,
        const SessionConfig &config, Connection *communicator,
        Discriminator *assignedDiscriminator) {
    Session *session = SessionByKey(key);
    if (session) {
        session->UpdateConfig(config);

        LOG(INFO, __func__ << ": UpdateConfig : "
                  << session->key().to_string() << "/"
                  << session->local_discriminator());

        return kResultCode_Ok;
    }

    *assignedDiscriminator = GenerateUniqueDiscriminator();
    session = new Session(*assignedDiscriminator, key, evm_, config,
                          communicator);

    by_discriminator_[*assignedDiscriminator] = session;
    by_key_[key] = session;
    refcounts_[session] = 1;

    LOG(INFO, __func__ << ": New session configured: " << key.to_string() << "/"
              << *assignedDiscriminator);

    return kResultCode_Ok;
}

Discriminator Server::SessionManager::GenerateUniqueDiscriminator() {
    class DiscriminatorGenerator {
     public:
        DiscriminatorGenerator() {
            next_ = gen()%0x1000000 + 1;
        }

        Discriminator Next() {
            return next_.fetch_and_increment();
        }

     private:
        tbb::atomic<Discriminator> next_;
        boost::random::mt19937 gen;
    };

    static DiscriminatorGenerator generator;

    return generator.Next();
}

Server::SessionManager::~SessionManager() {
    for (DiscriminatorSessionMap::iterator it = by_discriminator_.begin();
         it != by_discriminator_.end(); ++it) {
        it->second->Stop();
        delete it->second;
    }
}
}  // namespace BFD
