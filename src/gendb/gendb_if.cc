/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "gendb_if.h"
#include "cdb_if.h"

using namespace GenDb;

GenDbIf *GenDbIf::GenDbIfImpl(GenDbIf::DbErrorHandler hdlr,
        const std::vector<std::string> &cassandra_ips,
        const std::vector<int> &cassandra_ports,
        int analytics_ttl, std::string name, bool only_sync) {
    return (new CdbIf(hdlr, cassandra_ips, cassandra_ports, analytics_ttl,
        name, only_sync));
}

