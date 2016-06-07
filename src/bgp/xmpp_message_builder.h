/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_XMPP_MESSAGE_BUILDER_H_
#define SRC_BGP_XMPP_MESSAGE_BUILDER_H_

#include "bgp/message_builder.h"

class RibOut;

class BgpXmppMessageBuilder : public MessageBuilder {
public:
    BgpXmppMessageBuilder();
    virtual Message *Create(const RibOut *ribout, bool cache_routes,
                            const RibOutAttr *roattr,
                            const BgpRoute *route) const;

private:
    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessageBuilder);
};

#endif  // SRC_BGP_XMPP_MESSAGE_BUILDER_H_
