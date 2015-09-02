/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>

#include "gendb_if.h"
#include "cdb_if.h"

using namespace GenDb;

GenDbIf *GenDbIf::GenDbIfImpl(GenDbIf::DbErrorHandler hdlr,
        const std::vector<std::string> &cassandra_ips,
        const std::vector<int> &cassandra_ports,
        int analytics_ttl, std::string name, bool only_sync,
        const std::string& cassandra_user,
        const std::string& cassandra_password ) {
    return (new CdbIf(hdlr, cassandra_ips, cassandra_ports, analytics_ttl,
        name, only_sync, cassandra_user, cassandra_password));
}

size_t NewCol::GetSize() const {
    DbDataValueTypeSizeVisitor size_visitor;
    // Name
    std::for_each(name->begin(), name->end(),
        boost::apply_visitor(size_visitor));
    // Value
    std::for_each(value->begin(), value->end(),
        boost::apply_visitor(size_visitor));
    return size_visitor.GetSize();
}

size_t ColList::GetSize() const {
    DbDataValueTypeSizeVisitor size_visitor;
    // Rowkey
    std::for_each(rowkey_.begin(), rowkey_.end(),
        boost::apply_visitor(size_visitor));
    size_t size = size_visitor.GetSize();
    // Columns
    BOOST_FOREACH(const NewCol &col, columns_) {
        size += col.GetSize();
    }
    return size;
}
