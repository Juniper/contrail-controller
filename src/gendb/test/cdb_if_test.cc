/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
    CdbIf::CdbIfStats stats_;
};

TEST_F(CdbIfTest, EncodeDecodeStringDouble) {
    std::string teststrs[] = {"Test String1",
            "Test:Str :ing :2"};

    for (int i = 0; i < 2; i++) {
        std::string teststr = teststrs[i];
        std::string teststr_enc = DbEncodeStringNonComposite(teststr);
        DbDataValue teststr_dec = DbDecodeStringNonComposite(teststr_enc);
        std::string teststr_dec2;
        try {
            teststr_dec2 = boost::get<std::string>(teststr_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                teststr_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(teststr_dec2, teststr);
    }

    double testdoubles[] = {188.25, -9423, 82937823.92342384, -7478438.23990};

    for (int i = 0; i < sizeof(testdoubles)/sizeof(double); i++) {
        double testdouble = testdoubles[i];
        std::string testdouble_enc = DbEncodeDoubleNonComposite(testdouble);
        DbDataValue testdouble_dec = DbDecodeDoubleNonComposite(testdouble_enc);
        double testdouble_dec2;
        try {
            testdouble_dec2 = boost::get<double>(testdouble_dec);
        } catch (boost::bad_get& ex) {
            LOG(ERROR, __func__ << ":" << __LINE__ << ": Invalid value: " << 
                testdouble_dec.which() << ": " << ex.what());
        }
        EXPECT_EQ(testdouble_dec2, testdouble);
    }
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

