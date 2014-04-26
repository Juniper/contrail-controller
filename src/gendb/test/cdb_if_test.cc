/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/uuid/random_generator.hpp>
#include "testing/gunit.h"

#include "base/logging.h"
#include "../cdb_if.h"

using namespace GenDb;

class CdbIfTest : public ::testing::Test {
protected:
    CdbIfTest() {
    }
    ~CdbIfTest() {
    }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
    void GetStats(std::vector<GenDb::DbTableInfo> &vdbti,
        GenDb::DbErrors &dbe) {
        stats_.Get(vdbti, dbe);
    }
    void UpdateErrorsWriteTablespace() {
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_WRITE_TABLESPACE);
    }
    void UpdateStatsCfWrite(const std::string &cfname) {
        stats_.UpdateCf(cfname, true, false);
    }
    void UpdateStatsAll(const std::string &cfname) { 
        // Write success
        stats_.UpdateCf(cfname, true, false);
        // Write fail
        stats_.UpdateCf(cfname, true, true);
        // Read success
        stats_.UpdateCf(cfname, false, false);
        // Read fail
        stats_.UpdateCf(cfname, false, true);
        // Increment errors of each type
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_WRITE_TABLESPACE);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_READ_TABLESPACE);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN_FAMILY);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_WRITE_BATCH_COLUMN);
        stats_.IncrementErrors(
            CdbIf::CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN);
    }
    bool DbDataValueVecFromString(GenDb::DbDataValueVec& output,
        const GenDb::DbDataTypeVec& typevec, const std::string& input) {
        return dbif_.DbDataValueVecFromString(output, typevec, input);
    }
    bool DbDataValueVecToString(std::string& output, bool composite,
        const GenDb::DbDataValueVec& input) {
        return dbif_.DbDataValueVecToString(output, composite, input);
    }
 
    CdbIf dbif_;
    CdbIf::CdbIfStats stats_;
};

