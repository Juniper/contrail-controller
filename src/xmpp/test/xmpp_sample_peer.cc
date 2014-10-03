/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include "xmpp/test/xmpp_sample_peer.h"
#include <pugixml/pugixml.hpp>
#include "base/util.h"
#include "base/logging.h"
#include "xml/xml_pugi.h"

XmppSamplePeer::XmppSamplePeer(XmppChannel *channel) 
    : channel_(channel) {
        channel_->RegisterReceive(xmps::OTHER, 
                boost::bind(&XmppSamplePeer::ReceiveInternal, this, _1));
}

XmppSamplePeer::~XmppSamplePeer() {
    if (channel_) {
        channel_->UnRegisterReceive(xmps::OTHER);
        channel_ = NULL;
        LOG(DEBUG, "Unregister Receive and Deleting XmppSamplePeer");
    }
}

bool XmppSamplePeer::SendUpdate(const uint8_t *msg, size_t size) {
    if (!channel_) return false;
    return channel_->Send(msg, size, 
         xmps::OTHER, boost::bind(&XmppSamplePeer::WriteReadyCb, this, _1));
}

void XmppSamplePeer::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {

}

void XmppSamplePeer::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveUpdate(msg);
}

std::string XmppSamplePeer::ToString() const {
    return channel_->ToString();
}

void XmppSamplePeer::WriteReadyCb(const boost::system::error_code &ec) {
}

XmppPeerManager::XmppPeerManager(XmppServer *xmpp_server, void *server) {
     xmpp_server->RegisterConnectionEvent(xmps::BGP,
         boost::bind(&XmppPeerManager::XmppHandleConnectionEvent, 
                     this, _1, _2));
}

XmppPeerManager::~XmppPeerManager() {
    BOOST_FOREACH(XmppPeerMap::value_type &i, peer_mux_map_) {
        delete i.second;
    }
    peer_mux_map_.clear();
}

XmppSamplePeer *XmppPeerManager::FindPeer(const XmppChannel *mux) {
    XmppPeerMap::iterator it = peer_mux_map_.find(mux);
    if (it == peer_mux_map_.end())
        return NULL;
    return (*it).second;
}

void XmppPeerManager::VisitPeers(XmppPeerManager::VisitorFn fn) {
    BOOST_FOREACH(XmppPeerMap::value_type &i, peer_mux_map_) {
        fn(i.second);
    }
}

void XmppPeerManager::XmppHandleConnectionEvent(XmppChannel *mux,
                                                xmps::PeerState state) {
    if (state == xmps::READY) {
        XmppSamplePeer *peer = new XmppSamplePeer(mux);
        peer_mux_map_.insert(std::make_pair(mux, peer));

    } else if (state == xmps::NOT_READY) {
        XmppPeerMap::iterator it = peer_mux_map_.find(mux);
        if (it != peer_mux_map_.end()) {
            delete (*it).second;
            peer_mux_map_.erase(it);
        }
    }
}
