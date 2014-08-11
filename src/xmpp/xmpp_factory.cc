/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp_factory.h"

template <>
XmppObjectFactory *Factory<XmppObjectFactory>::singleton_ = NULL;

#include "xmpp_connection.h"
FACTORY_STATIC_REGISTER(XmppObjectFactory, XmppServerConnection,
                        XmppServerConnection);
FACTORY_STATIC_REGISTER(XmppObjectFactory, XmppClientConnection,
                        XmppClientConnection);
FACTORY_STATIC_REGISTER(XmppObjectFactory, XmppChannelMux,
                        XmppChannelMux);

#include "xmpp_lifetime.h"
FACTORY_STATIC_REGISTER(XmppObjectFactory, XmppLifetimeManager,
                        XmppLifetimeManager);

#include "xmpp_state_machine.h"
FACTORY_STATIC_REGISTER(XmppObjectFactory, XmppStateMachine,
                        XmppStateMachine);
