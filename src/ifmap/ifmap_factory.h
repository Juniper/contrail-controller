#ifndef __IFMAP__IFMAP_FACTORY_H__
#define __IFMAP__IFMAP_FACTORY_H__

#include <boost/function.hpp>
#include "base/factory.h"

class ConfigCassandraClient;
class ConfigClientManager;
class ConfigJsonParser;
class EventManager;
class IFMapChannelManager;
class IFMapConfigOptions;
class IFMapServer;
class IFMapXmppChannel;
class XmppChannel;

class IFMapFactory : public Factory<IFMapFactory> {
    FACTORY_TYPE_N3(IFMapFactory, IFMapXmppChannel, XmppChannel *,
                    IFMapServer *, IFMapChannelManager *);
    FACTORY_TYPE_N5(IFMapFactory, ConfigCassandraClient, ConfigClientManager *,
                    EventManager *, const IFMapConfigOptions &,
                    ConfigJsonParser *, int);
};

#endif
