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
    ss << "Start Op: " << Op::ToString(start_op_);
    if (!finish_.empty()) {
        ss << "Finish: " << DbDataValueVecToString(finish_);
    }
    ss << "Finish Op: " << Op::ToString(finish_op_);
    if (count_) {
        ss << "Count: " << count_;
    }
    return ss.str();
}

std::string Op::ToString(Op::type op) {
    switch (op) {
      case Op::GE:
        return ">=";
      case Op::GT:
        return ">";
      case Op::LE:
        return "<=";
      case Op::LT:
        return "<";
      default:
        assert(0);
    }
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

std::string GenDb::bytes_to_hex(const uint8_t *byte_array, size_t size) {
    std::stringstream value;
    value << std::hex << std::setfill('0');
    for (unsigned int n = 0; n < size; ++n) {
      value << std::setw(2) << static_cast<unsigned>(byte_array[n]);
    }
    if (value.str().size() == 0) {
      value << "00";
    }
    return "0x" + value.str();
}

std::ostream& GenDb::operator<<(std::ostream &out, const Blob &value) {
    out << bytes_to_hex(value.data(), value.size());
    return out;
}

// DbDataValue Printer
class DbDataValuePrinter : public boost::static_visitor<std::string> {
 public:
    DbDataValuePrinter() {
    }
    template <typename IntegerType>
    std::string operator()(const IntegerType &tint) const {
        return integerToString(tint);
    }
    std::string operator()(const std::string &tstring) const {
        return tstring;
    }
    std::string operator()(const boost::uuids::uuid &tuuid) const {
        return to_string(tuuid);
    }
    std::string operator()(const IpAddress &tipaddr) const {
        boost::system::error_code ec;
        std::string ips(tipaddr.to_string(ec));
        assert(ec == 0);
        return ips;
    }
    std::string operator()(const GenDb::Blob &tblob) const {
        std::stringstream value;
        value << tblob;
        return value.str();
    }
    std::string operator()(const boost::blank &tblank) const {
        assert(false && "Empty Db Value");
        return "EMPTY DB VALUE";
    }
};

std::string GenDb::DbDataValueToString(const GenDb::DbDataValue &db_value) {
    DbDataValuePrinter vprinter;
    return boost::apply_visitor(vprinter, db_value);
}
