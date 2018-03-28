/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_MESSAGE_BUILDER_H_
#define SRC_BGP_MESSAGE_BUILDER_H_

#include <string>

#include "bgp/bgp_rib_policy.h"

class BgpMessageBuilder;
class BgpRoute;
class BgpTable;
class BgpXmppMessageBuilder;
class IPeerUpdate;
class RibOutAttr;
class RibOut;

class Message {
public:
    Message() : num_reach_route_(0), num_unreach_route_(0) { }
    virtual ~Message() { }
    virtual bool Start(const RibOut *ribout, bool cache_routes,
        const RibOutAttr *roattr, const BgpRoute *route) = 0;
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr) = 0;
    virtual void Finish() = 0;
    virtual const uint8_t *GetData(IPeerUpdate *peer_update, size_t *lenp,
        const std::string **msg_str, std::string *temp) = 0;
    uint64_t num_reach_routes() const { return num_reach_route_; }
    uint64_t num_unreach_routes() const { return num_unreach_route_; }

protected:
    uint64_t num_reach_route_;
    uint64_t num_unreach_route_;

    virtual void Reset() {
        num_reach_route_ =  0;
        num_unreach_route_ = 0;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(Message);
};

class MessageBuilder {
public:
    virtual Message *Create() const = 0;
    static MessageBuilder *GetInstance(RibExportPolicy::Encoding encoding);

private:
    static BgpMessageBuilder *bgp_message_builder_;
    static BgpXmppMessageBuilder *xmpp_message_builder_;
};

#endif  // SRC_BGP_MESSAGE_BUILDER_H_
