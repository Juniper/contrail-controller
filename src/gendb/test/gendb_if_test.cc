//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <algorithm>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/variant.hpp>

#include "testing/gunit.h"

#include <base/util.h>
#include <base/logging.h>

typedef boost::variant<boost::blank, std::string, uint64_t, uint32_t,
    boost::uuids::uuid, uint8_t, uint16_t, double> DbTestVarVariant;

enum DbTestVarStructType {
    DB_TEST_VAR_TYPE_INVALID = 0,
    DB_TEST_VAR_TYPE_STRING = 1,
    DB_TEST_VAR_TYPE_UINT64 = 2,
    DB_TEST_VAR_TYPE_UINT32 = 3,
    DB_TEST_VAR_TYPE_UUID = 4,
    DB_TEST_VAR_TYPE_UINT8 = 5,
    DB_TEST_VAR_TYPE_UINT16 = 6,
    DB_TEST_VAR_TYPE_DOUBLE = 7,
    DB_TEST_VAR_TYPE_MAXVAL
};

struct DbTestVarStruct;

template<typename T>
inline const T& DbTestVarStructGet(const DbTestVarStruct *value);

template<typename T>
inline const T& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec);

struct DbTestVarStruct {
    DbTestVarStruct() : type(DB_TEST_VAR_TYPE_INVALID) {}
    DbTestVarStruct(const std::string &s) : type(DB_TEST_VAR_TYPE_STRING), str(s) {}
    DbTestVarStruct(const uint64_t &u64) : type(DB_TEST_VAR_TYPE_UINT64), u64(u64) {}
    DbTestVarStruct(const uint32_t &u32) : type(DB_TEST_VAR_TYPE_UINT32), u32(u32) {}
    DbTestVarStruct(const boost::uuids::uuid &uuid) : type(DB_TEST_VAR_TYPE_UUID),
        uuid(uuid) {}
    DbTestVarStruct(const uint8_t &u8) : type(DB_TEST_VAR_TYPE_UINT8), u8(u8) {}
    DbTestVarStruct(const uint16_t &u16) : type(DB_TEST_VAR_TYPE_UINT16), u16(u16) {}
    DbTestVarStruct(const double &d) : type(DB_TEST_VAR_TYPE_DOUBLE), d(d) {}

    inline DbTestVarStructType which() const {
        return type;
    }

    template<typename T>
    inline const T& Get(boost::system::error_code &ec) const {
        return DbTestVarStructGet<T>(this, ec);
    }

    template<typename T>
    inline const T& Get() const {
        return DbTestVarStructGet<T>(this);
    }

    friend inline bool operator==(const DbTestVarStruct& lhs,
        const DbTestVarStruct& rhs);
    friend inline bool operator<(const DbTestVarStruct& lhs,
        const DbTestVarStruct& rhs);
    friend inline std::ostream& operator<<(std::ostream& out,
        const DbTestVarStruct& value);

    DbTestVarStructType type;
    std::string str;
    uint64_t u64;
    uint32_t u32;
    boost::uuids::uuid uuid;
    uint8_t u8;
    uint16_t u16;
    double d;
};

inline bool operator==(const DbTestVarStruct& lhs, const DbTestVarStruct& rhs) {
    if (lhs.which() != rhs.which()) {
        return false;
    }
    switch (lhs.which()) {
    case DB_TEST_VAR_TYPE_STRING:
        return lhs.Get<std::string>() == rhs.Get<std::string>();
    case DB_TEST_VAR_TYPE_UINT64:
        return lhs.Get<uint64_t>() == rhs.Get<uint64_t>();
    case DB_TEST_VAR_TYPE_UINT32:
        return lhs.Get<uint32_t>() == rhs.Get<uint32_t>();
    case DB_TEST_VAR_TYPE_UUID:
        return lhs.Get<boost::uuids::uuid>() == rhs.Get<boost::uuids::uuid>();
    case DB_TEST_VAR_TYPE_UINT8:
        return lhs.Get<uint8_t>() == rhs.Get<uint8_t>();
    case DB_TEST_VAR_TYPE_UINT16:
        return lhs.Get<uint16_t>() == rhs.Get<uint16_t>();
    case DB_TEST_VAR_TYPE_DOUBLE:
        return lhs.Get<double>() == rhs.Get<double>();
    default:
        assert(0);
        return false;
    }
}

