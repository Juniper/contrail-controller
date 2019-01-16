/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __IFMAP__IFMAP_FACTORY_H__
#define __IFMAP__IFMAP_FACTORY_H__

#include <string>
#include <vector>

#include <boost/function.hpp>
#include "base/factory.h"

class EventManager;
class IFMapChannelManager;
class IFMapServer;
class IFMapXmppChannel;
class XmppChannel;

class IFMapFactory : public Factory<IFMapFactory> {
    FACTORY_TYPE_N3(IFMapFactory, IFMapXmppChannel, XmppChannel *,
                    IFMapServer *, IFMapChannelManager *);
};

#endif  // __IFMAP__IFMAP_FACTORY_H__
