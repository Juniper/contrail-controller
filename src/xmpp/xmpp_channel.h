/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CHANNEL_INTERFACE_H__
#define __XMPP_CHANNEL_INTERFACE_H__

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include "xmpp/xmpp_proto.h"

class XmppConnection;

namespace xmps {
    typedef enum {
        UNKNOWN = 1,
        READY = 2,
        NOT_READY = 3
    } PeerState;

    typedef enum {
        CONFIG = 1,
        BGP = 2,
        DNS = 3,
        OTHER = 4 
    } PeerId;
}

class XmppChannel {
public:
    typedef boost::function<void(const boost::system::error_code &)> 
        SendReadyCb;
    typedef boost::function<
        void(const XmppStanza::XmppMessage *, xmps::PeerState state)
        > ReceiveCb;

    virtual ~XmppChannel() { }
    virtual bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb) = 0;
    virtual void RegisterReceive(xmps::PeerId, ReceiveCb) = 0;
    virtual void UnRegisterReceive(xmps::PeerId) = 0;
    virtual std::string ToString() const = 0;
    virtual std::string StateName() const = 0;
    virtual std::string LastStateName() const = 0;
    virtual std::string LastStateChangeAt() const = 0;
    virtual std::string LastEvent() const = 0;
    virtual uint32_t rx_open() const = 0;
    virtual uint32_t rx_close() const = 0;
    virtual uint32_t rx_update() const = 0;
    virtual uint32_t rx_keepalive() const = 0;
    virtual uint32_t tx_open() const = 0;
    virtual uint32_t tx_close() const = 0;
    virtual uint32_t tx_update() const = 0;
    virtual uint32_t tx_keepalive() const = 0;
    virtual uint32_t FlapCount() const = 0;
    virtual std::string LastFlap() const = 0;
    virtual xmps::PeerState GetPeerState() const = 0;
    virtual std::string FromString() const = 0;
    virtual const XmppConnection *connection() const = 0;
};

#endif // __XMPP_CHANNEL_INTERFACE_H__
