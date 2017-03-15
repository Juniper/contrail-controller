//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include <base/logging.h>
#include <database/gendb_constants.h>
#include <database/gendb_if.h>
#include <database/cassandra/cql/cql_if_impl.h>
#include <database/cassandra/cql/test/mock_cql_lib_if.h>

using boost::assign::list_of;

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
            ("columnK", GenDb::DbDataType::IntegerType)
            ("columnL", GenDb::DbDataType::BlobType));
    std::string actual_qstring(
        cass::cql::impl::StaticCf2CassCreateTableIfNotExists(static_cf,
            GenDb::g_gendb_constants.SIZE_TIERED_COMPACTION_STRATEGY));
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
         "\"columnK\" varint, "
         "\"columnL\" blob) "
         "WITH compaction = {'class': "
         "'org.apache.cassandra.db.compaction.SizeTieredCompactionStrategy'} "
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
        cass::cql::impl::DynamicCf2CassCreateTableIfNotExists(dynamic_cf,
            GenDb::g_gendb_constants.LEVELED_COMPACTION_STRATEGY));
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
        (GenDb::DbDataType::IntegerType)
        (GenDb::DbDataType::BlobType);
    GenDb::NewCf dynamic_cf1(
        "DynamicCf1", // name
        all_types, // partition key
        all_types, // column name comparator
        boost::assign::list_of // column value validation class
            (GenDb::DbDataType::UTF8Type));
    std::string actual_qstring1(
        cass::cql::impl::DynamicCf2CassCreateTableIfNotExists(dynamic_cf1,
            GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY));
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
        "key12 blob, "
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
        "column12 blob, "
        "value text, "
        "PRIMARY KEY ("
        "(key, key2, key3, key4, key5, key6, key7, key8, key9, key10, key11, "
        "key12), "
        "column1, column2, column3, column4, column5, column6, column7, "
        "column8, column9, column10, column11, column12)) "
        "WITH compaction = {'class': "
        "'org.apache.cassandra.db.compaction.DateTieredCompactionStrategy'} "
        "AND read_repair_chance = 0.0 "
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
            ("columnK", GenDb::DbDataType::IntegerType)
            ("columnL", GenDb::DbDataType::BlobType));
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
         "\"columnK\", "
         "\"columnL\") VALUES ("
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
        (GenDb::DbDataType::IntegerType)
        (GenDb::DbDataType::BlobType);
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
        "key12, "
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
        "column12, "
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
static const GenDb::Blob tblob_(
    reinterpret_cast<const uint8_t *>("012345678901234567890123456789"),
    strlen("012345678901234567890123456789"));

static const GenDb::DbDataValueVec all_values = boost::assign::list_of
    (GenDb::DbDataValue(tstring_))
    (GenDb::DbDataValue(tu64_))
    (GenDb::DbDataValue(tu32_))
    (GenDb::DbDataValue(tuuid_))
    (GenDb::DbDataValue(tu8_))
    (GenDb::DbDataValue(tu16_))
    (GenDb::DbDataValue(tdouble_))
    (GenDb::DbDataValue(tblob_));

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
        "(key, key2, key3, key4, key5, key6, key7, key8, "
        "column1, column2, column3, column4, column5, column6, column7, "
        "column8, "
        "value) VALUES ("
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839, "
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839, "
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
    GenDb::NewCol *tblob_column(new GenDb::NewCol(
        "BlobColumn", tblob_, 864000));
    columns.push_back(tblob_column);
    std::string actual_qstring(
        cass::cql::impl::StaticCf2CassInsertIntoTable(v_static_columns.get()));
    std::string expected_qstring(
        "INSERT INTO InsertIntoStaticCf "
        "(key, key2, key3, key4, key5, key6, key7, key8, "
        "\"StringColumn\", \"U64Column\", \"U32Column\", \"UUIDColumn\", "
        "\"U8Column\", \"U16Column\", \"DoubleColumn\", \"BlobColumn\") "
        "VALUES ("
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839, "
        "'Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839) "
        "USING TTL 864000");
    EXPECT_EQ(expected_qstring, actual_qstring);
}

