/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_message_builder_h
#define ctrlplane_message_builder_h

#include "bgp/bgp_ribout.h"

class BgpRoute;

class Message {
public:
    Message() : num_reach_route_(0), num_unreach_route_(0) {
    }
    virtual ~Message();
    // Returns true if the route was successfully added to the message.
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr) = 0;
    virtual void Finish() = 0;
    virtual const uint8_t *GetData(IPeerUpdate *peer_update, size_t *lenp) = 0;
    uint32_t num_reach_routes() const { 
        return num_reach_route_; 
    }
    uint32_t num_unreach_routes() const { 
        return num_unreach_route_; 
    }
protected:
    uint32_t num_reach_route_;
    uint32_t num_unreach_route_;
};

class MessageBuilder {
public:
    virtual Message *Create(const BgpTable *table,
                            const RibOutAttr *roattr,
                            const BgpRoute *route) const = 0;
    static MessageBuilder *GetInstance(RibExportPolicy::Encoding encoding);
};

#endif
