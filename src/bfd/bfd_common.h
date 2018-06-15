/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef SRC_BFD_BFD_COMMON_H_
#define SRC_BFD_BFD_COMMON_H_

#include <ostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/date_time.hpp>
#include <boost/random/taus88.hpp>

#include "base/util.h"

namespace BFD {
typedef uint32_t Discriminator;
typedef boost::posix_time::time_duration TimeInterval;
typedef uint32_t ClientId;

enum BFDState {
    kAdminDown, kDown, kInit, kUp
};

enum Port {
    kSingleHop = 3784,
    kMultiHop = 4784,
    kSendPortMin = 49152,
    kSendPortMax = 65535,
};

enum AuthType {
    kReserved,
    kSimplePassword,
    kKeyedMD5,
    kMeticulousKeyedMD5,
    kKeyedSHA1,
    kMeticulousKeyedSHA1,
};

enum ResultCode {
    kResultCode_Ok,
    kResultCode_UnknownSession,
    kResultCode_Error,
    kResultCode_InvalidPacket,
    kResultCode_NotImplemented,
};

enum Diagnostic {
    kNoDiagnostic,
    kControlDetectionTimeExpired,
    kEchoFunctionFailed,
    kNeighborSignaledSessionDown,
    kForwardingPlaneReset,
    kPathDown,
    kConcatenatedPathDown,
    kAdministrativelyDown,
    kReverseConcatenatedPathDown,
    kDiagnosticFirstInvalid
};

std::ostream &operator<<(std::ostream &, enum BFDState);
boost::optional<BFDState> BFDStateFromString(const char *);

struct SessionIndex {
public:
    SessionIndex(uint32_t if_index = 0, uint32_t vrf_index = 0) :
            if_index(if_index), vrf_index(vrf_index) { }
    bool operator<(const SessionIndex &other) const {
        BOOL_KEY_COMPARE(if_index, other.if_index);
        BOOL_KEY_COMPARE(vrf_index, other.vrf_index);
        return false;
    }

    const std::string to_string() const {
        std::ostringstream os;
        os << "if_index:" << if_index << ", vrf_index: " << vrf_index;
        return os.str();
    }

    uint32_t if_index;
    uint32_t vrf_index;
};

struct SessionKey {
public:
    SessionKey(const boost::asio::ip::address &remote_address,
               const SessionIndex &session_index = SessionIndex(),
               uint16_t remote_port = kSingleHop,
               const boost::asio::ip::address &local_address =
                   boost::asio::ip::address()) :
            local_address(local_address),
            remote_address(remote_address), index(session_index),
            remote_port(remote_port) {
    }

    SessionKey() : remote_port(kSingleHop) { }

    bool operator<(const SessionKey &other) const {
        BOOL_KEY_COMPARE(local_address, other.local_address);
        BOOL_KEY_COMPARE(remote_address, other.remote_address);
        BOOL_KEY_COMPARE(remote_port, other.remote_port);
        BOOL_KEY_COMPARE(index, other.index);
        return false;
    }

    const std::string to_string() const {
        std::ostringstream os;
        os << local_address << ":" << remote_address << ":";
        os << index.to_string() << ":" << ":" << remote_port;
        return os.str();
    }

    boost::asio::ip::address local_address;
    boost::asio::ip::address remote_address;
    SessionIndex index; // InterfaceIndex or VrfIndex
    uint16_t remote_port;
};

struct SessionConfig {
    SessionConfig() : desiredMinTxInterval(boost::posix_time::seconds(1)),
        requiredMinRxInterval(boost::posix_time::seconds(0)),
        detectionTimeMultiplier(1) {}

    TimeInterval desiredMinTxInterval;  // delay
    TimeInterval requiredMinRxInterval; // timeout
    int detectionTimeMultiplier;        // max-retries ?
};

typedef boost::function<void(const SessionKey &key,
                             const BFD::BFDState &state)> ChangeCb;
extern const int kMinimalPacketLength;
extern const TimeInterval kIdleTxInterval;
extern boost::random::taus88 randomGen;
}  // namespace BFD

#endif  // SRC_BFD_BFD_COMMON_H_
