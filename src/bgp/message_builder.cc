/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/message_builder.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/xmpp_message_builder.h"

Message::~Message() {
}

MessageBuilder *MessageBuilder::GetInstance(
    RibExportPolicy::Encoding encoding) {
    if (encoding == RibExportPolicy::BGP) {
        return BgpMessageBuilder::GetInstance();
    } else if (encoding == RibExportPolicy::XMPP) {
        return BgpXmppMessageBuilder::GetInstance();
    }
    return NULL;
}
