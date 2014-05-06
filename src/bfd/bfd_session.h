/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_SESSION_H_
#define BFD_SESSION_H_

#include "bfd/bfd_common.h"
#include "bfd/bfd_state_machine.h"

#include <string>
#include <map>
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
        detectionTimeMultiplier(0), state(kInit) {}
    Discriminator discriminator;
    TimeInterval minRxInterval;
    TimeInterval minTxInterval;
    int detectionTimeMultiplier;
    BFDState state;
};

class BFDSession {
 private:
    mutable tbb::mutex mutex_;
    Discriminator localDiscriminator_;
    boost::asio::ip::address remoteHost_;
    Timer *sendTimer_;
    Timer *recvTimer_;
    BFDSessionConfig currentConfig_;
    BFDSessionConfig nextConfig_;
    BFDRemoteSessionState remoteSession_;
    boost::scoped_ptr<StateMachine> sm_;
    bool pollSequence_;
    Connection *communicator_;
    bool stopped_;
    tbb::atomic<int> refcount_;

    bool SendTimerExpired();
    bool RecvTimerExpired();
    void ScheduleSendTimer();
    void ScheduleRecvDeadlineTimer();
    void PreparePacket(const BFDSessionConfig &config, ControlPacket *packet);
    void SendPacket(const ControlPacket *packet);
    BFDState LocalStateNoLock();

    //TODO integrate with refcount?
    typedef std::map<ClientId, StateMachine::ChangeCb> Callbacks;
    Callbacks callbacks_;
    void StateChangeCallback(const BFD::BFDState &new_state);

 public:
    BFDSession(Discriminator localDiscriminator,
            boost::asio::ip::address remoteHost,
            EventManager *evm,
            const BFDSessionConfig &config, Connection *communicator) :
                localDiscriminator_(localDiscriminator), remoteHost_(remoteHost),
                    sendTimer_(TimerManager::CreateTimer(*evm->io_service(), "BFD TX timer")),
                    recvTimer_(TimerManager::CreateTimer(*evm->io_service(), "BFD RX timeout")),

                    currentConfig_(config),
                    nextConfig_(config),
                    sm_(CreateStateMachine(evm)),
                    pollSequence_(false),
                    communicator_(communicator),
                    stopped_(false) {
        ScheduleSendTimer();
        ScheduleRecvDeadlineTimer();
        refcount_ = 1;
        sm_->SetCallback(boost::optional<StateMachine::ChangeCb>(boost::bind(&BFDSession::StateChangeCallback, this, _1)));
    }

    void Stop();

    ~BFDSession() {
        Stop();
    }


    void increment_ref();

    int decrement_ref();

    std::string toString() const;
    ResultCode ProcessControlPacket(const ControlPacket *packet);
    boost::asio::ip::address RemoteHost();
    BFDState LocalState();
    void InitPollSequence();
    BFDSessionConfig Config();
    BFDRemoteSessionState RemoteState();
    Discriminator LocalDiscriminator();
    void UpdateConfig(const BFDSessionConfig& config);

    TimeInterval DetectionTime();
    TimeInterval TxInterval();


    void RegisterChangeCallback(ClientId client_id, StateMachine::ChangeCb cb);
    void UnregisterChangeCallback(ClientId client_id);
};

}  // namespace BFD

#endif /* BFD_SESSION_H_ */
