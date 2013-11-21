/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "../cdb_if.h"
#include "base/logging.h"

class CdbIfTest : public ::testing::Test {
public:

    CdbIfTest() :
        cdbif_(new CdbIf()) {
    }
    ~CdbIfTest() {
        delete cdbif_;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    CdbIf *cdbif() {
        return cdbif_;
    }
    std::string Db_encode_string_non_composite(std::string teststr) {
        return cdbif_->Db_encode_string_non_composite(teststr);
    }
    DbDataValue Db_decode_string_non_composite(std::string teststr_enc) {
        return cdbif_->Db_decode_string_non_composite(teststr_enc);
    }
    std::string Db_encode_Double_non_composite(double testdouble) {
        return cdbif_->Db_encode_Double_non_composite(testdouble);
    }
    DbDataValue Db_decode_Double_non_composite(std::string testdouble_enc) {
        return cdbif_->Db_decode_Double_non_composite(testdouble_enc);
    }

private:
    CdbIf *cdbif_;
};

TEST_F(CdbIfTest, Test1) {
    std::string teststrs[] = {"Test String1",
            "Test:Str :ing :2"};

    for (int i = 0; i < 2; i++) {
        std::string teststr = teststrs[i];
        std::string teststr_enc = Db_encode_string_non_composite(teststr);
        DbDataValue teststr_dec = Db_decode_string_non_composite(teststr_enc);
        std::string teststr_dec2;
        try {
            teststr_dec2 = boost::get<std::string>(teststr_dec);
        } catch (boost::bad_get& ex) {
            CDBIF_HANDLE_EXCEPTION(__func__ << "Exception for boost::get, what=" << ex.what());
        }
        EXPECT_EQ(teststr_dec2, teststr);
    }

    double testdoubles[] = {188.25, -9423, 82937823.92342384, -7478438.23990};

    for (int i = 0; i < sizeof(testdoubles)/sizeof(double); i++) {
        double testdouble = testdoubles[i];
        std::string testdouble_enc = Db_encode_Double_non_composite(testdouble);
        DbDataValue testdouble_dec = Db_decode_Double_non_composite(testdouble_enc);
        double testdouble_dec2;
        try {
            testdouble_dec2 = boost::get<double>(testdouble_dec);
        } catch (boost::bad_get& ex) {
            CDBIF_HANDLE_EXCEPTION(__func__ << "Exception for boost::get, what=" << ex.what());
        }
        EXPECT_EQ(testdouble_dec2, testdouble);
    }
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

