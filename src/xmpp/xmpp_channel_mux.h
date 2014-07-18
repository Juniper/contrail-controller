/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CHANNEL_MUX_H__
#define __XMPP_CHANNEL_MUX_H__

#include <boost/system/error_code.hpp>
#include <tbb/mutex.h>
#include "xmpp/xmpp_channel.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_state_machine.h"

class XmppConnection;

class XmppChannelMux : public XmppChannel {
public:
    explicit XmppChannelMux(XmppConnection *); 
    virtual ~XmppChannelMux();

    virtual bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb);
    virtual void RegisterReceive(xmps::PeerId, ReceiveCb);
    virtual void UnRegisterReceive(xmps::PeerId);
    size_t ReceiverCount() const;

    virtual std::string ToString() const;
    virtual std::string StateName() const;
    virtual xmps::PeerState GetPeerState() const;
    virtual std::string FromString() const;

    virtual std::string LastStateName() const;
    virtual std::string LastStateChangeAt() const;
    virtual std::string LastEvent() const;
    virtual uint32_t rx_open() const;
    virtual uint32_t rx_close() const;
    virtual uint32_t rx_update() const;
    virtual uint32_t rx_keepalive() const;
    virtual uint32_t tx_open() const;
    virtual uint32_t tx_close() const;
    virtual uint32_t tx_update() const;
    virtual uint32_t tx_keepalive() const;
    virtual uint32_t FlapCount() const;
    virtual std::string LastFlap() const;

    virtual void ProcessXmppMessage(const XmppStanza::XmppMessage *msg);
    void WriteReady(const boost::system::error_code &ec);

    void HandleStateEvent(xmsm::XmState state);

    virtual const XmppConnection *connection() const { return connection_; }
    virtual XmppConnection *connection() { return connection_; }

protected:
    friend class XmppChannelMuxMock;

private:
    void RegisterWriteReady(xmps::PeerId, SendReadyCb);
    void UnRegisterWriteReady(xmps::PeerId id); 

    typedef std::map<xmps::PeerId, SendReadyCb> WriteReadyCbMap;
    typedef std::map<xmps::PeerId, ReceiveCb> ReceiveCbMap;

    WriteReadyCbMap map_;
    ReceiveCbMap rxmap_;
    SendReadyCb cb_;
    XmppConnection *connection_;
    tbb::mutex mutex_;
};

#endif // __XMPP_CHANNEL_MUX_H__
