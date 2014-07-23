/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__xmpp_factory__
#define __ctrlplane__xmpp_factory__

#include "base/factory.h"

class TcpServer;
class XmppChannelConfig;
class XmppChannelMux;
class XmppConnection;
class XmppClient;
class XmppClientConnection;
class XmppServer;
class XmppServerConnection;
class XmppStateMachine;

class XmppObjectFactory : public Factory<XmppObjectFactory> {
    FACTORY_TYPE_N2(XmppObjectFactory, XmppServerConnection,
                    XmppServer *, const XmppChannelConfig *);
    FACTORY_TYPE_N2(XmppObjectFactory, XmppClientConnection,
                    XmppClient *, const XmppChannelConfig *);
    FACTORY_TYPE_N2(XmppObjectFactory, XmppStateMachine,
                    XmppConnection *, bool);
    FACTORY_TYPE_N1(XmppObjectFactory, XmppChannelMux, XmppConnection *);
};

#endif /* defined(__ctrlplane__xmpp_factory__) */
