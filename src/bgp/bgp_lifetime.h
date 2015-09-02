/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_LIFETIME_H_
#define SRC_BGP_BGP_LIFETIME_H_

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

#endif  // SRC_BGP_BGP_LIFETIME_H_
