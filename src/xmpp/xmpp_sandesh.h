/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef XMPP_SANDESH_H_
#define XMPP_SANDESH_H_

#include <sandesh/sandesh.h>

class XmppServer;

struct XmppSandeshContext : public SandeshContext {
    XmppSandeshContext() : xmpp_server(NULL), test_mode(false) {
    }

    XmppServer *xmpp_server;
    bool test_mode;
};

#endif /* XMPP_SANDESH_H_ */
