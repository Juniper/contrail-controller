/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_lifetime.h"

#include "bgp/bgp_server.h"

BgpLifetimeManager::BgpLifetimeManager(BgpServer *server, int task_id)
    : LifetimeManager(task_id), server_(server) {
}

BgpLifetimeManager::~BgpLifetimeManager() {
}

bool BgpLifetimeManager::MayDestroy() {
    return server_->IsReadyForDeletion();
}
