/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_xmpp_message_builder_h
#define ctrlplane_xmpp_message_builder_h

#include "bgp/message_builder.h"

class BgpXmppMessageBuilder : public MessageBuilder {
public:
    BgpXmppMessageBuilder();
    virtual Message *Create(const BgpTable *table,
                            const RibOutAttr *roattr,
                            const BgpRoute *route) const;
    static BgpXmppMessageBuilder *GetInstance();

private:
    static BgpXmppMessageBuilder instance_;
    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessageBuilder);
};

#endif