TEST_F(CqlIfTest, SelectFromTable) {
    std::string table("SelectTable");
    std::string actual_qstring(
        cass::cql::impl::CassSelectFromTable(table));
    std::string expected_qstring(
        "SELECT * FROM SelectTable");
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
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
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
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
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
        "key8=0x303132333435363738393031323334353637383930313233343536373839 AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) >= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
        " AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) <= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
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
        "key8=0x303132333435363738393031323334353637383930313233343536373839 AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) >= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
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
        "key8=0x303132333435363738393031323334353637383930313233343536373839 AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) <= "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
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
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839 "
        "LIMIT 5000");
    EXPECT_EQ(expected_qstring4, actual_string4);
    // Normal slice with start and finish operatorse
    GenDb::ColumnNameRange op_crange;
    op_crange.start_ = all_values;
    op_crange.finish_ = all_values;
    op_crange.count_ = 5000;
    op_crange.start_op_ = GenDb::Op::GT;
    op_crange.finish_op_ = GenDb::Op::LT;
    std::string actual_string5(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, op_crange));
    std::string expected_qstring5(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839 AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) > "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
        " AND "
        "(column1, column2, column3, column4, column5, column6, column7, "
        "column8) < "
        "('Test', 123456789, 123456789, " + tuuid_s_ + ", 128, 65535, 1.123, "
        "0x303132333435363738393031323334353637383930313233343536373839)"
        " LIMIT 5000");
    EXPECT_EQ(expected_qstring5, actual_string5);
}

TEST_F(CqlIfTest, SelectFromTableReadFields) {
    std::string table("SliceSelectTable");
    // Empty field list. Read all fields
    GenDb::ColumnNameRange empty_crange;
    GenDb::FieldNamesToReadVec empty_fields;
    std::string actual_qstring(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, empty_crange, empty_fields));
    std::string expected_qstring(
        "SELECT * FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
    EXPECT_EQ(expected_qstring, actual_qstring);

    // Read only row key
    GenDb::FieldNamesToReadVec only_key;
    only_key.push_back(boost::make_tuple("key", true, false, false));
    std::string actual_qstring1(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, empty_crange, only_key));
    std::string expected_qstring1(
        "SELECT key FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
    EXPECT_EQ(expected_qstring1, actual_qstring1);

    // Read row key, column1 and value without timestamp
    GenDb::FieldNamesToReadVec key_column_values;
    key_column_values.push_back(boost::make_tuple("key", true, false, false));
    key_column_values.push_back(boost::make_tuple("column1", false, true, false));
    key_column_values.push_back(boost::make_tuple("value", false, false, false));
    std::string actual_qstring2(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, empty_crange, key_column_values));
    std::string expected_qstring2(
        "SELECT key,column1,value FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
    EXPECT_EQ(expected_qstring2, actual_qstring2);

    // Read row key, column1 and value with timestamp
    GenDb::FieldNamesToReadVec key_column_values_timestamp;
    key_column_values_timestamp.push_back(boost::make_tuple("key", true, false,
                                                            false));
    key_column_values_timestamp.push_back(boost::make_tuple("column1", false,
                                                            true, false));
    key_column_values_timestamp.push_back(boost::make_tuple("value", false,
                                                            false, true));
    std::string actual_qstring3(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, empty_crange, key_column_values_timestamp));
    std::string expected_qstring3(
        "SELECT key,column1,value,WRITETIME(value) FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839");
    EXPECT_EQ(expected_qstring3, actual_qstring3);

    // Read row key, column1 and value with timestamp and use column name
    // in filter. i.e. column1 >= "c"
    GenDb::Blob col_filter(reinterpret_cast<const uint8_t *>("d"), 1);
    GenDb::ColumnNameRange crange;
    crange.start_ = boost::assign::list_of(GenDb::DbDataValue(col_filter));
    std::string actual_qstring4(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, all_values, crange, key_column_values_timestamp));
    std::string expected_qstring4(
        "SELECT key,column1,value,WRITETIME(value) FROM SliceSelectTable "
        "WHERE key='Test' AND "
        "key2=123456789 AND "
        "key3=123456789 AND "
        "key4=" + tuuid_s_ + " AND "
        "key5=128 AND "
        "key6=65535 AND "
        "key7=1.123 AND "
        "key8=0x303132333435363738393031323334353637383930313233343536373839 "
        "AND (column1) >= (0x64)");
    EXPECT_EQ(expected_qstring4, actual_qstring4);
}

