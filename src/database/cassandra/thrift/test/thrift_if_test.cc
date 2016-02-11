/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include "testing/gunit.h"

#include "base/logging.h"
#include "../thrift_if_impl.h"

using namespace GenDb;

class ThriftIfTest : public ::testing::Test {
protected:
    ThriftIfTest() {
    }
    ~ThriftIfTest() {
    }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

TEST_F(ThriftIfTest, EncodeDecodeString) {
    std::vector<std::string> strings = boost::assign::list_of
        ("Test String1")
        ("")
        ("Test:Str :ing :2  ");
    for (size_t i = 0; i < strings.size(); i++) {
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
    // Composite - negative test
    uint32_t val = 100;
    std::string composite_enc(DbEncodeStringComposite(val));
    int offset;
    DbDataValue composite_dec(
            DbDecodeStringComposite(composite_enc.c_str(), offset));
    EXPECT_EQ(offset, composite_enc.size());
    std::string t_composite_dec;
    t_composite_dec = boost::get<std::string>(composite_dec);
    EXPECT_EQ(t_composite_dec, "");

    // Non Composite - negative test
    std::string ncomposite_enc(DbEncodeStringNonComposite(val));
    DbDataValue ncomposite_dec(
            DbDecodeStringNonComposite(ncomposite_enc.c_str()));
    std::string t_ncomposite_dec;
    t_ncomposite_dec = boost::get<std::string>(ncomposite_dec);
    EXPECT_EQ(t_ncomposite_dec, "");
}

TEST_F(ThriftIfTest, TestEncodeDecodeDouble) {
    std::vector<double> integers = boost::assign::list_of
        (std::numeric_limits<double>::min())
        (std::numeric_limits<double>::max())
        (0)
        (std::numeric_limits<double>::min()/2)
        (std::numeric_limits<double>::max()/2);
    for (size_t i = 0; i < integers.size(); i++) {
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
    std::string val;
    std::string composite_enc(
        DbEncodeDoubleComposite(val));
    int offset;
    DbDataValue composite_dec(
        DbDecodeDoubleComposite(
            composite_enc.c_str(), offset));
    EXPECT_EQ(offset, composite_enc.size());
    double t_composite_dec;
    t_composite_dec = boost::get<double>(composite_dec);
    EXPECT_EQ(t_composite_dec, 0);
    // Non Composite
    std::string ncomposite_enc(
        DbEncodeDoubleNonComposite(val));
    DbDataValue ncomposite_dec(
        DbDecodeDoubleNonComposite(ncomposite_enc));
    double t_ncomposite_dec;
    t_ncomposite_dec = boost::get<double>(ncomposite_dec);
    EXPECT_EQ(t_ncomposite_dec, 0);
}

TEST_F(ThriftIfTest, TestEncodeDecodeUUID) {
    boost::uuids::random_generator gen;
    std::vector<boost::uuids::uuid> integers = boost::assign::list_of
        (boost::uuids::nil_uuid())
        (gen())
        (gen());
    for (size_t i = 0; i < integers.size(); i++) {
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
    // Composite
    std::string val;
    std::string composite_enc(
        DbEncodeUUIDComposite(val));
    int offset;
    DbDataValue composite_dec(
        DbDecodeUUIDComposite(
            composite_enc.c_str(), offset));
    EXPECT_EQ(offset, composite_enc.size());
    boost::uuids::uuid t_composite_dec;
    t_composite_dec = boost::get<boost::uuids::uuid>(composite_dec);
    EXPECT_EQ(t_composite_dec, boost::uuids::nil_uuid());
    // Non Composite
    std::string ncomposite_enc(
        DbEncodeUUIDNonComposite(val));
    DbDataValue ncomposite_dec(
        DbDecodeUUIDNonComposite(ncomposite_enc));
    boost::uuids::uuid t_ncomposite_dec;
    t_ncomposite_dec = boost::get<boost::uuids::uuid>(ncomposite_dec);
    EXPECT_EQ(t_ncomposite_dec, boost::uuids::nil_uuid());
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
    // Composite
    std::string val;
    std::string composite_enc(
        DbEncodeIntegerComposite<NumberType>(val));
    int offset;
    DbDataValue composite_dec(
        DbDecodeIntegerComposite<NumberType>(
            composite_enc.c_str(), offset));
    EXPECT_EQ(offset, composite_enc.size());
    NumberType t_composite_dec;
    t_composite_dec = boost::get<NumberType>(composite_dec);
    EXPECT_EQ(t_composite_dec, std::numeric_limits<NumberType>::max());
    // Non Composite
    std::string ncomposite_enc(
        DbEncodeIntegerNonComposite<NumberType>(val));
    DbDataValue ncomposite_dec(
        DbDecodeIntegerNonComposite<NumberType>(ncomposite_enc));
    NumberType t_ncomposite_dec;
    t_ncomposite_dec = boost::get<NumberType>(ncomposite_dec);
    EXPECT_EQ(t_ncomposite_dec, std::numeric_limits<NumberType>::max());
}

TEST_F(ThriftIfTest, EncodeDecodeU8) {
    TestEncodeDecodeInteger<uint8_t>();
}

TEST_F(ThriftIfTest, EncodeDecodeU16) {
    TestEncodeDecodeInteger<uint16_t>();
}

TEST_F(ThriftIfTest, EncodeDecodeU32) {
    TestEncodeDecodeInteger<uint32_t>();
}

TEST_F(ThriftIfTest, EncodeDecodeU64) {
    TestEncodeDecodeInteger<uint64_t>();
}

TEST_F(ThriftIfTest, EncodeDecodeCompositeVector) {
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

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