TEST_F(CdbIfTest, EncodeDecodeString) {
    std::vector<std::string> strings = boost::assign::list_of
        ("Test String1")
        ("Test:Str :ing :2  ");
    for (int i = 0; i < strings.size(); i++) {
        std::string str(strings[i]);
        // Composite
        std::string composite_enc(DbEncodeStringComposite(str));
        int offset;
        DbDataValue composite_dec(
            DbDecodeStringComposite(composite_enc.c_str(), offset));
        EXPECT_EQ(offset, composite_enc.size());
        std::string t_composite_dec;
        try {
            t_composite_dec = boost::get<std::string>(
                composite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                composite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_composite_dec, str);
        // Non Composite
        std::string ncomposite_enc(
            DbEncodeStringNonComposite(str));
        DbDataValue ncomposite_dec(
            DbDecodeStringNonComposite(ncomposite_enc));
        std::string t_ncomposite_dec;
        try {
            t_ncomposite_dec = boost::get<std::string>(
                ncomposite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                ncomposite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_ncomposite_dec, str);
        // Match composite and non composite
        EXPECT_EQ(t_ncomposite_dec, t_composite_dec);
        EXPECT_EQ(ncomposite_dec, composite_dec);
    }
}

TEST_F(CdbIfTest, TestEncodeDecodeDouble) {
    std::vector<double> integers = boost::assign::list_of
        (std::numeric_limits<double>::min())
        (std::numeric_limits<double>::max())
        (0)
        (std::numeric_limits<double>::min()/2)
        (std::numeric_limits<double>::max()/2);
    for (int i = 0; i < integers.size(); i++) {
        double integer(integers[i]);
        // Composite
        std::string composite_enc(
            DbEncodeDoubleComposite(integer));
        int offset;
        DbDataValue composite_dec( 
            DbDecodeDoubleComposite(
                composite_enc.c_str(), offset));
        EXPECT_EQ(offset, composite_enc.size());
        double t_composite_dec;
        try {
            t_composite_dec = boost::get<double>(
                composite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                composite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_composite_dec, integer);
        // Non Composite
        std::string ncomposite_enc(
            DbEncodeDoubleNonComposite(integer));
        DbDataValue ncomposite_dec(
            DbDecodeDoubleNonComposite(ncomposite_enc));
        double t_ncomposite_dec;
        try {
            t_ncomposite_dec = boost::get<double>(
                ncomposite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                ncomposite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_ncomposite_dec, integer);
        // Match composite and non composite
        EXPECT_EQ(t_ncomposite_dec, t_composite_dec);
        EXPECT_EQ(ncomposite_dec, composite_dec);
    }
}

TEST_F(CdbIfTest, TestEncodeDecodeUUID) {
    boost::uuids::random_generator gen;
    std::vector<boost::uuids::uuid> integers = boost::assign::list_of
        (boost::uuids::nil_uuid())
        (gen())
        (gen());
    for (int i = 0; i < integers.size(); i++) {
        boost::uuids::uuid integer(integers[i]);
        // Composite
        std::string composite_enc(
            DbEncodeUUIDComposite(integer));
        int offset;
        DbDataValue composite_dec( 
            DbDecodeUUIDComposite(
                composite_enc.c_str(), offset));
        EXPECT_EQ(offset, composite_enc.size());
        boost::uuids::uuid t_composite_dec;
        try {
            t_composite_dec = boost::get<boost::uuids::uuid>(
                composite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                composite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_composite_dec, integer);
        // Non Composite
        std::string ncomposite_enc(
            DbEncodeUUIDNonComposite(integer));
        DbDataValue ncomposite_dec(
            DbDecodeUUIDNonComposite(ncomposite_enc));
        boost::uuids::uuid t_ncomposite_dec;
        try {
            t_ncomposite_dec = boost::get<boost::uuids::uuid>(
                ncomposite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                ncomposite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_ncomposite_dec, integer);
        // Match composite and non composite
        EXPECT_EQ(t_ncomposite_dec, t_composite_dec);
        EXPECT_EQ(ncomposite_dec, composite_dec);
    }
}

template <typename NumberType>
void TestEncodeDecodeInteger() {
    std::vector<NumberType> integers = boost::assign::list_of
        (std::numeric_limits<NumberType>::min())
        (std::numeric_limits<NumberType>::max())
        (0)
        (std::numeric_limits<NumberType>::min()/2)
        (std::numeric_limits<NumberType>::max()/2);
    for (int i = 0; i < integers.size(); i++) {
        NumberType integer(integers[i]);
        // Composite
        std::string composite_enc(
            DbEncodeIntegerComposite<NumberType>(integer));
        int offset;
        DbDataValue composite_dec( 
            DbDecodeIntegerComposite<NumberType>(
                composite_enc.c_str(), offset));
        EXPECT_EQ(offset, composite_enc.size());
        NumberType t_composite_dec;
        try {
            t_composite_dec = boost::get<NumberType>(
                composite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                composite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_composite_dec, integer);
        // Non Composite
        std::string ncomposite_enc(
            DbEncodeIntegerNonComposite<NumberType>(integer));
        DbDataValue ncomposite_dec(
            DbDecodeIntegerNonComposite<NumberType>(ncomposite_enc));
        NumberType t_ncomposite_dec;
        try {
            t_ncomposite_dec = boost::get<NumberType>(
                ncomposite_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                ncomposite_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(t_ncomposite_dec, integer);
        // Match composite and non composite
        EXPECT_EQ(t_ncomposite_dec, t_composite_dec);
        EXPECT_EQ(ncomposite_dec, composite_dec);
    }
}

TEST_F(CdbIfTest, EncodeDecodeU8) {
    TestEncodeDecodeInteger<uint8_t>();
}

TEST_F(CdbIfTest, EncodeDecodeU16) {
    TestEncodeDecodeInteger<uint16_t>();
}

TEST_F(CdbIfTest, EncodeDecodeU32) {
    TestEncodeDecodeInteger<uint32_t>();
}

TEST_F(CdbIfTest, EncodeDecodeU64) {
    TestEncodeDecodeInteger<uint64_t>();
}

TEST_F(CdbIfTest, EncodeDecodeCompositeVector) {
    uint32_t T2(166699223);
    uint32_t T1(6979602);
    GenDb::DbDataValueVec col_name;
    col_name.reserve(2);
    col_name.push_back("a6s23:Analytics:Collector:0");
    col_name.push_back(T1);
    std::string en_str;
    DbDataValueVecToString(en_str, true, col_name);
    GenDb::DbDataValueVec dec_col_name;
    DbDataTypeVec typevec = boost::assign::list_of
        (GenDb::DbDataType::AsciiType)
        (GenDb::DbDataType::Unsigned32Type);
    DbDataValueVecFromString(dec_col_name, typevec, en_str);
    EXPECT_EQ(col_name[0], dec_col_name[0]);
    EXPECT_EQ(col_name[1], dec_col_name[1]);
    uint32_t t1 = boost::get<uint32_t>(dec_col_name.at(1));
    ASSERT_EQ(T1, t1);
}

TEST_F(CdbIfTest, Stats) {
    // Update Cf stats
    const std::string cfname("FakeColumnFamily");
    UpdateStatsAll(cfname);
    // Get and verify
    std::vector<GenDb::DbTableInfo> vdbti;
    GenDb::DbErrors adbe;
    GetStats(vdbti, adbe);
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
    GetStats(vdbti, adbe_diffs);
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

