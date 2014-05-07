/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_COMMON_H_
#define BFD_COMMON_H_

#include <boost/date_time.hpp>
#include <boost/random/taus88.hpp>

namespace BFD {

typedef uint32_t Discriminator;
typedef boost::posix_time::time_duration TimeInterval;

enum BFDState {
    kAdminDown, kDown, kInit, kUp,
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

static const int kMinimalPacketLength = 24;
static const TimeInterval kIdleTxInterval = boost::posix_time::seconds(1);
extern boost::random::taus88 randomGen;
}  // namespace BFD

#endif /* BFD_COMMON_H_ */