inline bool operator<(const DbTestVarStruct& lhs, const DbTestVarStruct& rhs) {
    if (lhs.which() != rhs.which()) {
        return lhs.which() < rhs.which();
    }
    switch (lhs.which()) {
    case DB_TEST_VAR_TYPE_STRING:
        return lhs.Get<std::string>() < rhs.Get<std::string>();
    case DB_TEST_VAR_TYPE_UINT64:
        return lhs.Get<uint64_t>() < rhs.Get<uint64_t>();
    case DB_TEST_VAR_TYPE_UINT32:
        return lhs.Get<uint32_t>() < rhs.Get<uint32_t>();
    case DB_TEST_VAR_TYPE_UUID:
        return lhs.Get<boost::uuids::uuid>() < rhs.Get<boost::uuids::uuid>();
    case DB_TEST_VAR_TYPE_UINT8:
        return lhs.Get<uint8_t>() < rhs.Get<uint8_t>();
    case DB_TEST_VAR_TYPE_UINT16:
        return lhs.Get<uint16_t>() < rhs.Get<uint16_t>();
    case DB_TEST_VAR_TYPE_DOUBLE:
        return lhs.Get<double>() < rhs.Get<double>();
    default:
        assert(0);
        return false;
    }
}

inline std::ostream& operator<<(std::ostream& out, const DbTestVarStruct& value) {
    switch (value.which()) {
    case DB_TEST_VAR_TYPE_STRING:
        out << value.Get<std::string>();
        break;
    case DB_TEST_VAR_TYPE_UINT64:
        out << value.Get<uint64_t>();
        break;
    case DB_TEST_VAR_TYPE_UINT32:
        out << value.Get<uint32_t>();
        break;
    case DB_TEST_VAR_TYPE_UUID:
        out << value.Get<boost::uuids::uuid>();
        break;
    case DB_TEST_VAR_TYPE_UINT8:
        // uint8_t must be handled specially because ostream sees
        // uint8_t as a text type instead of an integer type
        out << (uint16_t)value.Get<uint8_t>();
        break;
    case DB_TEST_VAR_TYPE_UINT16:
        out << value.Get<uint16_t>();
        break;
    case DB_TEST_VAR_TYPE_DOUBLE:
        out << value.Get<double>();
        break;
    default:
        assert(0);
        break;
    }
    return out;
}

template<typename T>
inline const T& DbTestVarStructGet(const DbTestVarStruct *value) {
    // Compile Error if not specialized
}

template<typename T>
inline const T& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    // Compile Error if not specialized
}

template<>
inline const std::string& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->str;
}

template<>
inline const std::string& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_STRING) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->str;
}

template<>
inline const uint64_t& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->u64;
}

template<>
inline const uint64_t& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_UINT64) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->u64;
}

template<>
inline const uint32_t& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->u32;
}

template<>
inline const uint32_t& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_UINT32) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->u32;
}

template<>
inline const boost::uuids::uuid& DbTestVarStructGet(
    const DbTestVarStruct *value) {
    return value->uuid;
}

template<>
inline const boost::uuids::uuid& DbTestVarStructGet(
    const DbTestVarStruct *value, boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_UUID) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->uuid;
}

template<>
inline const uint8_t& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->u8;
}

template<>
inline const uint8_t& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_UINT8) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->u8;
}

template<>
inline const uint16_t& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->u16;
}

template<>
inline const uint16_t& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_UINT16) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->u16;
}

template<>
inline const double& DbTestVarStructGet(const DbTestVarStruct *value) {
    return value->d;
}

template<>
inline const double& DbTestVarStructGet(const DbTestVarStruct *value,
    boost::system::error_code &ec) {
    if (value->which() != DB_TEST_VAR_TYPE_DOUBLE) {
        ec = make_error_code(boost::system::errc::invalid_argument);
    }
    return value->d;
}

