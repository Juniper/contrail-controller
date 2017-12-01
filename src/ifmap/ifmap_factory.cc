/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifdef _WINDOWS
#include <boost/asio.hpp>
#include <windows.h>
 //This is required due to a dependency between boost and winsock that will result in:
 //fatal error C1189: #error :  WinSock.h has already been included
#endif
#include <vector>

#include "ifmap/ifmap_factory.h"

template <>
IFMapFactory *Factory<IFMapFactory>::singleton_ = NULL;

#include "ifmap/ifmap_xmpp.h"
FACTORY_STATIC_REGISTER(IFMapFactory, IFMapXmppChannel, IFMapXmppChannel);

#include "database/cassandra/cql/cql_if.h"
FACTORY_STATIC_REGISTER(IFMapFactory, CqlIf, CqlIf);

