/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_xmpp_lifetime_h
#define ctrlplane_xmpp_lifetime_h

#include "base/lifetime.h"

class XmppLifetimeManager : public LifetimeManager {
public:
    explicit XmppLifetimeManager(int task_id);
    virtual ~XmppLifetimeManager();

private:
    DISALLOW_COPY_AND_ASSIGN(XmppLifetimeManager);
};

#endif
