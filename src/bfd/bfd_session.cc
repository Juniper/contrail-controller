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
bool BFDSession::SendTimerExpired() {
    LOG(DEBUG, __func__);
    tbb::mutex::scoped_lock lock(mutex_);

    ControlPacket packet;
    PreparePacket(nextConfig_, &packet);
    SendPacket(&packet);

    // Workaround: Timer code isn't re-entrant
    this->sendTimer_->Reschedule(TxInterval().total_milliseconds());
    return true;
}


bool BFDSession::RecvTimerExpired() {
    LOG(DEBUG, __func__);
    tbb::mutex::scoped_lock lock(mutex_);
    sm_->ProcessTimeout();

    return false;
}


std::string BFDSession::toString() const {
    tbb::mutex::scoped_lock lock(mutex_);

    std::ostringstream out;
    out << "RemoteHost: " << remoteHost_ << "\n";
    out << "LocalDiscriminator: 0x" << std::hex << localDiscriminator_ << "\n";
    out << "RemoteDiscriminator: 0x" << std::hex << remoteSession_.discriminator << "\n";
    out << "DesiredMinTxInterval: " << currentConfig_.desiredMinTxInterval << "\n";
    out << "RequiredMinRxInterval: " << currentConfig_.requiredMinRxInterval << "\n";
    out << "RemoteMinRxInterval: " << remoteSession_.minRxInterval << "\n";

    return out.str();
}

void BFDSession::ScheduleSendTimer() {
    TimeInterval ti = TxInterval();
    LOG(DEBUG, __func__ << " " << ti);

    sendTimer_->Start(ti.total_milliseconds(),
                             boost::bind(&BFDSession::SendTimerExpired, this));
}

void BFDSession::ScheduleRecvDeadlineTimer() {
    TimeInterval ti = DetectionTime();
    LOG(DEBUG, __func__ << ti);

    recvTimer_->Cancel();
    recvTimer_->Start(ti.total_milliseconds(),
                             boost::bind(&BFDSession::RecvTimerExpired, this));
}

BFDState BFDSession::LocalStateNoLock() {
    return sm_->GetState();
}

BFDState BFDSession::LocalState() {
    tbb::mutex::scoped_lock lock(mutex_);

    return LocalStateNoLock();
}

//       If periodic BFD Control packets are already being sent (the remote
//       system is not in Demand mode), the Poll Sequence MUST be performed by
//       setting the Poll (P) bit on those scheduled periodic transmissions;
//       additional packets MUST NOT be sent.
void BFDSession::InitPollSequence() {
    tbb::mutex::scoped_lock lock(mutex_);

    pollSequence_ = true;
    if (LocalStateNoLock() != kUp && LocalStateNoLock() != kAdminDown) {
        ControlPacket packet;
        PreparePacket(nextConfig_, &packet);
        SendPacket(&packet);
    }
}

void BFDSession::PreparePacket(const BFDSessionConfig &config, ControlPacket *packet) {
    // move to ControlPacket constructor
    packet->diagnostic = kNoDiagnostic;
    packet->state = LocalStateNoLock();
    packet->poll = pollSequence_;
    packet->final = false;
    packet->control_plane_independent = false;
    packet->authentication_present = false;
    packet->demand = false;
    packet->multipoint = false;
    packet->detection_time_multiplier = config.detectionTimeMultiplier;
    packet->sender_discriminator = localDiscriminator_;
    packet->receiver_discriminator = remoteSession_.discriminator;
    packet->desired_min_tx_interval = config.desiredMinTxInterval;
    packet->required_min_rx_interval = config.requiredMinRxInterval;
    packet->required_min_echo_rx_interval = boost::posix_time::microseconds(0);

    packet->length = kMinimalPacketLength;
}

ResultCode BFDSession::ProcessControlPacket(const ControlPacket *packet) {
    tbb::mutex::scoped_lock lock(mutex_);

    remoteSession_.discriminator = packet->sender_discriminator;
    if (remoteSession_.minRxInterval != packet->required_min_rx_interval) {
        // TODO schedule from previous packet
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

    if (LocalStateNoLock() == kUp) {
        ScheduleRecvDeadlineTimer();
    }

    return kResultCode_Ok;
}

void BFDSession::SendPacket(const ControlPacket *packet) {
    communicator_->SendPacket(remoteHost_, packet);
}

TimeInterval BFDSession::DetectionTime() {
    return std::max(currentConfig_.requiredMinRxInterval,
            remoteSession_.minTxInterval) * remoteSession_.detectionTimeMultiplier;
}

TimeInterval BFDSession::TxInterval() {
    TimeInterval minInterval, maxInterval;
    if (LocalStateNoLock() == kUp) {
        TimeInterval negotiatedInterval = std::max(currentConfig_.desiredMinTxInterval, remoteSession_.minRxInterval);
        minInterval = negotiatedInterval * 3/4;
        if (currentConfig_.detectionTimeMultiplier == 1)
            maxInterval = negotiatedInterval * 9/10;
        else
            maxInterval = negotiatedInterval;
    } else {
        minInterval = kIdleTxInterval * 3/4;
        maxInterval = kIdleTxInterval;
    }
     boost::random::uniform_int_distribution<> dist(minInterval.total_microseconds(), maxInterval.total_microseconds());
     return boost::posix_time::microseconds(dist(randomGen));
}

boost::asio::ip::address BFDSession::RemoteHost() {
    tbb::mutex::scoped_lock lock(mutex_);
    return remoteHost_;
}

void BFDSession::Stop() {
    tbb::mutex::scoped_lock lock(mutex_);

    if (stopped_ == false) {
        TimerManager::DeleteTimer(sendTimer_);
        TimerManager::DeleteTimer(recvTimer_);
        stopped_ = true;
        sm_->SetCallback(boost::optional<StateMachine::ChangeCb>());
    }
}

BFDSessionConfig BFDSession::Config() {
    tbb::mutex::scoped_lock lock(mutex_);
    return nextConfig_;
}

BFDRemoteSessionState BFDSession::RemoteState() {
    tbb::mutex::scoped_lock lock(mutex_);
    return remoteSession_;
}

Discriminator BFDSession::LocalDiscriminator() {
    tbb::mutex::scoped_lock lock(mutex_);
    return localDiscriminator_;
}

void BFDSession::StateChangeCallback(const BFD::BFDState &new_state) {
    for (Callbacks::const_iterator it = callbacks_.begin(); it != callbacks_.end(); ++it) {
        it->second(new_state);
    }
}

void BFDSession::RegisterChangeCallback(ClientId client_id, StateMachine::ChangeCb cb) {
    tbb::mutex::scoped_lock lock(mutex_);
    callbacks_[client_id] = cb;
}

void BFDSession::UnregisterChangeCallback(ClientId client_id) {
    tbb::mutex::scoped_lock lock(mutex_);
    callbacks_.erase(client_id);
}

void BFDSession::UpdateConfig(const BFDSessionConfig& config) {
    //TODO
    LOG(WARN, "BFDSession::UpdateConfig not implemented");
}

void BFDSession::increment_ref() {
    LOG(DEBUG, "Session: " << RemoteHost() << " refcount: " << static_cast<int>(refcount_) << " +1");
    refcount_.fetch_and_increment();
}

int BFDSession::decrement_ref() {
    LOG(DEBUG, "Session: " << RemoteHost() << " refcount: " << static_cast<int>(refcount_) << " -1");
    return refcount_.fetch_and_decrement();
}

}  // namespace BFD