TEST_F(CqlIfTest, SelectFromTableMultipleRead) {
    std::string table("SliceSelectTable");
    // Empty field list. Read all fields
    GenDb::ColumnNameRange empty_crange;
    GenDb::FieldNamesToReadVec empty_fields;

    std::vector <std::string> str_keys = list_of("uuid1")("uuid2")("uuid3")("uuid4");
    std::vector<GenDb::DbDataValueVec> keys; 
    BOOST_FOREACH(std::string key, str_keys) {
        keys.push_back(list_of(GenDb::DbDataValue(key)));
    }
    std::string actual_qstring(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, keys, empty_crange, empty_fields));
    std::string expected_qstring(
        "SELECT * FROM SliceSelectTable "
        "WHERE key IN ('uuid1','uuid2','uuid3','uuid4')");
    EXPECT_EQ(expected_qstring, actual_qstring);

    // Read only row key
    GenDb::FieldNamesToReadVec only_key;
    only_key.push_back(boost::make_tuple("key", true, false, false));
    std::string actual_qstring1(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, keys, empty_crange, only_key));
    std::string expected_qstring1(
        "SELECT key FROM SliceSelectTable "
        "WHERE key IN ('uuid1','uuid2','uuid3','uuid4')");
    EXPECT_EQ(expected_qstring1, actual_qstring1);

    // Read row key, column1 and value without timestamp
    GenDb::FieldNamesToReadVec key_column_values;
    key_column_values.push_back(boost::make_tuple("key", true, false, false));
    key_column_values.push_back(boost::make_tuple("column1", false, true, false));
    key_column_values.push_back(boost::make_tuple("value", false, false, false));
    std::string actual_qstring2(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, keys, empty_crange, key_column_values));
    std::string expected_qstring2(
        "SELECT key,column1,value FROM SliceSelectTable "
        "WHERE key IN ('uuid1','uuid2','uuid3','uuid4')");
    EXPECT_EQ(expected_qstring2, actual_qstring2);

    // Read row key, column1 and value with timestamp
    GenDb::FieldNamesToReadVec key_column_values_timestamp;
    key_column_values_timestamp.push_back(boost::make_tuple("key", true, false,
                                                            false));
    key_column_values_timestamp.push_back(boost::make_tuple("column1", false,
                                                            true, false));
    key_column_values_timestamp.push_back(boost::make_tuple("value", false,
                                                            false, true));
    std::string actual_qstring3(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, keys, empty_crange, key_column_values_timestamp));
    std::string expected_qstring3(
        "SELECT key,column1,value,WRITETIME(value) FROM SliceSelectTable "
        "WHERE key IN ('uuid1','uuid2','uuid3','uuid4')");
    EXPECT_EQ(expected_qstring3, actual_qstring3);

    // Read row key, column1 and value with timestamp and use column name
    // in filter. i.e. column1 >= "c"
    GenDb::Blob col_filter(reinterpret_cast<const uint8_t *>("d"), 1);
    GenDb::ColumnNameRange crange;
    crange.start_ = boost::assign::list_of(GenDb::DbDataValue(col_filter));
    std::string actual_qstring4(
        cass::cql::impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
            table, keys, crange, key_column_values_timestamp));
    std::string expected_qstring4(
        "SELECT key,column1,value,WRITETIME(value) FROM SliceSelectTable "
        "WHERE key IN ('uuid1','uuid2','uuid3','uuid4') "
        "AND (column1) >= (0x64)");
    EXPECT_EQ(expected_qstring4, actual_qstring4);
}

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::ContainerEq;