class DbPerfTest : public ::testing::Test {
public:
    std::string EncodeString(const std::string &input) const {
        return input;
    }
    std::string EncodeUint64(const uint64_t &input) const {
        return integerToString(input);
    }
    std::string EncodeUint32(const uint32_t &input) const {
        return integerToString(input);
    }
    std::string EncodeUuid(const boost::uuids::uuid &input) const {
        std::ostringstream ostr;
        ostr << input;
        return ostr.str();
    }
    std::string EncodeUint8(const uint8_t &input) const {
        return integerToString(input);
    }
    std::string EncodeUint16(const uint16_t &input) const {
        return integerToString(input);
    }
    std::string EncodeDouble(const double &input) const {
        return integerToString(input);
    }
    class DbDataValueEncodeVisitor : public boost::static_visitor<> {
    public:
        DbDataValueEncodeVisitor(const DbPerfTest *test,
            std::string &result) :
            test_(test), result_(result) {
        }
        void operator()(const std::string &input) {
            result_ += test_->EncodeString(input);
        }
        void operator()(const uint64_t &input) {
            result_ += test_->EncodeUint64(input);
        }
        void operator()(const uint32_t &input) {
            result_ += test_->EncodeUint32(input);
        }
        void operator()(const boost::uuids::uuid &input) {
            result_ += test_->EncodeUuid(input);
        }
        void operator()(const uint8_t &input) {
            result_ += test_->EncodeUint8(input);
        }
        void operator()(const uint16_t &input) {
            result_ += test_->EncodeUint16(input);
        }
        void operator()(const double &input) {
            result_ += test_->EncodeDouble(input);
        }
        void operator()(const boost::blank &input) {
            assert(0);
        }
        const DbPerfTest *test_;
        std::string &result_;
    };
    static const std::string tstring_;
    static const uint64_t tu64_;
    static const uint32_t tu32_;
    static const boost::uuids::uuid tuuid_;
    static const uint8_t tu8_;
    static const uint16_t tu16_;
    static const double tdouble_;
};

const std::string DbPerfTest::tstring_("Test");
const uint64_t DbPerfTest::tu64_(123456789ULL);
const uint32_t DbPerfTest::tu32_(123456789);
const boost::uuids::uuid DbPerfTest::tuuid_ = boost::uuids::random_generator()();
const uint8_t DbPerfTest::tu8_(128);
const uint16_t DbPerfTest::tu16_(65535);
const double DbPerfTest::tdouble_(1.0);


TEST_F(DbPerfTest, DISABLED_VariantVector) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarVariant> columns;
        columns.push_back(tstring_);
        columns.push_back(tu64_);
        columns.push_back(tu32_);
        columns.push_back(tuuid_);
        columns.push_back(tu8_);
        columns.push_back(tu16_);
        columns.push_back(tdouble_);
    }
}

TEST_F(DbPerfTest, DISABLED_StructVector) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarStruct> columns;
        columns.push_back(DbTestVarStruct(tstring_));
        columns.push_back(DbTestVarStruct(tu64_));
        columns.push_back(DbTestVarStruct(tu32_));
        columns.push_back(DbTestVarStruct(tuuid_));
        columns.push_back(DbTestVarStruct(tu8_));
        columns.push_back(DbTestVarStruct(tu16_));
        columns.push_back(DbTestVarStruct(tdouble_));
    }
}

TEST_F(DbPerfTest, DISABLED_VariantEncode) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarVariant> columns;
        columns.push_back(tstring_);
        columns.push_back(tu64_);
        columns.push_back(tu32_);
        columns.push_back(tuuid_);
        columns.push_back(tu8_);
        columns.push_back(tu16_);
        columns.push_back(tdouble_);
        // Encode
        std::string res;
        for (std::vector<DbTestVarVariant>::const_iterator it = columns.begin();
             it != columns.end(); it++) {
            const DbTestVarVariant &value(*it);
            switch (value.which()) {
            case DB_TEST_VAR_TYPE_STRING:
                res += EncodeString(boost::get<std::string>(value));
                break;
            case DB_TEST_VAR_TYPE_UINT64:
                res += EncodeUint64(boost::get<uint64_t>(value));
                break;
            case DB_TEST_VAR_TYPE_UINT32:
                res += EncodeUint32(boost::get<uint32_t>(value));
                break;
            case DB_TEST_VAR_TYPE_UUID:
                res += EncodeUuid(boost::get<boost::uuids::uuid>(value));
                break;
            case DB_TEST_VAR_TYPE_UINT8:
                res += EncodeUint8(boost::get<uint8_t>(value));
                break;
            case DB_TEST_VAR_TYPE_UINT16:
                res += EncodeUint16(boost::get<uint16_t>(value));
                break;
            case DB_TEST_VAR_TYPE_DOUBLE:
                res += EncodeDouble(boost::get<double>(value));
                break;
            default:
                assert(0);
                break;
            }
        }
    }
}

