/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_INIT__
#define __XMPP_INIT__


#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_server.h"

class XmppChannelConfig;
class XmppConfigData;

class XmppInit {
public:
    static const char *kControlNodeJID;
    static const char *kAgentNodeJID;
    static const char *kDnsNodeJID;
    static const char *kPubSubNS;
    static const char *kJIDControlBgp;
    static const char *kJIDControlDns;
    static const char *kConfigPeer;
    static const char *kBgpPeer;
    static const char *kDnsPeer;
    static const char *kOtherPeer;
    static const char *kFqnPrependAgentNodeJID;
    XmppInit();
    ~XmppInit();
    void Reset(bool keep_config = false);
    void InitClient(XmppClient *);
    void InitServer(XmppServer *, int port, bool logUVE);

    void AddXmppChannelConfig(XmppChannelConfig *); 
    XmppChannelConfig *AllocChannelConfig(const std::string &peer_ip, int port,
                                          const std::string &from, 
                                          const std::string &to, 
                                          const std::string &node, 
                                          bool isClient); 
private:
    XmppServer *g_server_;
    XmppClient *g_client_;
    XmppConfigData *cfg_;
};

#endif
