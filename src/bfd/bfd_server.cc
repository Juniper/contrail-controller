/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_state_machine.h"
#include "bfd/bfd_common.h"

#include <boost/foreach.hpp>

#include "base/logging.h"
#include "io/event_manager.h"

namespace BFD {

Session* Server::GetSession(const ControlPacket *packet) {
    if (packet->receiver_discriminator)
        return session_manager_.SessionByDiscriminator(
                packet->receiver_discriminator);
    return session_manager_.SessionByAddress(
            packet->sender_host);
}

Session *Server::SessionByAddress(const boost::asio::ip::address &address) {
    tbb::mutex::scoped_lock lock(mutex_);
    return session_manager_.SessionByAddress(address);
}

ResultCode Server::ProcessControlPacket(const ControlPacket *packet) {
    tbb::mutex::scoped_lock lock(mutex_);

    ResultCode result;
    result = packet->Verify();
    if (result != kResultCode_Ok) {
        LOG(ERROR, "Wrong packet: " << result);
        return result;
    }
    Session *session = NULL;
    session = GetSession(packet);
    if (session == NULL) {
        LOG(ERROR, "Unknown session: " << packet->sender_host << "/"
                   << packet->receiver_discriminator);
        return kResultCode_UnknownSession;
    }
    LOG(DEBUG, "Found session: " << session->toString());
    result = session->ProcessControlPacket(packet);
    if (result != kResultCode_Ok) {
        LOG(ERROR, "Unable to process session: " << result);
        return result;
    }
    LOG(DEBUG, "Packet correctly processed");

    return kResultCode_Ok;
}

ResultCode Server::ConfigureSession(const boost::asio::ip::address &remoteHost,
                                     const SessionConfig &config,
                                     Discriminator *assignedDiscriminator) {
    tbb::mutex::scoped_lock lock(mutex_);
    return session_manager_.ConfigureSession(remoteHost, config,
                                             communicator_,
                                             assignedDiscriminator);
}

ResultCode Server::RemoveSessionReference(const boost::asio::ip::address
                                          &remoteHost) {
    tbb::mutex::scoped_lock lock(mutex_);

    return session_manager_.RemoveSessionReference(remoteHost);
}

Session* Server::SessionManager::SessionByDiscriminator(
    Discriminator discriminator) {
    DiscriminatorSessionMap::const_iterator it =
            by_discriminator_.find(discriminator);
    if (it == by_discriminator_.end())
        return NULL;
    return it->second;
}

Session* Server::SessionManager::SessionByAddress(
    const boost::asio::ip::address &address) {
    AddressSessionMap::const_iterator it = by_address_.find(address);
    if (it == by_address_.end())
        return NULL;
    else
        return it->second;
}

ResultCode Server::SessionManager::RemoveSessionReference(
    const boost::asio::ip::address &remoteHost) {

    Session *session = SessionByAddress(remoteHost);
    if (session == NULL) {
        LOG(DEBUG, __PRETTY_FUNCTION__ << " No such session: " << remoteHost);
        return kResultCode_UnknownSession;
    }

    if (!--refcounts_[session]) {
        by_discriminator_.erase(session->local_discriminator());
        by_address_.erase(session->remote_host());
        delete session;
    }

    return kResultCode_Ok;
}

ResultCode Server::SessionManager::ConfigureSession(
                const boost::asio::ip::address &remoteHost,
                const SessionConfig &config,
                Connection *communicator,
                Discriminator *assignedDiscriminator) {
    Session *session = SessionByAddress(remoteHost);
    if (session) {
        session->UpdateConfig(config);
        refcounts_[session]++;

        LOG(INFO, __func__ << ": Reference count incremented: "
                  << session->remote_host() << "/"
                  << session->local_discriminator() << ","
                  << refcounts_[session] << " refs");

        return kResultCode_Ok;
    }

    *assignedDiscriminator = GenerateUniqueDiscriminator();
    session = new Session(*assignedDiscriminator, remoteHost, evm_, config,
                          communicator);

    by_discriminator_[*assignedDiscriminator] = session;
    by_address_[remoteHost] = session;
    refcounts_[session] = 1;

    LOG(INFO, __func__ << ": New session configured: " << remoteHost << "/"
              << *assignedDiscriminator);

    return kResultCode_Ok;
}

Discriminator Server::SessionManager::GenerateUniqueDiscriminator() {
    class DiscriminatorGenerator {
     public:
        DiscriminatorGenerator() {
            next_ = random()%0x1000000 + 1;
        }

        Discriminator Next() {
            return next_.fetch_and_increment();
        }

     private:
        tbb::atomic<Discriminator> next_;
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
