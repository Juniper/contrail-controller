/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__bgp_config_listener__
#define __ctrlplane__bgp_config_listener__

#include "ifmap/ifmap_config_listener.h"

#include "bgp/bgp_config.h"

class BgpConfigManager;

//
// Vide: IFMapConfigListener description
//
class BgpConfigListener : public IFMapConfigListener {
public:
    BgpConfigListener(BgpConfigManager *manager);

private:
    friend class BgpConfigListenerTest;

    void DependencyTrackerInit();

    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};

#endif /* defined(__ctrlplane__bgp_config_listener__) */
