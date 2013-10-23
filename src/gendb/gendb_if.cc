/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "gendb_if.h"
#include "cdb_if.h"

GenDb::NewCol::NewCol(const std::string& n, const DbDataValue& v, int ttl) :
    cftype_(NewCf::COLUMN_FAMILY_SQL), ttl(ttl) {
        name.push_back(n);
        value.push_back(v);
}

GenDb::GenDbIf *GenDbIf::GenDbIfImpl(boost::asio::io_service *ioservice,
        DbErrorHandler hdlr, std::string cassandra_ip,
        unsigned short cassandra_port, bool enable_stats, int analytics_ttl, std::string name) {
    return (new CdbIf(ioservice, hdlr, cassandra_ip, cassandra_port, enable_stats, analytics_ttl, name));
}

