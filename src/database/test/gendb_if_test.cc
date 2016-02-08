//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <algorithm>

#include <boost/system/error_code.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/variant.hpp>

#include <base/util.h>
#include <base/logging.h>
#include <base/string_util.h>

#include "testing/gunit.h"
#include <database/gendb_if.h>
#include <database/gendb_statistics.h>

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
};

static const std::string tstring_("Test");
static const uint64_t tu64_(123456789ULL);
static const uint32_t tu32_(123456789);
static const boost::uuids::uuid tuuid_ = boost::uuids::random_generator()();
static const uint8_t tu8_(128);
static const uint16_t tu16_(65535);
static const double tdouble_(1.0);

TEST_F(DbPerfTest, VariantVector) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

TEST_F(DbPerfTest, StructVector) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

TEST_F(DbPerfTest, VariantEncode) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

TEST_F(DbPerfTest, VariantVisitorEncode) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

TEST_F(DbPerfTest, StructSafeEncode) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

TEST_F(DbPerfTest, StructEncode) {
    //int loop_count = 100000;
    int loop_count = 1;
    for (int i = 0; i < loop_count; i++) {
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

class GenDbTest : public ::testing::Test {
 protected:
    void GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) {
        stats_.GetDiffs(vdbti, dbe);
    }
    void UpdateErrorsWriteTablespace() {
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_TABLESPACE);
    }
    void UpdateStatsCfWrite(const std::string &cfname) {
        stats_.IncrementTableWrite(cfname);
    }
    void UpdateStatsAll(const std::string &cfname) {
        // Write success
        stats_.IncrementTableWrite(cfname);
        // Write fail
        stats_.IncrementTableWriteFail(cfname);
        // Read success
        stats_.IncrementTableRead(cfname);
        // Read fail
        stats_.IncrementTableReadFail(cfname);
        // Increment errors of each type
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_TABLESPACE);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_READ_TABLESPACE);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_COLUMN_FAMILY);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_COLUMN);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_BATCH_COLUMN);
        stats_.IncrementErrors(
            GenDb::IfErrors::ERR_READ_COLUMN);
    }

    GenDb::GenDbIfStats stats_;
};

TEST_F(GenDbTest, NewColSizeSql) {
    // SQL - string
    GenDb::NewCol tstring_col(tstring_, tstring_, 0);
    EXPECT_EQ(tstring_.length() + tstring_.length(), tstring_col.GetSize());
    // SQL - uint64_t
    GenDb::NewCol tu64_col(tstring_, tu64_, 0);
    EXPECT_EQ(tstring_.length() + sizeof(tu64_), tu64_col.GetSize());
    // SQL - uint32_t
    GenDb::NewCol tu32_col(tstring_, tu32_, 0);
    EXPECT_EQ(tstring_.length() + sizeof(tu32_), tu32_col.GetSize());
    // SQL - boost::uuids::uuid
    GenDb::NewCol tuuid_col(tstring_, tuuid_, 0);
    EXPECT_EQ(tstring_.length() + tuuid_.size(), tuuid_col.GetSize());
    // SQL - uint8_t
    GenDb::NewCol tu8_col(tstring_, tu8_, 0);
    EXPECT_EQ(tstring_.length() + sizeof(tu8_), tu8_col.GetSize());
    // SQL - uint16_t
    GenDb::NewCol tu16_col(tstring_, tu16_, 0);
    EXPECT_EQ(tstring_.length() + sizeof(tu16_), tu16_col.GetSize());
    // SQL - double
    GenDb::NewCol tdouble_col(tstring_, tdouble_, 0);
    EXPECT_EQ(tstring_.length() + sizeof(tdouble_), tdouble_col.GetSize());
}

static void PopulateDbDataValueVec(GenDb::DbDataValueVec *dbv, size_t *vsize) {
    dbv->push_back(tstring_);
    size_t size = tstring_.length();
    dbv->push_back(tu64_);
    size += sizeof(tu64_);
    dbv->push_back(tu32_);
    size += sizeof(tu32_);
    dbv->push_back(tuuid_);
    size += tuuid_.size();
    dbv->push_back(tu8_);
    size += sizeof(tu8_);
    dbv->push_back(tu16_);
    size += sizeof(tu16_);
    dbv->push_back(tdouble_);
    size += sizeof(tdouble_);
    *vsize += size;
}

static GenDb::NewCol* CreateNewColNoSql(size_t *csize) {
    GenDb::DbDataValueVec *name(new GenDb::DbDataValueVec);
    PopulateDbDataValueVec(name, csize);
    GenDb::DbDataValueVec *value(new GenDb::DbDataValueVec);
    PopulateDbDataValueVec(value, csize);
    GenDb::NewCol *nosql_col(new GenDb::NewCol(name, value, 0));
    return nosql_col;
}

TEST_F(GenDbTest, NewColSizeNoSql) {
    size_t expected_size(0);
    boost::scoped_ptr<GenDb::NewCol> col(CreateNewColNoSql(&expected_size));
    EXPECT_EQ(expected_size, col->GetSize());
}

TEST_F(GenDbTest, ColListSize) {
    GenDb::ColList colList;
    colList.cfname_ = "TestColList";
    size_t expected_size(0);
    PopulateDbDataValueVec(&colList.rowkey_, &expected_size);
    // SQL - string
    GenDb::NewCol *tstring_col(new GenDb::NewCol(tstring_, tstring_, 0));
    expected_size += 2 * tstring_.length();
    colList.columns_.push_back(tstring_col);
    // No SQL
    GenDb::NewCol *nosql_col(CreateNewColNoSql(&expected_size));
    colList.columns_.push_back(nosql_col);
    EXPECT_EQ(expected_size, colList.GetSize());
}

TEST_F(GenDbTest, Stats) {
    // Update Cf stats
    const std::string cfname("FakeColumnFamily");
    UpdateStatsAll(cfname);
    // Get and verify
    std::vector<GenDb::DbTableInfo> vdbti;
    GenDb::DbErrors adbe;
    GetStats(&vdbti, &adbe);
    ASSERT_EQ(1, vdbti.size());
    GenDb::DbTableInfo edbti;
    edbti.set_table_name(cfname);
    edbti.set_reads(1);
    edbti.set_read_fails(1);
    edbti.set_writes(1);
    edbti.set_write_fails(1);
    EXPECT_EQ(edbti, vdbti[0]);
    vdbti.clear();
    GenDb::DbErrors edbe;
    edbe.set_write_tablespace_fails(1);
    edbe.set_read_tablespace_fails(1);
    edbe.set_write_table_fails(1);
    edbe.set_read_table_fails(1);
    edbe.set_write_column_fails(1);
    edbe.set_write_batch_column_fails(1);
    edbe.set_read_column_fails(1);
    EXPECT_EQ(edbe, adbe);
    // Diffs
    // Write success
    UpdateStatsCfWrite(cfname);
    UpdateErrorsWriteTablespace();
    // Get and verify
    GenDb::DbErrors adbe_diffs;
    GetStats(&vdbti, &adbe_diffs);
    ASSERT_EQ(1, vdbti.size());
    GenDb::DbTableInfo edbti_diffs;
    edbti_diffs.set_table_name(cfname);
    edbti_diffs.set_writes(1);
    EXPECT_EQ(edbti_diffs, vdbti[0]);
    vdbti.clear();
    GenDb::DbErrors edbe_diffs;
    edbe_diffs.set_write_tablespace_fails(1);
    EXPECT_EQ(edbe_diffs, adbe_diffs);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
