/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_MESSAGE_BUILDER_H_
#define SRC_BGP_BGP_MESSAGE_BUILDER_H_

#include <string>

#include "bgp/bgp_proto.h"
#include "bgp/message_builder.h"

class RibOut;

class BgpMessage : public Message {
public:
    BgpMessage();
    virtual ~BgpMessage();
    virtual bool Start(const RibOut *ribout, bool cache_routes,
                       const RibOutAttr *roattr, const BgpRoute *route);
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr);
    virtual void Finish();
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp,
                                   const std::string **msg_str,
                                   std::string *temp);

private:
    virtual void Reset();
    bool StartReach(const RibOut *ribout, const RibOutAttr *roattr,
                    const BgpRoute *route);
    bool StartUnreach(const BgpRoute *route);
    bool UpdateLength(const char *tag, int size, int delta);

    const BgpTable *table_;
    EncodeOffsets encode_offsets_;
    uint8_t data_[BgpProto::kMaxMessageSize];
    size_t datalen_;

    DISALLOW_COPY_AND_ASSIGN(BgpMessage);
};

class BgpMessageBuilder : public MessageBuilder {
public:
    BgpMessageBuilder();
    virtual Message *Create() const;

private:
    DISALLOW_COPY_AND_ASSIGN(BgpMessageBuilder);
};

#endif  // SRC_BGP_BGP_MESSAGE_BUILDER_H_
