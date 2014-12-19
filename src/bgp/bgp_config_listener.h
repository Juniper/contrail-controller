/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_CONFIG_LISTENER_H_
#define SRC_BGP_BGP_CONFIG_LISTENER_H_

#include "ifmap/ifmap_config_listener.h"

#include "bgp/bgp_config.h"

class BgpConfigManager;

//
// Vide: IFMapConfigListener description
//
class BgpConfigListener : public IFMapConfigListener {
public:
    explicit BgpConfigListener(BgpConfigManager *manager);

private:
    friend class BgpConfigListenerTest;

    void DependencyTrackerInit();

    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};

#endif  // SRC_BGP_BGP_CONFIG_LISTENER_H_
