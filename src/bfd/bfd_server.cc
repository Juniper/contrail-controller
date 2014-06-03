/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_state_machine.h"
#include "bfd/bfd_common.h"

#include "base/logging.h"
#include "io/event_manager.h"

boost::random::taus88 BFD::randomGen;

namespace BFD {

BFDSession* BFDServer::GetSession(const ControlPacket *packet) {
    if (packet->receiver_discriminator)
        return sessionManager_.SessionByDiscriminator(packet->receiver_discriminator);
    else
        return sessionManager_.SessionByAddress(packet->sender_host);
}

BFDSession *BFDServer::SessionByAddress(const boost::asio::ip::address &address) {
    tbb::mutex::scoped_lock lock(mutex_);
    return sessionManager_.SessionByAddress(address);
}

ResultCode BFDServer::ProcessControlPacket(const ControlPacket *packet) {
    LOG(DEBUG, __func__ <<  " new packet: " << packet->toString() << " " << static_cast<void*>(this));
    tbb::mutex::scoped_lock lock(mutex_);

    ResultCode result;
    result = packet->Verify();
    if (result != kResultCode_Ok) {
        LOG(ERROR, "Wrong packet: " << result);
        return result;
    }
    BFDSession *session = NULL;
    session = GetSession(packet);
    if (session == NULL) {
        LOG(ERROR, "Unknown session");
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

ResultCode BFDServer::CreateSession(const boost::asio::ip::address &remoteHost, const BFDSessionConfig &config,
        Discriminator *assignedDiscriminator) {
    tbb::mutex::scoped_lock lock(mutex_);

    return sessionManager_.CreateOrUpdateSession(remoteHost, config, communicator_,  assignedDiscriminator);
}

ResultCode BFDServer::RemoveSession(const boost::asio::ip::address &remoteHost) {
    tbb::mutex::scoped_lock lock(mutex_);

    return sessionManager_.RemoveSession(remoteHost);
}

BFDSession* BFDServer::SessionManager::SessionByDiscriminator(Discriminator discriminator) {
    DiscriminatorSessionMap::const_iterator it = by_discriminator_.find(discriminator);
    if (it == by_discriminator_.end())
        return NULL;
    else
        return it->second;
}
BFDSession* BFDServer::SessionManager::SessionByAddress(const boost::asio::ip::address &address) {
    AddressSessionMap::const_iterator it = by_address_.find(address);
    if (it == by_address_.end())
        return NULL;
    else
        return it->second;
}

ResultCode BFDServer::SessionManager::RemoveSession(const boost::asio::ip::address &remoteHost) {
    BFDSession *session = SessionByAddress(remoteHost);
    if (session == NULL)
        return kResultCode_UnknownSession;

    if (session->decrement_ref() == 1) {
        by_discriminator_.erase(session->LocalDiscriminator());
        by_address_.erase(session->RemoteHost());
        session->Stop();
        delete session;
    }

    return kResultCode_Ok;
}

ResultCode BFDServer::SessionManager::CreateOrUpdateSession(const boost::asio::ip::address &remoteHost,
        const BFDSessionConfig &config,
        Connection *communicator,
        Discriminator *assignedDiscriminator) {
    BFDSession *session = SessionByAddress(remoteHost);
    if (session) {
        session->UpdateConfig(config);
        session->increment_ref();
        return kResultCode_Ok;
    }

    *assignedDiscriminator = GenerateUniqDiscriminator();
    session = new BFDSession(*assignedDiscriminator, remoteHost, evm_, config, communicator);
    by_discriminator_[*assignedDiscriminator] = session;
    by_address_[remoteHost] = session;

    return kResultCode_Ok;
}

class DiscriminatorGenerator {
    tbb::atomic<Discriminator> next_;

 public:
    DiscriminatorGenerator() {
        next_ = random()%0x1000000 + 1;
    }
    Discriminator Next() {
        return next_.fetch_and_increment();
    }
};

static DiscriminatorGenerator generator;

Discriminator BFDServer::SessionManager::GenerateUniqDiscriminator() {
    return generator.Next();
}

BFDServer::SessionManager::~SessionManager() {
    for (DiscriminatorSessionMap::iterator it = by_discriminator_.begin(); it != by_discriminator_.end(); ++it) {
        it->second->Stop();
        delete it->second;
    }
    by_discriminator_.clear();
    by_address_.clear();
}
}  // namespace BFD
