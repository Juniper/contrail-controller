/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_BFD_SESSION_H_
#define SRC_BFD_BFD_SESSION_H_

#include "bfd/bfd_common.h"
#include "bfd/bfd_state_machine.h"

#include <string>
#include <map>
#include <boost/scoped_ptr.hpp>
#include <boost/asio/ip/address.hpp>

#include "base/timer.h"
#include "io/event_manager.h"

namespace BFD {
class Connection;
struct SessionConfig;
struct ControlPacket;

struct BFDRemoteSessionState {
    BFDRemoteSessionState() : discriminator(0),
        minRxInterval(boost::posix_time::seconds(1)),
        minTxInterval(boost::posix_time::seconds(0)),
        detectionTimeMultiplier(1),
        state(kDown) {}

    Discriminator discriminator;
    TimeInterval minRxInterval;
    TimeInterval minTxInterval;
    int detectionTimeMultiplier;
    BFDState state;
};

struct BFDStats {
    BFDStats(): rx_count(0), tx_count(0),
        rx_error_count(0), tx_error_count(0),
        receive_timer_expired_count(0),
        send_timer_expired_count(0) {}

    int rx_count;
    int tx_count;
    int rx_error_count;
    int tx_error_count;
    uint32_t receive_timer_expired_count;
    uint32_t send_timer_expired_count;
};

class Session {
 public:
    Session(Discriminator localDiscriminator, const SessionKey &key,
            EventManager *evm, const SessionConfig &config,
            Connection *communicator);
    virtual ~Session();

    void Stop();
    ResultCode ProcessControlPacket(const ControlPacket *packet);
    void InitPollSequence();
    void RegisterChangeCallback(ClientId client_id, ChangeCb cb);
    void UnregisterChangeCallback(ClientId client_id);
    void UpdateConfig(const SessionConfig& config);

    std::string               toString() const;
    const SessionKey &        key() const;
    BFDState                  local_state() const;
    SessionConfig             config() const;
    BFDRemoteSessionState     remote_state() const;
    Discriminator             local_discriminator() const;
    bool                      Up() const;
    BFDStats &                Stats() { return stats_; }

    TimeInterval detection_time();
    TimeInterval tx_interval();

    // Yields number of registered callbacks.
    // Server::SessionManager will delete a Session instance if its
    // reference count drops to zero.
    int reference_count();

 protected:
    bool RecvTimerExpired();

 private:
    typedef std::map<ClientId, ChangeCb> Callbacks;

    bool SendTimerExpired();
    void ScheduleSendTimer();
    void ScheduleRecvDeadlineTimer();
    void PreparePacket(const SessionConfig &config, ControlPacket *packet);
    void SendPacket(const ControlPacket *packet);
    void CallStateChangeCallbacks(const SessionKey &key,
                                  const BFD::BFDState &new_state);
    boost::asio::ip::udp::endpoint GetRandomLocalEndPoint() const;
    uint16_t GetRandomLocalPort() const;
    BFDState local_state_non_locking() const;

    Discriminator            localDiscriminator_;
    SessionKey               key_;
    Timer                    *sendTimer_;
    Timer                    *recvTimer_;
    SessionConfig            currentConfig_;
    SessionConfig            nextConfig_;
    BFDRemoteSessionState    remoteSession_;
    boost::scoped_ptr<StateMachine> sm_;
    bool                     pollSequence_;
    Connection               *communicator_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    bool                     started_;
    bool                     stopped_;
    Callbacks                callbacks_;
    BFDStats                 stats_;
};

}  // namespace BFD

#endif  // SRC_BFD_BFD_SESSION_H_
