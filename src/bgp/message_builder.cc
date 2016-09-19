/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/message_builder.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/bgp_factory.h"

BgpMessageBuilder *MessageBuilder::bgp_message_builder_;
BgpXmppMessageBuilder *MessageBuilder::xmpp_message_builder_;

MessageBuilder *MessageBuilder::GetInstance(
    RibExportPolicy::Encoding encoding) {
    if (encoding == RibExportPolicy::BGP) {
        if (bgp_message_builder_ == NULL) {
            bgp_message_builder_ =
                    BgpObjectFactory::Create<BgpMessageBuilder>();
        }
        return bgp_message_builder_;
    } else if (encoding == RibExportPolicy::XMPP) {
        if (xmpp_message_builder_ == NULL) {
            xmpp_message_builder_=
                    BgpObjectFactory::Create<BgpXmppMessageBuilder>();
        }
        return xmpp_message_builder_;
    }
    return NULL;
}
