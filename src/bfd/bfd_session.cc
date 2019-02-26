/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_session.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_connection.h"

#include <boost/asio.hpp>
#include <boost/random.hpp>
#include <string>
#include <algorithm>

#include "base/logging.h"

namespace BFD {

Session::Session(Discriminator localDiscriminator,
        const SessionKey &key,
        EventManager *evm,
        const SessionConfig &config, Connection *communicator) :
        localDiscriminator_(localDiscriminator),
        key_(key),
        sendTimer_(TimerManager::CreateTimer(*evm->io_service(),
            "BFD TX", TaskScheduler::GetInstance()->GetTaskId("BFD"), 0)),
        recvTimer_(TimerManager::CreateTimer(*evm->io_service(),
            "BFD RX", TaskScheduler::GetInstance()->GetTaskId("BFD"), 0)),
        currentConfig_(config),
        nextConfig_(config),
        sm_(CreateStateMachine(evm, this)),
        pollSequence_(false),
        communicator_(communicator),
        local_endpoint_(key.local_address, GetRandomLocalPort()),
        remote_endpoint_(key.remote_address, key.remote_port),
        started_(false),
        stopped_(false) {
    ScheduleSendTimer();
    ScheduleRecvDeadlineTimer();
    sm_->SetCallback(boost::optional<ChangeCb>(
        boost::bind(&Session::CallStateChangeCallbacks, this, _1, _2)));
}

uint16_t Session::GetRandomLocalPort() const {
    boost::random::uniform_int_distribution<> dist(kSendPortMin, kSendPortMax);
    return dist(randomGen);
}

Session::~Session() {
    Stop();
}

bool Session::SendTimerExpired() {
    ControlPacket packet;

    stats_.send_timer_expired_count++;
    PreparePacket(nextConfig_, &packet);
    SendPacket(&packet);

    // Workaround: Timer code isn't re-entrant
    this->sendTimer_->Reschedule(tx_interval().total_milliseconds());
    return true;
}

bool Session::RecvTimerExpired() {

    if (local_state_non_locking() == kUp) {
        // Bfd state will transition to Down state,
        // restore to default values
        remoteSession_.minRxInterval = boost::posix_time::seconds(1);
        remoteSession_.minTxInterval = boost::posix_time::seconds(0);
    }
    sm_->ProcessTimeout();
    stats_.receive_timer_expired_count++;

    return false;
}

std::string Session::toString() const {
    std::ostringstream out;
    out << "SessionKey: " << key_.to_string() << "\n";
    out << "LocalDiscriminator: 0x" << std::hex << localDiscriminator_ << "\n";
    out << "RemoteDiscriminator: 0x" << std::hex << remoteSession_.discriminator
        << "\n";
    out << "DesiredMinTxInterval: " << currentConfig_.desiredMinTxInterval
        << "\n";
    out << "RequiredMinRxInterval: " << currentConfig_.requiredMinRxInterval
        << "\n";
    out << "RemoteMinRxInterval: " << remoteSession_.minRxInterval << "\n";
    out << "RemoteMinTxInterval: " << remoteSession_.minTxInterval << "\n";
    out << "Local State:" << local_state() << "\n";
    out << "Remote State:" << remote_state().state << "\n";

    return out.str();
}

void Session::ScheduleSendTimer() {
    int elapsed_time_ms;
    int remaining_time_ms;
    TimeInterval ti = tx_interval();

    // get the elapsed time only if the bfd session timer is running,
    // otherwise program the config send timer value
    if (started_ == true) {
        elapsed_time_ms = sendTimer_->GetElapsedTime();
        sendTimer_->Cancel();
        if (elapsed_time_ms < 0) {
            remaining_time_ms = 0;
        } else {
            remaining_time_ms = ti.total_milliseconds() - elapsed_time_ms;
        }
    } else {
        // timer not yet started, program with config value
        remaining_time_ms = ti.total_milliseconds();
    }

    if (remaining_time_ms > 0) {
        sendTimer_->Start(remaining_time_ms,
                boost::bind(&Session::SendTimerExpired, this));
    } else {
        // fire the timer now!
        sendTimer_->Start(0,
                boost::bind(&Session::SendTimerExpired, this));
    }
    if (started_ != true) {
        started_ = true;
    }
}

void Session::ScheduleRecvDeadlineTimer() {
    TimeInterval ti = detection_time();

    recvTimer_->Cancel();
    recvTimer_->Start(ti.total_milliseconds(),
                      boost::bind(&Session::RecvTimerExpired, this));
}

BFDState Session::local_state_non_locking() const {
    return sm_->GetState();
}

BFDState Session::local_state() const {
    return local_state_non_locking();
}

//  If periodic BFD Control packets are already being sent (the remote
//  system is not in Demand mode), the Poll Sequence MUST be performed by
//  setting the Poll (P) bit on those scheduled periodic transmissions;
//  additional packets MUST NOT be sent.
void Session::InitPollSequence() {
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
    TimeInterval oldMinRxInterval = remoteSession_.minRxInterval;
    remoteSession_.discriminator = packet->sender_discriminator;
    remoteSession_.detectionTimeMultiplier = packet->detection_time_multiplier;
    remoteSession_.state = packet->state;
    if ((local_state_non_locking() == kUp) && packet->poll) {
        remoteSession_.minTxInterval = packet->desired_min_tx_interval;
        if (packet->required_min_rx_interval < remoteSession_.minRxInterval) {
            remoteSession_.minRxInterval = packet->required_min_rx_interval;
            ScheduleSendTimer();
        } else {
            // After sending the BFD pkt with previous agreed rate, update
            // the SendTimer() with new remoteSession_.minRxInterval so as to
            // not impact the remote Session's detection time.
            remoteSession_.minRxInterval = packet->required_min_rx_interval;
        }
    } else if (local_state_non_locking() == kInit ||
               local_state_non_locking() == kDown) {
        remoteSession_.minRxInterval = packet->required_min_rx_interval;
        remoteSession_.minTxInterval = packet->desired_min_tx_interval;
        remoteSession_.detectionTimeMultiplier = 
                                     packet->detection_time_multiplier;
        if (packet->required_min_rx_interval.total_microseconds() && 
                oldMinRxInterval >= (packet->required_min_rx_interval * 10)) {
            // reschedule the sendtimer to the new value
            ScheduleSendTimer();
        }
    }

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
    boost::asio::mutable_buffer buffer =
        boost::asio::mutable_buffer(new uint8_t[kMinimalPacketLength],
                                    kMinimalPacketLength);
    int pktSize = EncodeControlPacket(packet,
        boost::asio::buffer_cast<uint8_t *>(buffer), kMinimalPacketLength);
    if (pktSize != kMinimalPacketLength) {
        LOG(ERROR,
           "Unable to encode packet: pktSize " << pktSize
           << ", session: " << toString());
        stats_.tx_error_count++;
        const uint8_t *p = boost::asio::buffer_cast<const uint8_t *>(buffer);
        delete[] p;
    } else {
        communicator_->SendPacket(local_endpoint_, remote_endpoint_,
                                  key_.index, buffer, pktSize);
        stats_.tx_count++;
    }
}

TimeInterval Session::detection_time() {
    return std::max(currentConfig_.requiredMinRxInterval,
                    remoteSession_.minTxInterval) *
            remoteSession_.detectionTimeMultiplier;
}

TimeInterval Session::tx_interval() {
    TimeInterval minInterval, maxInterval;

    TimeInterval negotiatedInterval =
        std::max(currentConfig_.desiredMinTxInterval,
                remoteSession_.minRxInterval);

    minInterval = negotiatedInterval * 3/4;
    if (currentConfig_.detectionTimeMultiplier == 1) {
        maxInterval = negotiatedInterval * 9/10;
    } else {
        maxInterval = negotiatedInterval;
    }

    boost::random::uniform_int_distribution<>
                dist(minInterval.total_microseconds(),
                maxInterval.total_microseconds());
    return boost::posix_time::microseconds(dist(randomGen));
}

const SessionKey &Session::key() const {
    return key_;
}

void Session::Stop() {
    if (stopped_ == false) {
        TimerManager::DeleteTimer(sendTimer_);
        TimerManager::DeleteTimer(recvTimer_);
        stopped_ = true;
        started_ = false;
        sm_->SetCallback(boost::optional<ChangeCb>());
    }
}

SessionConfig Session::config() const {
    return nextConfig_;
}

BFDRemoteSessionState Session::remote_state() const {
    return remoteSession_;
}

Discriminator Session::local_discriminator() const {
    return localDiscriminator_;
}

void Session::CallStateChangeCallbacks(
    const SessionKey &key, const BFD::BFDState &new_state) {
    for (Callbacks::const_iterator it = callbacks_.begin();
         it != callbacks_.end(); ++it) {
        it->second(key, new_state);
    }
}

void Session::RegisterChangeCallback(ClientId client_id, ChangeCb cb) {
    callbacks_[client_id] = cb;
}

void Session::UnregisterChangeCallback(ClientId client_id) {
    callbacks_.erase(client_id);
}

void Session::UpdateConfig(const SessionConfig& config) {
    nextConfig_.desiredMinTxInterval = config.desiredMinTxInterval;
    nextConfig_.requiredMinRxInterval = config.requiredMinRxInterval;
    nextConfig_.detectionTimeMultiplier = config.detectionTimeMultiplier;
    pollSequence_ = true;
}

int Session::reference_count() {
    return callbacks_.size();
}

bool Session::Up() const {
    return local_state() == kUp;
}

}  // namespace BFD
