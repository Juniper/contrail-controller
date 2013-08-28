/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_SANDESH_H_
#define BGP_SANDESH_H_

#include <sandesh/sandesh.h>

class BgpServer;
class BgpXmppChannelManager;
class IFMapServer;

struct BgpSandeshContext : public SandeshContext {
    BgpServer *bgp_server;
    BgpXmppChannelManager *xmpp_peer_manager;
    IFMapServer *ifmap_server;
};

#endif /* BGP_SANDESH_H_ */
