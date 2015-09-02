/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"

#include "bgp/bgp_config_ifmap.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpConfigManager,
                        BgpIfmapConfigManager);

#include "bgp/xmpp_message_builder.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpXmppMessageBuilder,
                        BgpXmppMessageBuilder);
