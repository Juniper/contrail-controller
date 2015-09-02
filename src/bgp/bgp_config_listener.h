/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP__BGP_CONFIG_LISTENER__
#define BGP__BGP_CONFIG_LISTENER__

#include "ifmap/ifmap_config_listener.h"

class BgpIfmapConfigManager;

//
// Vide: IFMapConfigListener description
//
class BgpConfigListener : public IFMapConfigListener {
public:
    explicit BgpConfigListener(BgpIfmapConfigManager *manager);

private:
    friend class BgpConfigListenerTest;

    void DependencyTrackerInit();

    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};

#endif  // BGP__BGP_CONFIG_LISTENER__
