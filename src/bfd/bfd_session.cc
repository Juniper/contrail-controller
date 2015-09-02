/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_session.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_connection.h"

#include <tbb/mutex.h>
#include <boost/asio.hpp>
#include <boost/random.hpp>
#include <string>
#include <algorithm>

#include "base/logging.h"

namespace BFD {

Session::Session(Discriminator localDiscriminator,
        boost::asio::ip::address remoteHost,
        EventManager *evm,
        const SessionConfig &config, Connection *communicator) :
        localDiscriminator_(localDiscriminator),
        remoteHost_(remoteHost),
        sendTimer_(TimerManager::CreateTimer(*evm->io_service(),
                                             "BFD TX timer")),
        recvTimer_(TimerManager::CreateTimer(*evm->io_service(),
                                             "BFD RX timeout")),
        currentConfig_(config),
        nextConfig_(config),
        sm_(CreateStateMachine(evm)),
        pollSequence_(false),
        communicator_(communicator),
        stopped_(false) {
    ScheduleSendTimer();
    ScheduleRecvDeadlineTimer();
    sm_->SetCallback(boost::optional<StateMachine::ChangeCb>(
        boost::bind(&Session::CallStateChangeCallbacks, this, _1)));
}

Session::~Session() {
    Stop();
}

bool Session::SendTimerExpired() {
    LOG(DEBUG, __func__);
    tbb::mutex::scoped_lock lock(mutex_);

    ControlPacket packet;
    PreparePacket(nextConfig_, &packet);
    SendPacket(&packet);

    // Workaround: Timer code isn't re-entrant
    this->sendTimer_->Reschedule(tx_interval().total_milliseconds());
    return true;
}

bool Session::RecvTimerExpired() {
    LOG(DEBUG, __func__);
    tbb::mutex::scoped_lock lock(mutex_);
    sm_->ProcessTimeout();

    return false;
}

std::string Session::toString() const {
    tbb::mutex::scoped_lock lock(mutex_);

    std::ostringstream out;
    out << "RemoteHost: " << remoteHost_ << "\n";
    out << "LocalDiscriminator: 0x" << std::hex << localDiscriminator_ << "\n";
    out << "RemoteDiscriminator: 0x" << std::hex << remoteSession_.discriminator
        << "\n";
    out << "DesiredMinTxInterval: " << currentConfig_.desiredMinTxInterval
        << "\n";
    out << "RequiredMinRxInterval: " << currentConfig_.requiredMinRxInterval
        << "\n";
    out << "RemoteMinRxInterval: " << remoteSession_.minRxInterval << "\n";

    return out.str();
}

void Session::ScheduleSendTimer() {
    TimeInterval ti = tx_interval();
    LOG(DEBUG, __func__ << " " << ti);

    sendTimer_->Start(ti.total_milliseconds(),
                      boost::bind(&Session::SendTimerExpired, this));
}

void Session::ScheduleRecvDeadlineTimer() {
    TimeInterval ti = detection_time();
    LOG(DEBUG, __func__ << ti);

    recvTimer_->Cancel();
    recvTimer_->Start(ti.total_milliseconds(),
                      boost::bind(&Session::RecvTimerExpired, this));
}

BFDState Session::local_state_non_locking() {
    return sm_->GetState();
}

BFDState Session::local_state() {
    tbb::mutex::scoped_lock lock(mutex_);

    return local_state_non_locking();
}

//  If periodic BFD Control packets are already being sent (the remote
//  system is not in Demand mode), the Poll Sequence MUST be performed by
//  setting the Poll (P) bit on those scheduled periodic transmissions;
//  additional packets MUST NOT be sent.
void Session::InitPollSequence() {
    tbb::mutex::scoped_lock lock(mutex_);

    pollSequence_ = true;
    if (local_state_non_locking() != kUp &&
        local_state_non_locking() != kAdminDown) {
        ControlPacket packet;
        PreparePacket(nextConfig_, &packet);
        SendPacket(&packet);
    }
}

void Session::PreparePacket(const SessionConfig &config,
                            ControlPacket *packet) {

    packet->state = local_state_non_locking();
    packet->poll = pollSequence_;
    packet->sender_discriminator = localDiscriminator_;
    packet->receiver_discriminator = remoteSession_.discriminator;
    packet->detection_time_multiplier = config.detectionTimeMultiplier;
    packet->desired_min_tx_interval = config.desiredMinTxInterval;
    packet->required_min_rx_interval = config.requiredMinRxInterval;
}

ResultCode Session::ProcessControlPacket(const ControlPacket *packet) {
    tbb::mutex::scoped_lock lock(mutex_);

    remoteSession_.discriminator = packet->sender_discriminator;
    if (remoteSession_.minRxInterval != packet->required_min_rx_interval) {
        // TODO(bfd) schedule timer based on previous packet
        ScheduleSendTimer();
        remoteSession_.minRxInterval = packet->required_min_rx_interval;
    }
    remoteSession_.minTxInterval = packet->desired_min_tx_interval;
    remoteSession_.detectionTimeMultiplier = packet->detection_time_multiplier;
    remoteSession_.state = packet->state;

    sm_->ProcessRemoteState(packet->state);

    // poll sequence
    if (packet->poll) {
        ControlPacket newPacket;
        PreparePacket(nextConfig_, &newPacket);
        newPacket.poll = false;  // poll & final are forbidden in single packet
        newPacket.final = true;
        SendPacket(&newPacket);
    }
    if (packet->final) {
        pollSequence_ = false;
        currentConfig_ = nextConfig_;
    }

    if (local_state_non_locking() == kUp ||
        local_state_non_locking() == kInit) {
        ScheduleRecvDeadlineTimer();
    }

    return kResultCode_Ok;
}

void Session::SendPacket(const ControlPacket *packet) {
    communicator_->SendPacket(remoteHost_, packet);
}

TimeInterval Session::detection_time() {
    return std::max(currentConfig_.requiredMinRxInterval,
                    remoteSession_.minTxInterval) *
            remoteSession_.detectionTimeMultiplier;
}

TimeInterval Session::tx_interval() {
    TimeInterval minInterval, maxInterval;

    if (local_state_non_locking() == kUp) {
        TimeInterval negotiatedInterval =
                std::max(currentConfig_.desiredMinTxInterval,
                         remoteSession_.minRxInterval);

        minInterval = negotiatedInterval * 3/4;
        if (currentConfig_.detectionTimeMultiplier == 1)
            maxInterval = negotiatedInterval * 9/10;
        else
            maxInterval = negotiatedInterval;
    } else {
        minInterval = kIdleTxInterval * 3/4;
        maxInterval = kIdleTxInterval;
    }
    boost::random::uniform_int_distribution<>
            dist(minInterval.total_microseconds(),
                 maxInterval.total_microseconds());
    return boost::posix_time::microseconds(dist(randomGen));
}

boost::asio::ip::address Session::remote_host() {
    tbb::mutex::scoped_lock lock(mutex_);
    return remoteHost_;
}

void Session::Stop() {
    tbb::mutex::scoped_lock lock(mutex_);

    if (stopped_ == false) {
        TimerManager::DeleteTimer(sendTimer_);
        TimerManager::DeleteTimer(recvTimer_);
        stopped_ = true;
        sm_->SetCallback(boost::optional<StateMachine::ChangeCb>());
    }
}

SessionConfig Session::config() {
    tbb::mutex::scoped_lock lock(mutex_);
    return nextConfig_;
}

BFDRemoteSessionState Session::remote_state() {
    tbb::mutex::scoped_lock lock(mutex_);
    return remoteSession_;
}

Discriminator Session::local_discriminator() {
    tbb::mutex::scoped_lock lock(mutex_);
    return localDiscriminator_;
}

void Session::CallStateChangeCallbacks(const BFD::BFDState &new_state) {
    for (Callbacks::const_iterator it = callbacks_.begin();
         it != callbacks_.end(); ++it) {
        it->second(new_state);
    }
}

void Session::RegisterChangeCallback(ClientId client_id,
                                     StateMachine::ChangeCb cb) {
    tbb::mutex::scoped_lock lock(mutex_);
    callbacks_[client_id] = cb;
}

void Session::UnregisterChangeCallback(ClientId client_id) {
    tbb::mutex::scoped_lock lock(mutex_);
    callbacks_.erase(client_id);
}

void Session::UpdateConfig(const SessionConfig& config) {
    // TODO(bfd) implement UpdateConfig
    LOG(ERROR, "Session::UpdateConfig not implemented");
}

int Session::reference_count() {
    tbb::mutex::scoped_lock lock(mutex_);
    return callbacks_.size();
}

}  // namespace BFD
