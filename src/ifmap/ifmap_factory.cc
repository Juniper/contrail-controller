/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>

#include "ifmap/ifmap_factory.h"

template <>
IFMapFactory *Factory<IFMapFactory>::singleton_ = NULL;

#include "ifmap/ifmap_xmpp.h"
FACTORY_STATIC_REGISTER(IFMapFactory, IFMapXmppChannel, IFMapXmppChannel);

#include "ifmap/client/config_cassandra_client.h"
FACTORY_STATIC_REGISTER(IFMapFactory, ConfigCassandraClient,
                        ConfigCassandraClient);

#include "database/cassandra/cql/cql_if.h"
FACTORY_STATIC_REGISTER(IFMapFactory, CqlIf, CqlIf);

#include "ifmap/client/config_amqp_client.h"
FACTORY_STATIC_REGISTER(IFMapFactory, ConfigAmqpChannel, ConfigAmqpChannel);
