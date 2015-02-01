/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_SANDESH_H_
#define BGP_SANDESH_H_

#include <sandesh/sandesh.h>
#include "xmpp/xmpp_sandesh.h"

class BgpServer;
class BgpXmppChannelManager;
class IFMapServer;

struct BgpSandeshContext : public XmppSandeshContext {
    BgpSandeshContext()
        : XmppSandeshContext(),
          bgp_server(NULL),
          xmpp_peer_manager(NULL),
          ifmap_server(NULL), unit_test_mode(false) {
    }
    void set_unit_test_mode(bool val) { unit_test_mode = val; }
    bool get_unit_test_mode() { return unit_test_mode; }

    BgpServer *bgp_server;
    BgpXmppChannelManager *xmpp_peer_manager;
    IFMapServer *ifmap_server;
    bool unit_test_mode;
};

#endif /* BGP_SANDESH_H_ */
