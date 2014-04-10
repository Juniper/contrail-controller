/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "gendb_if.h"
#include "cdb_if.h"

using namespace GenDb;

GenDbIf *GenDbIf::GenDbIfImpl(GenDbIf::DbErrorHandler hdlr,
        std::string cassandra_ip, unsigned short cassandra_port,
        int analytics_ttl, std::string name, bool only_sync) {
    return (new CdbIf(hdlr, cassandra_ip, cassandra_port, analytics_ttl,
        name, only_sync));
}