TEST_F(DbPerfTest, DISABLED_VariantVisitorEncode) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarVariant> columns;
        columns.push_back(tstring_);
        columns.push_back(tu64_);
        columns.push_back(tu32_);
        columns.push_back(tuuid_);
        columns.push_back(tu8_);
        columns.push_back(tu16_);
        columns.push_back(tdouble_);
        // Encode
        std::string res;
        DbDataValueEncodeVisitor evisitor(this, res);
        std::for_each(columns.begin(), columns.end(),
            boost::apply_visitor(evisitor));
    }
}

TEST_F(DbPerfTest, DISABLED_StructSafeEncode) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarStruct> columns;
        columns.push_back(DbTestVarStruct(tstring_));
        columns.push_back(DbTestVarStruct(tu64_));
        columns.push_back(DbTestVarStruct(tu32_));
        columns.push_back(DbTestVarStruct(tuuid_));
        columns.push_back(DbTestVarStruct(tu8_));
        columns.push_back(DbTestVarStruct(tu16_));
        columns.push_back(DbTestVarStruct(tdouble_));
        // Encode
        std::string res;
        for (std::vector<DbTestVarStruct>::const_iterator it = columns.begin();
             it != columns.end(); it++) {
            const DbTestVarStruct &value(*it);
            boost::system::error_code ec;
            switch (value.which()) {
            case DB_TEST_VAR_TYPE_STRING:
                res += EncodeString(value.Get<std::string>(ec));
                break;
            case DB_TEST_VAR_TYPE_UINT64:
                res += EncodeUint64(value.Get<uint64_t>(ec));
                break;
            case DB_TEST_VAR_TYPE_UINT32:
                res += EncodeUint32(value.Get<uint32_t>(ec));
                break;
            case DB_TEST_VAR_TYPE_UUID:
                res += EncodeUuid(value.Get<boost::uuids::uuid>(ec));
                break;
            case DB_TEST_VAR_TYPE_UINT8:
                res += EncodeUint8(value.Get<uint8_t>(ec));
                break;
            case DB_TEST_VAR_TYPE_UINT16:
                res += EncodeUint16(value.Get<uint16_t>(ec));
                break;
            case DB_TEST_VAR_TYPE_DOUBLE:
                res += EncodeDouble(value.Get<double>(ec));
                break;
            default:
                assert(0);
                break;
            }
        }
    }
}

TEST_F(DbPerfTest, DISABLED_StructEncode) {
    for (int i = 0; i < 100000; i++) {
        std::vector<DbTestVarStruct> columns;
        columns.push_back(DbTestVarStruct(tstring_));
        columns.push_back(DbTestVarStruct(tu64_));
        columns.push_back(DbTestVarStruct(tu32_));
        columns.push_back(DbTestVarStruct(tuuid_));
        columns.push_back(DbTestVarStruct(tu8_));
        columns.push_back(DbTestVarStruct(tu16_));
        columns.push_back(DbTestVarStruct(tdouble_));
        // Encode
        std::string res;
        for (std::vector<DbTestVarStruct>::const_iterator it = columns.begin();
             it != columns.end(); it++) {
            const DbTestVarStruct &value(*it);
            switch (value.which()) {
            case DB_TEST_VAR_TYPE_STRING:
                res += EncodeString(value.Get<std::string>());
                break;
            case DB_TEST_VAR_TYPE_UINT64:
                res += EncodeUint64(value.Get<uint64_t>());
                break;
            case DB_TEST_VAR_TYPE_UINT32:
                res += EncodeUint32(value.Get<uint32_t>());
                break;
            case DB_TEST_VAR_TYPE_UUID:
                res += EncodeUuid(value.Get<boost::uuids::uuid>());
                break;
            case DB_TEST_VAR_TYPE_UINT8:
                res += EncodeUint8(value.Get<uint8_t>());
                break;
            case DB_TEST_VAR_TYPE_UINT16:
                res += EncodeUint16(value.Get<uint16_t>());
                break;
            case DB_TEST_VAR_TYPE_DOUBLE:
                res += EncodeDouble(value.Get<double>());
                break;
            default:
                assert(0);
            }
        }
    }
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
