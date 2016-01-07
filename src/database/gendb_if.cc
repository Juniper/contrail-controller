/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>

#include <base/string_util.h>
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
        ss << DbDataValueToString(v_db_value[i]);
    }
    ss << "]";
    return ss.str();
}

std::string GenDb::DbDataValueToString(const GenDb::DbDataValue &db_value) {
    std::string vstr;
    switch (db_value.which()) {
      case GenDb::DB_VALUE_STRING: {
        vstr = boost::get<std::string>(db_value);
        break;
      }
      case GenDb::DB_VALUE_UINT64: {
        uint64_t vint = boost::get<uint64_t>(db_value);
        vstr = integerToString(vint);
        break;
      }
      case GenDb::DB_VALUE_UINT32: {
        uint32_t vint = boost::get<uint32_t>(db_value);
        vstr = integerToString(vint);
        break;
      }
      case GenDb::DB_VALUE_UINT16: {
        uint16_t vint = boost::get<uint16_t>(db_value);
        vstr = integerToString(vint);
        break;
      }
      case GenDb::DB_VALUE_UINT8: {
        uint8_t vint = boost::get<uint8_t>(db_value);
        vstr = integerToString(vint);
        break;
      }
      case GenDb::DB_VALUE_UUID: {
        boost::uuids::uuid vuuid = boost::get<boost::uuids::uuid>(db_value);
        vstr = to_string(vuuid);
        break;
      }
      case GenDb::DB_VALUE_DOUBLE: {
        double vdouble = boost::get<double>(db_value);
        vstr = integerToString(vdouble);
        break;
      }
      case GenDb::DB_VALUE_INET: {
        IpAddress ip = boost::get<IpAddress>(db_value);
        boost::system::error_code ec;
        vstr = ip.to_string(ec);
        assert(ec == 0);
        break;
      }
      default: {
        assert(0);
        break;
      }
    }
    return vstr;
}
