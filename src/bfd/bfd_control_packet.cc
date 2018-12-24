/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_control_packet.h"

#include <string>
#include <list>
#include <boost/mpl/list.hpp>

#include "base/proto.h"

namespace BFD {
class VersionAndDiagnostic: public ProtoElement<VersionAndDiagnostic> {
 public:
    static const int kSize = 1;

    static const int kVersionBitmask = 0xE0;
    static const int kVersionOffset = 5;
    static const int kDiagnosticBitmask = 0x1F;
    static const int kSupportedVersion = 1;

    static bool Verifier(const ControlPacket *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return ((value & kVersionBitmask) >>
                    kVersionOffset == kSupportedVersion)
                && ((value & kDiagnosticBitmask) < kDiagnosticFirstInvalid);
    }
};

class Flags: public ProtoElement<Flags> {
 public:
    static const int kSize = 1;

    static const int kStateBitmask = 0xC0;
    static const int kStateOffset = 6;
    static const int kPollOffset = 5;
    static const int kFinallOffset = 4;
    static const int kControlPlaneIndependentOffset = 3;
    static const int kAuthenticationPresentOffset = 2;
    static const int kDemandOffset = 1;
    static const int kMultipointOffset = 0;
};

class DetectionTimeMultiplier: public ProtoElement<DetectionTimeMultiplier> {
 public:
    static const int kSize = 1;

    static bool Verifier(const ControlPacket *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return value > 0;
    }
};

class Length: public ProtoElement<Length> {
 public:
    static const int kSize = 1;

    static bool Verifier(const void *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return value == kMinimalPacketLength;
    }
};

class SenderDiscriminator: public ProtoElement<SenderDiscriminator> {
 public:
    static const int kSize = 4;

    static bool Verifier(const ControlPacket *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint32_t value = get_value(data, 4);
        return value > 0;
    }
};

class ReceiverDiscriminator: public ProtoElement<ReceiverDiscriminator> {
 public:
    static const int kSize = 4;
};

class DesiredMinTxInterval: public ProtoElement<DesiredMinTxInterval> {
 public:
    static const int kSize = 4;
};

class RequiredMinRxInterval: public ProtoElement<RequiredMinRxInterval> {
 public:
    static const int kSize = 4;
};

class RequiredMinEchoRXInterval :
            public ProtoElement<RequiredMinEchoRXInterval> {
 public:
    static const int kSize = 4;
};

class ControlPacketMessage: public ProtoSequence<ControlPacketMessage> {
 public:
    typedef mpl::list<VersionAndDiagnostic, Flags, DetectionTimeMultiplier,
                      Length, SenderDiscriminator, ReceiverDiscriminator,
                      DesiredMinTxInterval, RequiredMinRxInterval,
                      RequiredMinEchoRXInterval> Sequence;
};

std::string ControlPacket::toString() const {
    std::ostringstream out;
    out << "Length: " << length << "\n";
    out << "Diagnostic: " << diagnostic << "\n";
    out << "State: " << state << "\n";
    out << "Poll: " << poll << "\n";
    out << "Final: " << final << "\n";
    out << "ControlPlaneIndependent: " << control_plane_independent << "\n";
    out << "AuthenticationPresent: " << authentication_present << "\n";
    out << "Demand: " << demand << "\n";
    out << "Multipoint: " << multipoint << "\n";
    out << "DetectionTimeMultiplier: " << detection_time_multiplier << "\n";
    out << "SenderDiscriminator: 0x"
        << std::hex << sender_discriminator << "\n";
    out << "ReceiverDiscriminator: 0x" << std::hex << receiver_discriminator
        << "\n";
    out << "DesiredMinTxInterval: " << desired_min_tx_interval << "\n";
    out << "RequiredMinRxInterval: " << required_min_rx_interval << "\n";
    out << "RequiredMinEchoRXInterval: "
        << required_min_echo_rx_interval << "\n";
    return out.str();
}

ResultCode ControlPacket::Verify() const {
    if (multipoint)
        return kResultCode_InvalidPacket;

    if (authentication_present)
        return kResultCode_NotImplemented;

    if (receiver_discriminator == 0 && state == kUp)
        return kResultCode_InvalidPacket;

    if (poll && final)
        return kResultCode_InvalidPacket;

    return kResultCode_Ok;
}

ControlPacket* ParseControlPacket(const uint8_t *data, size_t size) {
    ParseContext context;
    int result = ControlPacketMessage::Parse(data, size, &context,
                                             new ControlPacket());
    if (result < 0 || (size_t)result != size) {
        return NULL;
    }
    return static_cast<ControlPacket *>(context.release());
}

int EncodeControlPacket(const ControlPacket *msg, uint8_t *data, size_t size) {
    EncodeContext context;
    return ControlPacketMessage::Encode(&context, msg, data, size);
}

bool operator==(const ControlPacket &p1, const ControlPacket &p2) {
    return p1.poll == p2.poll && p1.final == p2.final
            && p1.control_plane_independent == p2.control_plane_independent
            && p1.authentication_present == p2.authentication_present
            && p1.demand == p2.demand && p1.multipoint == p2.multipoint
            && p1.detection_time_multiplier == p2.detection_time_multiplier
            && p1.length == p2.length
            && p1.sender_discriminator == p2.sender_discriminator
            && p1.receiver_discriminator == p2.receiver_discriminator
            && p1.diagnostic == p2.diagnostic && p1.state == p2.state
            && p1.desired_min_tx_interval == p2.desired_min_tx_interval
            && p1.required_min_rx_interval == p2.required_min_rx_interval
            && p1.required_min_echo_rx_interval ==
                p2.required_min_echo_rx_interval;
}
}  // namespace BFD
