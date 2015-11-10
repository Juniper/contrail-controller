/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <database/gendb_if.h>

using namespace GenDb;

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
