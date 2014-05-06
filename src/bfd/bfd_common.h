/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_COMMON_H_
#define BFD_COMMON_H_

#include <string>

#include <boost/date_time.hpp>
#include <boost/random/taus88.hpp>

namespace BFD {

typedef uint32_t Discriminator;
typedef boost::posix_time::time_duration TimeInterval;
typedef uint32_t ClientId;

enum BFDState {
    kAdminDown, kDown, kInit, kUp,
};

//TODO move to .cpp file
static std::string BFDState_str[] = {
        "AdminDown", "Down", "Init", "Up",
};
static const int BFDState_str_len = sizeof(BFDState_str) / sizeof(BFDState_str[0]);

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

//TODO move to cpp file
inline std::ostream& operator<<(std::ostream& out, const BFDState& state) {
    if (state < BFDState_str_len) {
        out << BFDState_str[state];
    } else {
        out << "Unknown";
    }
    return out;
}

inline bool BFDStateFromString(std::string str, BFDState *state) {
    for (int i = 0; i < BFDState_str_len; ++i) {
        if (BFDState_str[i] == str) {
            *state = (BFDState)i;
            return true;
        }
    }
    return false;
}

static const int kMinimalPacketLength = 24;
static const TimeInterval kIdleTxInterval = boost::posix_time::seconds(1);
extern boost::random::taus88 randomGen;
}  // namespace BFD

#endif /* BFD_COMMON_H_ */
