/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_SESSION_H_
#define BFD_SESSION_H_

#include "bfd/bfd_common.h"
#include "bfd/bfd_state_machine.h"

#include <string>
#include <boost/scoped_ptr.hpp>
#include <boost/asio/ip/address.hpp>
#include "base/timer.h"
#include "tbb/mutex.h"
#include "io/event_manager.h"

namespace BFD {

class Connection;
class BFDSessionConfig;
class ControlPacket;

class BFDSessionConfig {
 public:
    TimeInterval desiredMinTxInterval;
    TimeInterval requiredMinRxInterval;
    int detectionTimeMultiplier;
};

class BFDRemoteSessionState {
 public:
    BFDRemoteSessionState() : discriminator(0),
        minRxInterval(boost::posix_time::seconds(0)), minTxInterval(boost::posix_time::seconds(0)),
        detectionTimeMultiplier(0) {}
    Discriminator discriminator;
    TimeInterval minRxInterval;
    TimeInterval minTxInterval;
    int detectionTimeMultiplier;
};

class BFDSession {
 private:
    mutable tbb::mutex mutex_;
    Discriminator localDiscriminator_;
    boost::asio::ip::address remoteHost_;
    Timer *sendTimer_;
    Timer *recvTimer_;
    const BFDSessionConfig *currentConfig_;
    const BFDSessionConfig *nextConfig_;
    BFDRemoteSessionState remoteSession_;
    boost::scoped_ptr<StateMachine> sm_;
    bool pollSequence_;
    Connection *communicator_;
    bool stopped_;

    bool SendTimerExpired();
    bool RecvTimerExpired();
    void ScheduleSendTimer();
    void ScheduleRecvDeadlineTimer();
    void PreparePacket(const BFDSessionConfig *config, ControlPacket *packet);
    void SendPacket(const ControlPacket *packet);

 public:
    BFDSession(Discriminator localDiscriminator,
            boost::asio::ip::address remoteHost,
            EventManager *evm,
            const BFDSessionConfig *config, Connection *communicator) :
                localDiscriminator_(localDiscriminator), remoteHost_(remoteHost),
                    sendTimer_(TimerManager::CreateTimer(*evm->io_service(), "BFD TX timer")),
                    recvTimer_(TimerManager::CreateTimer(*evm->io_service(), "BFD RX timeout")),

                    currentConfig_(config),
                    nextConfig_(config),
                    sm_(CreateStateMachine()),
                    pollSequence_(false),
                    communicator_(communicator),
                    stopped_(false) {
        ScheduleSendTimer();
    }

    void Stop();

    ~BFDSession() {
        Stop();
    }

    std::string toString() const;
    ResultCode ProcessControlPacket(const ControlPacket *packet);
    boost::asio::ip::address RemoteHost();

    BFDState LocalState();
    TimeInterval DetectionTime();
    TimeInterval TxInterval();
    void InitPollSequence();
};

}  // namespace BFD

#endif /* BFD_SESSION_H_ */
