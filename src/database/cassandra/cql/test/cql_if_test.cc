//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>

#include <boost/format.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include <base/logging.h>
#include <database/gendb_if.h>
#include <database/cassandra/cql/cql_if_impl.h>

class CqlIfTest : public ::testing::Test {
 protected:
    CqlIfTest() {
    }
    ~CqlIfTest() {
    }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

TEST_F(CqlIfTest, DISABLED_EncodeCqlStringPerfSnprintf) {
    for (int i = 0; i < 100000; i++) {
        char buf[512];
        int n(snprintf(buf, sizeof(buf),
            "CREATE KEYSPACE %s WITH "
            "replication = {'class': 'SimpleStrategy', "
                           "'replication_factor': '%d'};",
            "ContrailAnalytics", 2));
        if (n < 0 || n >= (int)sizeof(buf)) {
            EXPECT_TRUE(0);
        }
        std::string query(buf);
    }
}

TEST_F(CqlIfTest, DISABLED_EncodeCqlStringPerfBoostFormat) {
    for (int i = 0; i < 100000; i++) {
        boost::format("CREATE KEYSPACE %1% WITH "
            "replication = {'class': 'SimpleStrategy', "
                           "'replication_factor': '%2%'};") %
            "ContrailAnalytics" % 2;
    }
}

TEST_F(CqlIfTest, StaticCfCreateTable) {
    GenDb::NewCf static_cf(
        "StaticCf", // name
        boost::assign::list_of // partition key
            (GenDb::DbDataType::LexicalUUIDType),
        boost::assign::map_list_of // columns
            ("columnA", GenDb::DbDataType::AsciiType)
            ("columnB", GenDb::DbDataType::LexicalUUIDType)
            ("columnC", GenDb::DbDataType::TimeUUIDType)
            ("columnD", GenDb::DbDataType::Unsigned8Type)
            ("columnE", GenDb::DbDataType::Unsigned16Type)
            ("columnF", GenDb::DbDataType::Unsigned32Type)
            ("columnG", GenDb::DbDataType::Unsigned64Type)
            ("columnH", GenDb::DbDataType::DoubleType)
            ("columnI", GenDb::DbDataType::UTF8Type)
            ("columnJ", GenDb::DbDataType::InetType)
            ("columnK", GenDb::DbDataType::IntegerType));
    std::string actual_qstring(
        cass::cql::impl::StaticCf2CassCreateTableIfNotExists(static_cf));
    std::string expected_qstring(
        "CREATE TABLE IF NOT EXISTS StaticCf ("
         "key uuid PRIMARY KEY, "
         "\"columnA\" ascii, "
         "\"columnB\" uuid, "
         "\"columnC\" timeuuid, "
         "\"columnD\" int, "
         "\"columnE\" int, "
         "\"columnF\" int, "
         "\"columnG\" bigint, "
         "\"columnH\" double, "
         "\"columnI\" text, "
         "\"columnJ\" inet, "
         "\"columnK\" varint) "
         "WITH compaction = {'class': "
         "'org.apache.cassandra.db.compaction.LeveledCompactionStrategy'} "
         "AND gc_grace_seconds = 0");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, DynamicCfCreateTable) {
    // Single elements in paritition key, column name, and value
    GenDb::NewCf dynamic_cf(
        "DynamicCf", // name
        boost::assign::list_of // partition key
            (GenDb::DbDataType::Unsigned32Type),
        boost::assign::list_of // column name comparator
            (GenDb::DbDataType::Unsigned32Type),
        boost::assign::list_of // column value validation class
            (GenDb::DbDataType::LexicalUUIDType));
    std::string actual_qstring(
        cass::cql::impl::DynamicCf2CassCreateTableIfNotExists(dynamic_cf));
    std::string expected_qstring(
        "CREATE TABLE IF NOT EXISTS DynamicCf ("
        "key int, "
        "column1 int, "
        "value uuid, "
        "PRIMARY KEY (key, column1)) "
        "WITH compaction = {'class': "
        "'org.apache.cassandra.db.compaction.LeveledCompactionStrategy'} "
        "AND gc_grace_seconds = 0");
    EXPECT_EQ(expected_qstring, actual_qstring);
    // Multiple elements in partition key, column name, and single in value
    GenDb::DbDataTypeVec all_types = boost::assign::list_of
        (GenDb::DbDataType::AsciiType)
        (GenDb::DbDataType::LexicalUUIDType)
        (GenDb::DbDataType::TimeUUIDType)
        (GenDb::DbDataType::Unsigned8Type)
        (GenDb::DbDataType::Unsigned16Type)
        (GenDb::DbDataType::Unsigned32Type)
        (GenDb::DbDataType::Unsigned64Type)
        (GenDb::DbDataType::DoubleType)
        (GenDb::DbDataType::UTF8Type)
        (GenDb::DbDataType::InetType)
        (GenDb::DbDataType::IntegerType);
    GenDb::NewCf dynamic_cf1(
        "DynamicCf1", // name
        all_types, // partition key
        all_types, // column name comparator
        boost::assign::list_of // column value validation class
            (GenDb::DbDataType::UTF8Type));
    std::string actual_qstring1(
        cass::cql::impl::DynamicCf2CassCreateTableIfNotExists(dynamic_cf1));
    std::string expected_qstring1(
        "CREATE TABLE IF NOT EXISTS DynamicCf1 ("
        "key ascii, "
        "key2 uuid, "
        "key3 timeuuid, "
        "key4 int, "
        "key5 int, "
        "key6 int, "
        "key7 bigint, "
        "key8 double, "
        "key9 text, "
        "key10 inet, "
        "key11 varint, "
        "column1 ascii, "
        "column2 uuid, "
        "column3 timeuuid, "
        "column4 int, "
        "column5 int, "
        "column6 int, "
        "column7 bigint, "
        "column8 double, "
        "column9 text, "
        "column10 inet, "
        "column11 varint, "
        "value text, "
        "PRIMARY KEY ("
        "(key, key2, key3, key4, key5, key6, key7, key8, key9, key10, key11), "
        "column1, column2, column3, column4, column5, column6, column7, "
        "column8, column9, column10, column11)) "
        "WITH compaction = {'class': "
        "'org.apache.cassandra.db.compaction.LeveledCompactionStrategy'} "
        "AND gc_grace_seconds = 0");
    EXPECT_EQ(expected_qstring1, actual_qstring1);
}

TEST_F(CqlIfTest, StaticCfInsertIntoTablePrepare) {
    GenDb::NewCf static_cf(
        "InsertIntoStaticCf", // name
        boost::assign::list_of // partition key
            (GenDb::DbDataType::LexicalUUIDType),
        boost::assign::map_list_of // columns
            ("columnA", GenDb::DbDataType::AsciiType)
            ("columnB", GenDb::DbDataType::LexicalUUIDType)
            ("columnC", GenDb::DbDataType::TimeUUIDType)
            ("columnD", GenDb::DbDataType::Unsigned8Type)
            ("columnE", GenDb::DbDataType::Unsigned16Type)
            ("columnF", GenDb::DbDataType::Unsigned32Type)
            ("columnG", GenDb::DbDataType::Unsigned64Type)
            ("columnH", GenDb::DbDataType::DoubleType)
            ("columnI", GenDb::DbDataType::UTF8Type)
            ("columnJ", GenDb::DbDataType::InetType)
            ("columnK", GenDb::DbDataType::IntegerType));
    std::string actual_qstring(
        cass::cql::impl::StaticCf2CassPrepareInsertIntoTable(static_cf));
    std::string expected_qstring(
        "INSERT INTO InsertIntoStaticCf ("
         "key, "
         "\"columnA\", "
         "\"columnB\", "
         "\"columnC\", "
         "\"columnD\", "
         "\"columnE\", "
         "\"columnF\", "
         "\"columnG\", "
         "\"columnH\", "
         "\"columnI\", "
         "\"columnJ\", "
         "\"columnK\") VALUES ("
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?, "
         "?) USING TTL ?");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, DynamicCfInsertIntoTablePrepare) {
    // Multiple elements in partition key, column name, and single in value
    GenDb::DbDataTypeVec all_types = boost::assign::list_of
        (GenDb::DbDataType::AsciiType)
        (GenDb::DbDataType::LexicalUUIDType)
        (GenDb::DbDataType::TimeUUIDType)
        (GenDb::DbDataType::Unsigned8Type)
        (GenDb::DbDataType::Unsigned16Type)
        (GenDb::DbDataType::Unsigned32Type)
        (GenDb::DbDataType::Unsigned64Type)
        (GenDb::DbDataType::DoubleType)
        (GenDb::DbDataType::UTF8Type)
        (GenDb::DbDataType::InetType)
        (GenDb::DbDataType::IntegerType);
    GenDb::NewCf dynamic_cf(
        "InsertIntoDynamicCf", // name
        all_types, // partition key
        all_types, // column name comparator
        boost::assign::list_of // column value validation class
            (GenDb::DbDataType::UTF8Type));
    std::string actual_qstring(
        cass::cql::impl::DynamicCf2CassPrepareInsertIntoTable(dynamic_cf));
    std::string expected_qstring(
        "INSERT INTO InsertIntoDynamicCf ("
        "key, "
        "key2, "
        "key3, "
        "key4, "
        "key5, "
        "key6, "
        "key7, "
        "key8, "
        "key9, "
        "key10, "
        "key11, "
        "column1, "
        "column2, "
        "column3, "
        "column4, "
        "column5, "
        "column6, "
        "column7, "
        "column8, "
        "column9, "
        "column10, "
        "column11, "
        "value) VALUES ("
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?, "
        "?) USING TTL ?");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

static const std::string tstring_("Test");
static const uint64_t tu64_(123456789ULL);
static const uint32_t tu32_(123456789);
static const boost::uuids::uuid tuuid_ = boost::uuids::random_generator()();
static const std::string tuuid_s_(to_string(tuuid_));
static const uint8_t tu8_(128);
static const uint16_t tu16_(65535);
static const double tdouble_(1.123);

static const GenDb::DbDataValueVec all_values = boost::assign::list_of
    (GenDb::DbDataValue(tstring_))
    (GenDb::DbDataValue(tu64_))
    (GenDb::DbDataValue(tu32_))
    (GenDb::DbDataValue(tuuid_))
    (GenDb::DbDataValue(tu8_))
    (GenDb::DbDataValue(tu16_))
    (GenDb::DbDataValue(tdouble_));

TEST_F(CqlIfTest, InsertIntoDynamicTable) {
    // DynamicCf
    boost::scoped_ptr<GenDb::ColList> v_dyn_columns(new GenDb::ColList);
    v_dyn_columns->cfname_ = "InsertIntoDynamicCf";
    v_dyn_columns->rowkey_ = all_values;
    GenDb::NewColVec &columns(v_dyn_columns->columns_);
    GenDb::DbDataValueVec *v_all_values(new GenDb::DbDataValueVec);
    *v_all_values = all_values;
    GenDb::DbDataValueVec *v_text(new GenDb::DbDataValueVec);
    v_text->push_back(tstring_);
    GenDb::NewCol *column(new GenDb::NewCol(v_all_values, v_text, 864000));
    columns.push_back(column);
    std::string actual_qstring(
        cass::cql::impl::DynamicCf2CassInsertIntoTable(v_dyn_columns.get()));
    std::string expected_qstring(
        "INSERT INTO InsertIntoDynamicCf "
        "(key, key2, key3, key4, key5, key6, key7, "
        "column1, column2, column3, column4, column5, column6, column7, "
        "value) VALUES ("
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "'Test') USING TTL 864000");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, InsertIntoStaticTable) {
    // StaticCf
    boost::scoped_ptr<GenDb::ColList> v_static_columns(new GenDb::ColList);
    v_static_columns->cfname_ = "InsertIntoStaticCf";
    v_static_columns->rowkey_ = all_values;
    GenDb::NewColVec &columns(v_static_columns->columns_);
    GenDb::NewCol *tstring_column(new GenDb::NewCol(
        "StringColumn", tstring_, 864000));
    columns.push_back(tstring_column);
    GenDb::NewCol *tu64_column(new GenDb::NewCol(
        "U64Column", tu64_, 864000));
    columns.push_back(tu64_column);
    GenDb::NewCol *tu32_column(new GenDb::NewCol(
        "U32Column", tu32_, 864000));
    columns.push_back(tu32_column);
    GenDb::NewCol *tuuid_column(new GenDb::NewCol(
        "UUIDColumn", tuuid_, 864000));
    columns.push_back(tuuid_column);
    GenDb::NewCol *tu8_column(new GenDb::NewCol(
        "U8Column", tu8_, 864000));
    columns.push_back(tu8_column);
    GenDb::NewCol *tu16_column(new GenDb::NewCol(
        "U16Column", tu16_, 864000));
    columns.push_back(tu16_column);
    GenDb::NewCol *tdouble_column(new GenDb::NewCol(
        "DoubleColumn", tdouble_, 864000));
    columns.push_back(tdouble_column);
    std::string actual_qstring(
        cass::cql::impl::StaticCf2CassInsertIntoTable(v_static_columns.get()));
    std::string expected_qstring(
        "INSERT INTO InsertIntoStaticCf "
        "(key, key2, key3, key4, key5, key6, key7, "
        "\"StringColumn\", \"U64Column\", \"U32Column\", \"UUIDColumn\", "
        "\"U8Column\", \"U16Column\", \"DoubleColumn\") VALUES ("
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123) "
        "USING TTL 864000");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, SelectFromTablePartitionKey) {
    std::string table("PartitionKeySelectTable");
    std::string actual_qstring(
        cass::cql::impl::PartitionKey2CassSelectFromTable(table, all_values));
    std::string expected_qstring(
        "SELECT * FROM PartitionKeySelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, SelectFromTableSlice) {
    std::string table("SliceSelectTable");
    // Empty slice
    GenDb::ColumnNameRange empty_crange;
    std::string actual_qstring(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, empty_crange));
    std::string expected_qstring(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123");
    EXPECT_EQ(expected_qstring, actual_qstring);
    // Normal slice
    GenDb::ColumnNameRange crange;
    crange.start_ = all_values;
    crange.finish_ = all_values;
    crange.count_ = 5000;
    std::string actual_string1(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, crange));
    std::string expected_qstring1(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "(column1, column2, column3, column4, column5, column6, column7) >= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123)"
        " AND "
        "(column1, column2, column3, column4, column5, column6, column7) <= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123)"
        " LIMIT 5000");
    EXPECT_EQ(expected_qstring1, actual_string1);
    // Start slice only
    GenDb::ColumnNameRange start_crange;
    start_crange.start_ = all_values;
    start_crange.count_ = 5000;
    std::string actual_string2(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, start_crange));
    std::string expected_qstring2(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "(column1, column2, column3, column4, column5, column6, column7) >= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123)"
        " LIMIT 5000");
    EXPECT_EQ(expected_qstring2, actual_string2);
    // Finish slice only
    GenDb::ColumnNameRange finish_crange;
    finish_crange.finish_ = all_values;
    finish_crange.count_ = 5000;
    std::string actual_string3(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, finish_crange));
    std::string expected_qstring3(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "(column1, column2, column3, column4, column5, column6, column7) <= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123)"
        " LIMIT 5000");
    EXPECT_EQ(expected_qstring3, actual_string3);
    // Count only
    GenDb::ColumnNameRange count_crange;
    count_crange.count_ = 5000;
    std::string actual_string4(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, count_crange));
    std::string expected_qstring4(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 "
        "LIMIT 5000");
    EXPECT_EQ(expected_qstring4, actual_string4);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
