/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __IFMAP__IFMAP_FACTORY_H__
#define __IFMAP__IFMAP_FACTORY_H__

#include <string>
#include <vector>

#include <boost/function.hpp>
#include "base/factory.h"

namespace cass { namespace cql { class CqlIf; } }
using cass::cql::CqlIf;

class EventManager;
class IFMapChannelManager;
class IFMapConfigOptions;
class IFMapServer;
class IFMapXmppChannel;
class XmppChannel;

class IFMapFactory : public Factory<IFMapFactory> {
    FACTORY_TYPE_N3(IFMapFactory, IFMapXmppChannel, XmppChannel *,
                    IFMapServer *, IFMapChannelManager *);
    FACTORY_TYPE_N5(IFMapFactory, CqlIf, EventManager *,
                    const std::vector<std::string> &, int, const std::string &,
                    const std::string &);
};

#endif  // __IFMAP__IFMAP_FACTORY_H__
