/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_CONTROL_PACKET_H_
#define BFD_CONTROL_PACKET_H_

#include "bfd/bfd_common.h"

#include <string>
#include <boost/asio/ip/address.hpp>
#include "base/parse_object.h"

class ParseContext;
class EncodeContext;

namespace BFD {

class ControlPacket : public ParseObject {
 public:
    bool poll;
    bool final;
    bool control_plane_independent;
    bool authentication_present;
    bool demand;
    bool multipoint;
    int detection_time_multiplier;
    int length;
    BFD::Discriminator sender_discriminator;
    BFD::Discriminator receiver_discriminator;

    BFD::Diagnostic diagnostic;
    BFD::BFDState state;

    TimeInterval desired_min_tx_interval;
    TimeInterval required_min_rx_interval;
    TimeInterval required_min_echo_rx_interval;

    boost::asio::ip::address sender_host;

    std::string toString() const;

    ResultCode Verify() const;
};

ControlPacket* ParseControlPacket(const uint8_t *data, size_t size);
int EncodeControlPacket(const ControlPacket *msg, uint8_t *data, size_t size);

bool operator==(const ControlPacket &p1, const ControlPacket &p2);
}  // namespace BFD

#endif /* BFD_CONTROL_PACKET_H_ */
