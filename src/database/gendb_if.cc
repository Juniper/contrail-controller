/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <database/gendb_if.h>

using namespace GenDb;

class DbDataValueSizeVisitor : public boost::static_visitor<> {
 public:
    DbDataValueSizeVisitor() :
        size_(0) {
    }
    template<typename T>
    void operator()(const T &t) {
        size_ += sizeof(t);
    }
    void operator()(const std::string &str) {
        size_ += str.length();
    }
    void operator()(const boost::blank &blank) {
        size_ += 0;
    }
    void operator()(const boost::uuids::uuid &uuid) {
        size_ += uuid.size();
    }
    size_t GetSize() const {
        return size_;
    }
    size_t size_;
};

size_t NewCol::GetSize() const {
    DbDataValueSizeVisitor size_visitor;
    // Name
    std::for_each(name->begin(), name->end(),
        boost::apply_visitor(size_visitor));
    // Value
    std::for_each(value->begin(), value->end(),
        boost::apply_visitor(size_visitor));
    return size_visitor.GetSize();
}

size_t ColList::GetSize() const {
    DbDataValueSizeVisitor size_visitor;
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

std::string ColumnNameRange::ToString() const {
    std::ostringstream ss;
    ss << "ColumnNameRange: ";
    if (IsEmpty()) {
        ss << "Empty";
        return ss.str();
    }
    if (!start_.empty()) {
        ss << "Start: " << DbDataValueVecToString(start_);;
    }
    if (!finish_.empty()) {
        ss << "Finish: " << DbDataValueVecToString(finish_);
    }
    if (count_) {
        ss << "Count: " << count_;
    }
    return ss.str();
}

std::string GenDb::DbDataValueVecToString(
    const GenDb::DbDataValueVec &v_db_value) {
    std::ostringstream ss;
    ss << "[";
    for (int i = 0; i < (int)v_db_value.size(); i++) {
        if (i) {
            ss << " ";
        }
        ss << v_db_value[i];
    }
    ss << "]";
    return ss.str();
}
