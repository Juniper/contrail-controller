/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>

#include "config_factory.h"

template <>
ConfigFactory *Factory<ConfigFactory>::singleton_ = NULL;

#include "config_cassandra_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigCassandraPartition,
                        ConfigCassandraPartition);

#include "config_cassandra_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigCassandraClient,
                        ConfigCassandraClient);

#include "database/cassandra/cql/cql_if.h"
FACTORY_STATIC_REGISTER(ConfigFactory, CqlIf, CqlIf);

#include "config_amqp_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigAmqpChannel, ConfigAmqpChannel);

#include "config_json_parser_base.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigJsonParserBase, ConfigJsonParserBase);