TEST_F(CqlIfTest, DynamicCfGetResultAllRows) {
    cass::cql::test::MockCassLibrary mock_cci;
    size_t rk_count(1);
    size_t ck_count(1);
    size_t ccount(rk_count + ck_count + 1);
    size_t rows(3);
    EXPECT_CALL(mock_cci, CassIteratorNext(_))
        .Times(rows + 1)
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_false));
    EXPECT_CALL(mock_cci, CassResultColumnCount(_))
        .Times(rows)
        .WillRepeatedly(Return(ccount));
    // Return dummy pointer to avoid assert
    uint8_t dummy_cass_value;
    EXPECT_CALL(mock_cci, CassRowGetColumn(_, _))
        .Times(rows * ccount)
        .WillRepeatedly(Return(
            reinterpret_cast<const CassValue *>(&dummy_cass_value)));
    EXPECT_CALL(mock_cci, GetCassValueType(_))
        .Times(rows * ccount)
        .WillRepeatedly(Return(CASS_VALUE_TYPE_TEXT));
    EXPECT_CALL(mock_cci, CassValueGetString(_, _, _))
        .Times(rows * ccount)
        .WillOnce(DoAll(SetArgPointee<1>("key"),
            SetArgPointee<2>(strlen("key")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column"),
            SetArgPointee<2>(strlen("column")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("value"),
            SetArgPointee<2>(strlen("value")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key"),
            SetArgPointee<2>(strlen("key")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column1"),
            SetArgPointee<2>(strlen("column1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("value1"),
            SetArgPointee<2>(strlen("value1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key1"),
            SetArgPointee<2>(strlen("key1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column"),
            SetArgPointee<2>(strlen("column")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("value"),
            SetArgPointee<2>(strlen("value")), Return(CASS_OK)));
    GenDb::ColList *col_list(new GenDb::ColList);
    col_list->rowkey_.push_back("key");
    GenDb::NewCol *column(new GenDb::NewCol(
        new GenDb::DbDataValueVec(1, "column"),
        new GenDb::DbDataValueVec(1, "value"), 0));
    col_list->columns_.push_back(column);
    GenDb::NewCol *column1(new GenDb::NewCol(
        new GenDb::DbDataValueVec(1, "column1"),
        new GenDb::DbDataValueVec(1, "value1"), 0));
    col_list->columns_.push_back(column1);
    GenDb::ColList *col_list1(new GenDb::ColList);
    col_list1->rowkey_.push_back("key1");
    GenDb::NewCol *column2(new GenDb::NewCol(
        new GenDb::DbDataValueVec(1, "column"),
        new GenDb::DbDataValueVec(1, "value"), 0));
    col_list1->columns_.push_back(column2);
    GenDb::ColListVec expected_v_col_list;
    expected_v_col_list.push_back(col_list);
    expected_v_col_list.push_back(col_list1);
    GenDb::ColListVec actual_v_col_list;
    cass::cql::impl::CassResultPtr result(NULL, &mock_cci);
    cass::cql::impl::DynamicCfGetResult(&mock_cci, &result, rk_count,
        ck_count, &actual_v_col_list);
    EXPECT_THAT(actual_v_col_list, ContainerEq(expected_v_col_list));
}

TEST_F(CqlIfTest, StaticCfGetResultAllRows) {
    cass::cql::test::MockCassLibrary mock_cci;
    size_t rk_count(1);
    size_t ccount(rk_count + 2);
    size_t rows(3);
    EXPECT_CALL(mock_cci, CassIteratorNext(_))
        .Times(rows + 1)
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_true))
        .WillOnce(Return(cass_false));
    EXPECT_CALL(mock_cci, CassResultColumnCount(_))
        .Times(rows)
        .WillRepeatedly(Return(ccount));
    EXPECT_CALL(mock_cci, CassResultColumnName(_, _, _, _))
        .Times(rows * ccount)
        .WillOnce(DoAll(SetArgPointee<2>("KeyRow"),
            SetArgPointee<3>(strlen("KeyRow")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column1"),
            SetArgPointee<3>(strlen("Column1")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column2"),
            SetArgPointee<3>(strlen("Column2")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("KeyRow"),
            SetArgPointee<3>(strlen("KeyRow")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column1"),
            SetArgPointee<3>(strlen("Column1")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column2"),
            SetArgPointee<3>(strlen("Column2")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("KeyRow"),
            SetArgPointee<3>(strlen("KeyRow")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column1"),
            SetArgPointee<3>(strlen("Column1")),
            Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<2>("Column2"),
            SetArgPointee<3>(strlen("Column2")),
            Return(CASS_OK)));
    // Return dummy pointer to avoid assert
    uint8_t dummy_cass_value;
    EXPECT_CALL(mock_cci, CassRowGetColumn(_, _))
        .Times(rows * (rk_count + ccount))
        .WillRepeatedly(Return(
            reinterpret_cast<const CassValue *>(&dummy_cass_value)));
    EXPECT_CALL(mock_cci, GetCassValueType(_))
        .Times(rows * (rk_count + ccount))
        .WillRepeatedly(Return(CASS_VALUE_TYPE_TEXT));
    EXPECT_CALL(mock_cci, CassValueGetString(_, _, _))
        .Times(rows * (rk_count + ccount))
        .WillOnce(DoAll(SetArgPointee<1>("key"),
            SetArgPointee<2>(strlen("key")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key"),
            SetArgPointee<2>(strlen("key")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column1"),
            SetArgPointee<2>(strlen("column1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column2"),
            SetArgPointee<2>(strlen("column2")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key1"),
            SetArgPointee<2>(strlen("key1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key1"),
            SetArgPointee<2>(strlen("key1")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column11"),
            SetArgPointee<2>(strlen("column11")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column21"),
            SetArgPointee<2>(strlen("column21")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key2"),
            SetArgPointee<2>(strlen("key2")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("key2"),
            SetArgPointee<2>(strlen("key2")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column12"),
            SetArgPointee<2>(strlen("column12")), Return(CASS_OK)))
        .WillOnce(DoAll(SetArgPointee<1>("column22"),
            SetArgPointee<2>(strlen("column22")), Return(CASS_OK)));

    GenDb::ColListVec expected_v_col_list;
    // ColList
    GenDb::ColList *col_list(new GenDb::ColList);
    col_list->rowkey_.push_back("key");
    GenDb::NewCol *rcolumn(new GenDb::NewCol("KeyRow", "key", 0));
    col_list->columns_.push_back(rcolumn);
    GenDb::NewCol *column1(new GenDb::NewCol("Column1", "column1", 0));
    col_list->columns_.push_back(column1);
    GenDb::NewCol *column2(new GenDb::NewCol("Column2", "column2", 0));
    col_list->columns_.push_back(column2);
    expected_v_col_list.push_back(col_list);
    // ColList1
    GenDb::ColList *col_list1(new GenDb::ColList);
    col_list1->rowkey_.push_back("key1");
    GenDb::NewCol *rcolumn1(new GenDb::NewCol("KeyRow", "key1", 0));
    col_list1->columns_.push_back(rcolumn1);
    GenDb::NewCol *column11(new GenDb::NewCol("Column1", "column11", 0));
    col_list1->columns_.push_back(column11);
    GenDb::NewCol *column21(new GenDb::NewCol("Column2", "column21", 0));
    col_list1->columns_.push_back(column21);
    expected_v_col_list.push_back(col_list1);
    // ColList2
    GenDb::ColList *col_list2(new GenDb::ColList);
    col_list2->rowkey_.push_back("key2");
    GenDb::NewCol *rcolumn2(new GenDb::NewCol("KeyRow", "key2", 0));
    col_list2->columns_.push_back(rcolumn2);
    GenDb::NewCol *column12(new GenDb::NewCol("Column1", "column12", 0));
    col_list2->columns_.push_back(column12);
    GenDb::NewCol *column22(new GenDb::NewCol("Column2", "column22", 0));
    col_list2->columns_.push_back(column22);
    expected_v_col_list.push_back(col_list2);

    GenDb::ColListVec actual_v_col_list;
    cass::cql::impl::CassResultPtr result(NULL, &mock_cci);
    cass::cql::impl::StaticCfGetResult(&mock_cci, &result, rk_count,
        &actual_v_col_list);
    EXPECT_THAT(actual_v_col_list, ContainerEq(expected_v_col_list));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
