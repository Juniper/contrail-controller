/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_PEER_H__
#define __XMPP_PEER_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include "xmpp/xmpp_server.h"

class XmppChannel;

class XmppSamplePeer {
public:
    explicit XmppSamplePeer(XmppChannel *channel);
    virtual ~XmppSamplePeer();

    virtual std::string ToString() const;
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    XmppChannel *GetXmppChannel() { return channel_; }

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    XmppChannel *channel_;
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
};

class XmppPeerManager {
public:
    typedef std::map<const XmppChannel *, XmppSamplePeer *> XmppPeerMap;
    XmppPeerManager(XmppServer *, void *);
    virtual ~XmppPeerManager();

    typedef boost::function<void(XmppSamplePeer *)> VisitorFn;
    XmppSamplePeer *FindPeer(const XmppChannel *);
    void VisitPeers(XmppPeerManager::VisitorFn);
    virtual void XmppHandleConnectionEvent(XmppChannel *, xmps::PeerState);
    const XmppPeerMap &peer_mux_map() const { return peer_mux_map_; }

private:
    XmppPeerMap peer_mux_map_;
};


#endif // __XMPP_PEER_H__
