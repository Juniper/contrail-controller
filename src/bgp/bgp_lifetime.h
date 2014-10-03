/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_lifetime_h
#define ctrlplane_bgp_lifetime_h

#include "base/lifetime.h"

class BgpServer;

class BgpLifetimeManager : public LifetimeManager {
public:
    explicit BgpLifetimeManager(BgpServer *server, int task_id);
    virtual ~BgpLifetimeManager();

private:
    virtual bool MayDestroy();
    BgpServer *server_;

    DISALLOW_COPY_AND_ASSIGN(BgpLifetimeManager);
};

#endif
