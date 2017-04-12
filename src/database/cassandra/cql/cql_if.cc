//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#include <assert.h>

#include <tbb/atomic.h>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/unordered_map.hpp>

#include <cassandra.h>

#include <base/logging.h>
#include <base/task.h>
#include <base/timer.h>
#include <base/string_util.h>
#include <io/event_manager.h>
#include <database/gendb_if.h>
#include <database/gendb_constants.h>
#include <database/cassandra/cql/cql_if.h>
#include <database/cassandra/cql/cql_if_impl.h>
#include <database/cassandra/cql/cql_lib_if.h>


#define CQLIF_DEBUG "CqlTraceBufDebug"
#define CQLIF_INFO "CqlTraceBufInfo"
#define CQLIF_ERR  "CqlTraceBufErr"

SandeshTraceBufferPtr CqlTraceDebugBuf(SandeshTraceBufferCreate(
     CQLIF_DEBUG, 10000));
SandeshTraceBufferPtr CqlTraceInfoBuf(SandeshTraceBufferCreate(
     CQLIF_INFO, 10000));
SandeshTraceBufferPtr CqlTraceErrBuf(SandeshTraceBufferCreate(
     CQLIF_ERR, 20000));

#define CQLIF_DEBUG_TRACE(_Msg)                                           \
    do {                                                                  \
         std::stringstream ss;                                            \
         ss << __func__ << ":" << __FILE__ << ":" <<                      \
             __LINE__ << ": " << _Msg;                                    \
         CQL_TRACE_TRACE(CqlTraceDebugBuf,ss.str());                      \
       } while (false)                                                    \

#define CQLIF_INFO_TRACE(_Msg)                                            \
    do {                                                                  \
         std::stringstream ss;                                            \
         ss << __func__ << ":" << __FILE__ << ":" <<                      \
             __LINE__ << ": " << _Msg;                                    \
         CQL_TRACE_TRACE(CqlTraceInfoBuf,ss.str());                        \
       } while (false)                                                    \

#define CQLIF_ERR_TRACE(_Msg)                                             \
    do {                                                                  \
         std::stringstream ss;                                            \
         ss << __func__ << ":" << __FILE__ << ":" <<                      \
             __LINE__ << ": " << _Msg;                                    \
         CQL_TRACE_TRACE(CqlTraceErrBuf,ss.str());                          \
       } while (false)                                                    \

#define CASS_LIB_TRACE(_Level, _Msg)                                      \
    do {                                                                  \
        if (_Level == log4cplus::ERROR_LOG_LEVEL) {                       \
            CQL_TRACE_TRACE(CqlTraceErrBuf, _Msg);                          \
        }else if (_Level == log4cplus::DEBUG_LOG_LEVEL) {                 \
            CQL_TRACE_TRACE(CqlTraceDebugBuf, _Msg);                      \
        }else {                                                           \
            CQL_TRACE_TRACE(CqlTraceInfoBuf, _Msg);                        \
        }                                                                 \
    } while (false)                                                       \

#define CQLIF_LOG(_Level, _Msg)                                           \
    do {                                                                  \
        if (LoggingDisabled()) break;                                     \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();          \
        LOG4CPLUS_##_Level(logger, __func__ << ":" << __FILE__ << ":" <<  \
            __LINE__ << ": " << _Msg);                                    \
    } while (false)

#define CQLIF_LOG_ERR(_Msg)                                               \
    do {                                                                  \
        LOG(ERROR, __func__ << ":" << __FILE__ << ":" << __LINE__ << ": " \
            << _Msg);                                                     \
    } while (false)

namespace cass {
namespace cql {
namespace impl {

// CassString convenience structure
struct CassString {
    CassString() :
        data(NULL),
        length(0) {
    }

    CassString(const char *data) :
        data(data),
        length(strlen(data)) {
    }

    CassString(const char* data, size_t length) :
        data(data),
        length(length) {
    }

    const char* data;
    size_t length;
};

// CassUuid encode and decode
static inline void encode_uuid(char* output, const CassUuid &uuid) {
    uint64_t time_and_version = uuid.time_and_version;
    output[3] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;
    output[2] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;
    output[1] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;
    output[0] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;

    output[5] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;
    output[4] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;

    output[7] = static_cast<char>(time_and_version & 0x00000000000000FFLL);
    time_and_version >>= 8;
    output[6] = static_cast<char>(time_and_version & 0x000000000000000FFLL);

    uint64_t clock_seq_and_node = uuid.clock_seq_and_node;
    for (size_t i = 0; i < 8; ++i) {
        output[15 - i] = static_cast<char>(clock_seq_and_node & 0x00000000000000FFL);
        clock_seq_and_node >>= 8;
    }
}

static inline char* decode_uuid(char* input, CassUuid* output) {
  output->time_and_version  = static_cast<uint64_t>(static_cast<uint8_t>(input[3]));
  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[2])) << 8;
  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[1])) << 16;
  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[0])) << 24;

  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[5])) << 32;
  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[4])) << 40;

  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[7])) << 48;
  output->time_and_version |= static_cast<uint64_t>(static_cast<uint8_t>(input[6])) << 56;

  output->clock_seq_and_node = 0;
  for (size_t i = 0; i < 8; ++i) {
    output->clock_seq_and_node |= static_cast<uint64_t>(static_cast<uint8_t>(input[15 - i])) << (8 * i);
  }
  return input + 16;
}

static const char * DbDataType2CassType(
    const GenDb::DbDataType::type &db_type) {
    switch (db_type) {
      case GenDb::DbDataType::AsciiType:
        return "ascii";
      case GenDb::DbDataType::LexicalUUIDType:
        return "uuid";
      case GenDb::DbDataType::TimeUUIDType:
        return "timeuuid";
      case GenDb::DbDataType::Unsigned8Type:
      case GenDb::DbDataType::Unsigned16Type:
      case GenDb::DbDataType::Unsigned32Type:
        return "int";
      case GenDb::DbDataType::Unsigned64Type:
        return "bigint";
      case GenDb::DbDataType::DoubleType:
        return "double";
      case GenDb::DbDataType::UTF8Type:
        return "text";
      case GenDb::DbDataType::InetType:
        return "inet";
      case GenDb::DbDataType::IntegerType:
        return "varint";
      case GenDb::DbDataType::BlobType:
        return "blob";
      default:
        assert(false && "Invalid data type");
        return "";
    }
}

static std::string DbDataTypes2CassTypes(
    const GenDb::DbDataTypeVec &v_db_types) {
    assert(!v_db_types.empty());
    return std::string(DbDataType2CassType(v_db_types[0]));
}

static CassConsistency Db2CassConsistency(
    GenDb::DbConsistency::type dconsistency) {
    switch (dconsistency) {
      case GenDb::DbConsistency::ANY:
        return CASS_CONSISTENCY_ANY;
      case GenDb::DbConsistency::ONE:
        return CASS_CONSISTENCY_ONE;
      case GenDb::DbConsistency::TWO:
        return CASS_CONSISTENCY_TWO;
      case GenDb::DbConsistency::THREE:
        return CASS_CONSISTENCY_THREE;
      case GenDb::DbConsistency::QUORUM:
        return CASS_CONSISTENCY_QUORUM;
      case GenDb::DbConsistency::ALL:
        return CASS_CONSISTENCY_ALL;
      case GenDb::DbConsistency::LOCAL_QUORUM:
        return CASS_CONSISTENCY_LOCAL_QUORUM;
      case GenDb::DbConsistency::EACH_QUORUM:
        return CASS_CONSISTENCY_EACH_QUORUM;
      case GenDb::DbConsistency::SERIAL:
        return CASS_CONSISTENCY_SERIAL;
      case GenDb::DbConsistency::LOCAL_SERIAL:
        return CASS_CONSISTENCY_LOCAL_SERIAL;
      case GenDb::DbConsistency::LOCAL_ONE:
        return CASS_CONSISTENCY_LOCAL_ONE;
      case GenDb::DbConsistency::UNKNOWN:
      default:
        return CASS_CONSISTENCY_UNKNOWN;
    }
}

// Cass Query Printer
class CassQueryPrinter : public boost::static_visitor<> {
 public:
    CassQueryPrinter(std::ostream &os, bool quote_strings) :
        os_(os),
        quote_strings_(quote_strings) {
    }
    CassQueryPrinter(std::ostream &os) :
        os_(os),
        quote_strings_(true) {
    }
    template<typename T>
    void operator()(const T &t) const {
        os_ << t;
    }
    void operator()(const boost::uuids::uuid &tuuid) const {
        os_ << to_string(tuuid);
    }
    // uint8_t must be handled specially because ostream sees
    // uint8_t as a text type instead of an integer type
    void operator()(const uint8_t &tu8) const {
        os_ << (uint16_t)tu8;
    }
    void operator()(const std::string &tstring) const {
        if (quote_strings_) {
            os_ << "'" << tstring << "'";
        } else {
            os_ << tstring;
        }
    }
    // CQL int is 32 bit signed integer
    void operator()(const uint32_t &tu32) const {
        os_ << (int32_t)tu32;
    }
    // CQL bigint is 64 bit signed long
    void operator()(const uint64_t &tu64) const {
        os_ << (int64_t)tu64;
    }
    void operator()(const IpAddress &tipaddr) const {
        os_ << "'" << tipaddr << "'";
    }
    std::ostream &os_;
    bool quote_strings_;
};

//
// CassStatement bind
//
class CassStatementIndexBinder : public boost::static_visitor<> {
 public:
    CassStatementIndexBinder(interface::CassLibrary *cci,
        CassStatement *statement) :
        cci_(cci),
        statement_(statement) {
    }
    void operator()(const boost::blank &tblank, size_t index) const {
        assert(false && "CassStatement bind to boost::blank not supported");
    }
    void operator()(const std::string &tstring, size_t index) const {
        CassError rc(cci_->CassStatementBindStringN(statement_, index,
            tstring.c_str(), tstring.length()));
        assert(rc == CASS_OK);
    }
    void operator()(const boost::uuids::uuid &tuuid, size_t index) const {
        CassUuid cuuid;
        decode_uuid((char *)&tuuid, &cuuid);
        CassError rc(cci_->CassStatementBindUuid(statement_, index, cuuid));
        assert(rc == CASS_OK);
    }
    void operator()(const uint8_t &tu8, size_t index) const {
        CassError rc(cci_->CassStatementBindInt32(statement_, index, tu8));
        assert(rc == CASS_OK);
    }
    void operator()(const uint16_t &tu16, size_t index) const {
        CassError rc(cci_->CassStatementBindInt32(statement_, index, tu16));
        assert(rc == CASS_OK);
    }
    void operator()(const uint32_t &tu32, size_t index) const {
        assert(tu32 <= (uint32_t)std::numeric_limits<int32_t>::max());
        CassError rc(cci_->CassStatementBindInt32(statement_, index,
            (cass_int32_t)tu32));
        assert(rc == CASS_OK);
    }
    void operator()(const uint64_t &tu64, size_t index) const {
        assert(tu64 <= (uint64_t)std::numeric_limits<int64_t>::max());
        CassError rc(cci_->CassStatementBindInt64(statement_, index,
            (cass_int64_t)tu64));
        assert(rc == CASS_OK);
    }
    void operator()(const double &tdouble, size_t index) const {
        CassError rc(cci_->CassStatementBindDouble(statement_, index,
            (cass_double_t)tdouble));
        assert(rc == CASS_OK);
    }
    void operator()(const IpAddress &tipaddr, size_t index) const {
        CassInet cinet;
        if (tipaddr.is_v4()) {
            boost::asio::ip::address_v4 tv4(tipaddr.to_v4());
            cinet = cci_->CassInetInitV4(tv4.to_bytes().c_array());
        } else {
            boost::asio::ip::address_v6 tv6(tipaddr.to_v6());
            cinet = cci_->CassInetInitV6(tv6.to_bytes().c_array());
        }
        CassError rc(cci_->CassStatementBindInet(statement_, index,
            cinet));
        assert(rc == CASS_OK);
    }
    void operator()(const GenDb::Blob &tblob, size_t index) const {
        CassError rc(cci_->CassStatementBindBytes(statement_, index,
            tblob.data(), tblob.size()));
        assert(rc == CASS_OK);
    }
    interface::CassLibrary *cci_;
    CassStatement *statement_;
};

class CassStatementNameBinder : public boost::static_visitor<> {
 public:
    CassStatementNameBinder(interface::CassLibrary *cci,
        CassStatement *statement) :
        cci_(cci),
        statement_(statement) {
    }
    void operator()(const boost::blank &tblank, const char *name) const {
        assert(false && "CassStatement bind to boost::blank not supported");
    }
    void operator()(const std::string &tstring, const char *name) const {
        CassError rc(cci_->CassStatementBindStringByNameN(statement_, name,
            strlen(name), tstring.c_str(), tstring.length()));
        assert(rc == CASS_OK);
    }
    void operator()(const boost::uuids::uuid &tuuid, const char *name) const {
        CassUuid cuuid;
        decode_uuid((char *)&tuuid, &cuuid);
        CassError rc(cci_->CassStatementBindUuidByName(statement_, name,
            cuuid));
        assert(rc == CASS_OK);
    }
    void operator()(const uint8_t &tu8, const char *name) const {
        CassError rc(cci_->CassStatementBindInt32ByName(statement_, name,
            tu8));
        assert(rc == CASS_OK);
    }
    void operator()(const uint16_t &tu16, const char *name) const {
        CassError rc(cci_->CassStatementBindInt32ByName(statement_, name,
            tu16));
        assert(rc == CASS_OK);
    }
    void operator()(const uint32_t &tu32, const char *name) const {
        assert(tu32 <= (uint32_t)std::numeric_limits<int32_t>::max());
        CassError rc(cci_->CassStatementBindInt32ByName(statement_, name,
            (cass_int32_t)tu32));
        assert(rc == CASS_OK);
    }
    void operator()(const uint64_t &tu64, const char *name) const {
        assert(tu64 <= (uint64_t)std::numeric_limits<int64_t>::max());
        CassError rc(cci_->CassStatementBindInt64ByName(statement_, name,
            (cass_int64_t)tu64));
        assert(rc == CASS_OK);
    }
    void operator()(const double &tdouble, const char *name) const {
        CassError rc(cci_->CassStatementBindDoubleByName(statement_, name,
            (cass_double_t)tdouble));
        assert(rc == CASS_OK);
    }
    void operator()(const IpAddress &tipaddr, const char *name) const {
        CassInet cinet;
        if (tipaddr.is_v4()) {
            boost::asio::ip::address_v4 tv4(tipaddr.to_v4());
            cinet = cci_->CassInetInitV4(tv4.to_bytes().c_array());
        } else {
            boost::asio::ip::address_v6 tv6(tipaddr.to_v6());
            cinet = cci_->CassInetInitV6(tv6.to_bytes().c_array());
        }
        CassError rc(cci_->CassStatementBindInetByName(statement_, name,
            cinet));
        assert(rc == CASS_OK);
    }
    void operator()(const GenDb::Blob &tblob, const char *name) const {
        CassError rc(cci_->CassStatementBindBytesByNameN(statement_, name,
            strlen(name), tblob.data(), tblob.size()));
        assert(rc == CASS_OK);
    }
    interface::CassLibrary *cci_;
    CassStatement *statement_;
};

static const char * kQCompactionStrategy(
    "compaction = {'class': "
    "'org.apache.cassandra.db.compaction.%s'}");
static const std::string kQGCGraceSeconds("gc_grace_seconds = 0");
static const std::string kQReadRepairChanceDTCS(
             "read_repair_chance = 0.0");

//
// Cf2CassCreateTableIfNotExists
//

std::string StaticCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf,
    const std::string &compaction_strategy) {
    std::ostringstream query;
    // Table name
    query << "CREATE TABLE IF NOT EXISTS " << cf.cfname_ << " ";
    // Row key
    const GenDb::DbDataTypeVec &rkeys(cf.key_validation_class);
    assert(rkeys.size() == 1);
    query << "(key " << DbDataType2CassType(rkeys[0]) <<
        " PRIMARY KEY";
    // Columns
    const GenDb::NewCf::SqlColumnMap &columns(cf.cfcolumns_);
    assert(!columns.empty());
    BOOST_FOREACH(const GenDb::NewCf::SqlColumnMap::value_type &column,
        columns) {
        query << ", \"" << column.first << "\" " <<
            DbDataType2CassType(column.second);
    }
    char cbuf[512];
    int n(snprintf(cbuf, sizeof(cbuf), kQCompactionStrategy,
       compaction_strategy.c_str()));
    assert(!(n < 0 || n >= (int)sizeof(cbuf)));

    // The compaction strategy DateTieredCompactionStrategy precludes 
    // using read repair, because of the way timestamps are checked for 
    // DTCS compaction.In this case, you must set read_repair_chance to 
    // zero. For other compaction strategies, read repair should be 
    // enabled with a read_repair_chance value of 0.2 being typical
    if (compaction_strategy == 
        GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY) {
        query << ") WITH " << std::string(cbuf) << " AND " <<
            kQReadRepairChanceDTCS << " AND " << kQGCGraceSeconds;
    } else {
        query << ") WITH " << std::string(cbuf) << " AND " << 
            kQGCGraceSeconds;
    }

    return query.str();
}

std::string DynamicCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf,
    const std::string &compaction_strategy) {
    std::ostringstream query;
    // Table name
    query << "CREATE TABLE IF NOT EXISTS " << cf.cfname_ << " (";
    // Row key
    const GenDb::DbDataTypeVec &rkeys(cf.key_validation_class);
    int rk_size(rkeys.size());
    for (int i = 0; i < rk_size; i++) {
        if (i) {
            int key_num(i + 1);
            query << "key" << key_num;
        } else {
            query << "key";
        }
        query << " " << DbDataType2CassType(rkeys[i]) << ", ";
    }
    // Column name
    const GenDb::DbDataTypeVec &cnames(cf.comparator_type);
    int cn_size(cnames.size());
    for (int i = 0; i < cn_size; i++) {
        int cnum(i + 1);
        query << "column" << cnum << " " <<
            DbDataType2CassType(cnames[i]) << ", ";
    }
    // Value
    const GenDb::DbDataTypeVec &values(cf.default_validation_class);
    if (values.size() > 0) {
        query << "value" << " " << DbDataTypes2CassTypes(values) << ", ";
    }
    // Primarry Key
    query << "PRIMARY KEY (";
    std::ostringstream rkey_ss;
    for (int i = 0; i < rk_size; i++) {
        if (i) {
            int key_num(i + 1);
            rkey_ss << ", key" << key_num;
        } else {
            rkey_ss << "key";
        }
    }
    if (rk_size >= 2) {
        query << "(" << rkey_ss.str() << "), ";
    } else {
        query << rkey_ss.str() << ", ";
    }
    for (int i = 0; i < cn_size; i++) {
        int cnum(i + 1);
        if (i) {
            query << ", ";
        }
        query << "column" << cnum;
    }
    char cbuf[512];
    int n(snprintf(cbuf, sizeof(cbuf), kQCompactionStrategy,
       compaction_strategy.c_str()));
    assert(!(n < 0 || n >= (int)sizeof(cbuf)));

    // The compaction strategy DateTieredCompactionStrategy precludes
    // using read repair, because of the way timestamps are checked for
    // DTCS compaction.In this case, you must set read_repair_chance to
    // zero. For other compaction strategies, read repair should be
    // enabled with a read_repair_chance value of 0.2 being typical
    if (compaction_strategy == 
        GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY) {
        query << ")) WITH " << std::string(cbuf) << " AND " <<
            kQReadRepairChanceDTCS << " AND " << kQGCGraceSeconds;
    } else {
        query << ")) WITH " << std::string(cbuf) << " AND " <<
            kQGCGraceSeconds;
    }
    return query.str();
}

//
// Cf2CassInsertIntoTable
//

std::string StaticCf2CassInsertIntoTable(const GenDb::ColList *v_columns) {
    std::ostringstream query;
    // Table
    const std::string &table(v_columns->cfname_);
    query << "INSERT INTO " << table << " (";
    std::ostringstream values_ss;
    values_ss << "VALUES (";
    CassQueryPrinter values_printer(values_ss);
    // Row keys
    const GenDb::DbDataValueVec &rkeys(v_columns->rowkey_);
    int rk_size(rkeys.size());
    for (int i = 0; i < rk_size; i++) {
        if (i) {
            int key_num(i + 1);
            query << ", key" << key_num;
        } else {
            query << "key";
        }
        if (i) {
            values_ss << ", ";
        }
        boost::apply_visitor(values_printer, rkeys[i]);
    }
    // Columns
    int cttl(-1);
    CassQueryPrinter cnames_printer(query, false);
    BOOST_FOREACH(const GenDb::NewCol &column, v_columns->columns_) {
        assert(column.cftype_ == GenDb::NewCf::COLUMN_FAMILY_SQL);
        // Column Name
        query << ", ";
        const GenDb::DbDataValueVec &cnames(*column.name.get());
        assert(cnames.size() == 1);
        // Double quote column name strings
        query << "\"";
        boost::apply_visitor(cnames_printer, cnames[0]);
        query << "\"";
        // Column Values
        values_ss << ", ";
        const GenDb::DbDataValueVec &cvalues(*column.value.get());
        assert(cvalues.size() == 1);
        boost::apply_visitor(values_printer, cvalues[0]);
        // Column TTL
        cttl = column.ttl;
    }
    query << ") ";
    values_ss << ")";
    query << values_ss.str();
    if (cttl > 0) {
        query << " USING TTL " << cttl;
    }
    return query.str();
}

std::string DynamicCf2CassInsertIntoTable(const GenDb::ColList *v_columns) {
    std::ostringstream query;
    // Table
    const std::string &table(v_columns->cfname_);
    query << "INSERT INTO " << table << " (";
    std::ostringstream values_ss;
    // Row keys
    const GenDb::DbDataValueVec &rkeys(v_columns->rowkey_);
    int rk_size(rkeys.size());
    CassQueryPrinter values_printer(values_ss);
    for (int i = 0; i < rk_size; i++) {
        if (i) {
            int key_num(i + 1);
            query << ", key" << key_num;
        } else {
            query << "key";
        }
        boost::apply_visitor(values_printer, rkeys[i]);
        values_ss << ", ";
    }
    // Columns
    const GenDb::NewColVec &columns(v_columns->columns_);
    assert(columns.size() == 1);
    const GenDb::NewCol &column(columns[0]);
    assert(column.cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL);
    // Column Names
    const GenDb::DbDataValueVec &cnames(*column.name.get());
    int cn_size(cnames.size());
    for (int i = 0; i < cn_size; i++) {
        int cnum(i + 1);
        query << ", column" << cnum;
        boost::apply_visitor(values_printer, cnames[i]);
        if (i != cn_size - 1) {
            values_ss << ", ";
        }
    }
    // Column Values
    const GenDb::DbDataValueVec &cvalues(*column.value.get());
    if (cvalues.size() > 0) {
        query << ", value) VALUES (";
        values_ss << ", ";
        boost::apply_visitor(values_printer, cvalues[0]);
    } else {
        query << ") VALUES (";
    }
    values_ss << ")";
    query << values_ss.str();
    if (column.ttl > 0) {
        query << " USING TTL " << column.ttl;
    }
    return query.str();
}

//
// Cf2CassPrepareInsertIntoTable
//

std::string StaticCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf) {
   std::ostringstream query;
    // Table name
    query << "INSERT INTO " << cf.cfname_ << " ";
    // Row key
    const GenDb::DbDataTypeVec &rkeys(cf.key_validation_class);
    assert(rkeys.size() == 1);
    std::ostringstream values_ss;
    query << "(key";
    values_ss << ") VALUES (?";
    // Columns
    const GenDb::NewCf::SqlColumnMap &columns(cf.cfcolumns_);
    assert(!columns.empty());
    BOOST_FOREACH(const GenDb::NewCf::SqlColumnMap::value_type &column,
        columns) {
        query << ", \"" << column.first << "\"";
        values_ss << ", ?";
    }
    query << values_ss.str();
    query << ") USING TTL ?";
    return query.str();
}

std::string DynamicCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf) {
    std::ostringstream query;
    // Table name
    query << "INSERT INTO " << cf.cfname_ << " (";
    // Row key
    const GenDb::DbDataTypeVec &rkeys(cf.key_validation_class);
    int rk_size(rkeys.size());
    std::ostringstream values_ss;
    for (int i = 0; i < rk_size; i++) {
        if (i) {
            int key_num(i + 1);
            query << "key" << key_num;
        } else {
            query << "key";
        }
        query << ", ";
        values_ss << "?, ";
    }
    // Column name
    const GenDb::DbDataTypeVec &cnames(cf.comparator_type);
    int cn_size(cnames.size());
    for (int i = 0; i < cn_size; i++) {
        int cnum(i + 1);
        query << "column" << cnum;
        values_ss << "?";
        if (i != cn_size - 1) {
            query << ", ";
            values_ss << ", ";
        }
    }
    // Value
    const GenDb::DbDataTypeVec &values(cf.default_validation_class);
    if (values.size() > 0) {
        query << ", value";
        values_ss << ", ?";
    }
    query << ") VALUES (";
    values_ss << ")";
    query << values_ss.str();
    query << " USING TTL ?";
    return query.str();
}

//
// Cf2CassPrepareBind
//

bool StaticCf2CassPrepareBind(interface::CassLibrary *cci,
    CassStatement *statement,
    const GenDb::ColList *v_columns) {
    CassStatementNameBinder values_binder(cci, statement);
    // Row keys
    const GenDb::DbDataValueVec &rkeys(v_columns->rowkey_);
    int rk_size(rkeys.size());
    size_t idx(0);
    for (; (int) idx < rk_size; idx++) {
        std::string rk_name;
        if (idx) {
            int key_num(idx + 1);
            rk_name = "key" + integerToString(key_num);
        } else {
            rk_name = "key";
        }
        boost::apply_visitor(boost::bind(values_binder, _1, rk_name.c_str()),
            rkeys[idx]);
    }
    // Columns
    int cttl(-1);
    BOOST_FOREACH(const GenDb::NewCol &column, v_columns->columns_) {
        assert(column.cftype_ == GenDb::NewCf::COLUMN_FAMILY_SQL);
        const GenDb::DbDataValueVec &cnames(*column.name.get());
        assert(cnames.size() == 1);
        assert(cnames[0].which() == GenDb::DB_VALUE_STRING);
        std::string cname(boost::get<std::string>(cnames[0]));
        const GenDb::DbDataValueVec &cvalues(*column.value.get());
        assert(cvalues.size() == 1);
        boost::apply_visitor(boost::bind(values_binder, _1, cname.c_str()),
            cvalues[0]);
        // Column TTL
        cttl = column.ttl;
        idx++;
    }
    CassError rc(cci->CassStatementBindInt32(statement, idx++,
        (cass_int32_t)cttl));
    assert(rc == CASS_OK);
    return true;
}

bool DynamicCf2CassPrepareBind(interface::CassLibrary *cci,
    CassStatement *statement,
    const GenDb::ColList *v_columns) {
    CassStatementIndexBinder values_binder(cci, statement);
    // Row keys
    const GenDb::DbDataValueVec &rkeys(v_columns->rowkey_);
    int rk_size(rkeys.size());
    size_t idx(0);
    for (; (int) idx < rk_size; idx++) {
        boost::apply_visitor(boost::bind(values_binder, _1, idx), rkeys[idx]);
    }
    // Columns
    const GenDb::NewColVec &columns(v_columns->columns_);
    assert(columns.size() == 1);
    const GenDb::NewCol &column(columns[0]);
    assert(column.cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL);
    // Column Names
    const GenDb::DbDataValueVec &cnames(*column.name.get());
    int cn_size(cnames.size());
    for (int i = 0; i < cn_size; i++, idx++) {
        boost::apply_visitor(boost::bind(values_binder, _1, idx), cnames[i]);
    }
    // Column Values
    const GenDb::DbDataValueVec &cvalues(*column.value.get());
    if (cvalues.size() > 0) {
        boost::apply_visitor(boost::bind(values_binder, _1, idx++),
            cvalues[0]);
    }
    CassError rc(cci->CassStatementBindInt32(statement, idx++,
        (cass_int32_t)column.ttl));
    assert(rc == CASS_OK);
    return true;
}

static std::string CassSelectFromTableInternal(const std::string &table,
    const std::vector<GenDb::DbDataValueVec> &rkeys,
    const GenDb::ColumnNameRange &ck_range,
    const GenDb::FieldNamesToReadVec &read_vec) {
    std::ostringstream query;
    // Table
    if (read_vec.empty()) {
        query << "SELECT * FROM " << table;
    } else {
        query << "SELECT ";
        for (GenDb::FieldNamesToReadVec::const_iterator it = read_vec.begin();
             it != read_vec.end(); it++) {
            query << it->get<0>() << ",";
            bool read_timestamp = it->get<3>();
            if (read_timestamp) {
                query << "WRITETIME(" << it->get<0>() << "),";
            }
        }
        query.seekp(-1, query.cur);
        query << " FROM " << table;
    }
    if (rkeys.size() == 1) {
        GenDb::DbDataValueVec rkey = rkeys[0];
        int rk_size(rkey.size());
        CassQueryPrinter cprinter(query);
        for (int i = 0; i < rk_size; i++) {
            if (i) {
                int key_num(i + 1);
                query << " AND key" << key_num << "=";
            } else {
                query << " WHERE key=";
            }
            boost::apply_visitor(cprinter, rkey[i]);
        }

    } else if (rkeys.size() > 1) {
        query << " WHERE key IN (";
        BOOST_FOREACH(GenDb::DbDataValueVec rkey, rkeys) {
            int rk_size(rkey.size());
            assert(rk_size == 1);
            CassQueryPrinter cprinter(query);
            boost::apply_visitor(cprinter, rkey[0]);
            query << ",";
        }
        query.seekp(-1, query.cur);
        query << ")";
    }
    if (!ck_range.IsEmpty()) {
        if (!ck_range.start_.empty()) {
            int ck_start_size(ck_range.start_.size());
            std::ostringstream start_ss;
            start_ss << " " << GenDb::Op::ToString(ck_range.start_op_) << " (";
            CassQueryPrinter start_vprinter(start_ss);
            query << " AND (";
            for (int i = 0; i < ck_start_size; i++) {
                if (i) {
                    query << ", ";
                    start_ss << ", ";
                }
                int cnum(i + 1);
                query << "column" << cnum;
                boost::apply_visitor(start_vprinter, ck_range.start_[i]);
            }
            query << ")";
            start_ss << ")";
            query << start_ss.str();
        }
        if (!ck_range.finish_.empty()) {
            int ck_finish_size(ck_range.finish_.size());
            std::ostringstream finish_ss;
            finish_ss << " " << GenDb::Op::ToString(ck_range.finish_op_) <<
                " (";
            CassQueryPrinter finish_vprinter(finish_ss);
            query << " AND (";
            for (int i = 0; i < ck_finish_size; i++) {
                if (i) {
                    query << ", ";
                    finish_ss << ", ";
                }
                int cnum(i + 1);
                query << "column" << cnum;
                boost::apply_visitor(finish_vprinter, ck_range.finish_[i]);
            }
            query << ")";
            finish_ss << ")";
            query << finish_ss.str();
        }
        if (ck_range.count_) {
            query << " LIMIT " << ck_range.count_;
        }
    }
    return query.str();
}

std::string PartitionKey2CassSelectFromTable(const std::string &table,
    const GenDb::DbDataValueVec &rkeys) {
    std::vector<GenDb::DbDataValueVec> rkey_vec;
    rkey_vec.push_back(rkeys);
    return CassSelectFromTableInternal(table, rkey_vec, GenDb::ColumnNameRange(),
                                       GenDb::FieldNamesToReadVec());
}

std::string PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
    const std::string &table, const GenDb::DbDataValueVec &rkeys,
    const GenDb::ColumnNameRange &ck_range,
    const GenDb::FieldNamesToReadVec &read_vec) {
    std::vector<GenDb::DbDataValueVec> rkey_vec;
    rkey_vec.push_back(rkeys);
    return CassSelectFromTableInternal(table, rkey_vec, ck_range, read_vec);
}

std::string PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
    const std::string &table, const std::vector<GenDb::DbDataValueVec> &rkeys,
    const GenDb::ColumnNameRange &ck_range,
    const GenDb::FieldNamesToReadVec &read_vec) {
    return CassSelectFromTableInternal(table, rkeys, ck_range, read_vec);
}

std::string CassSelectFromTable(const std::string &table) {
    std::vector<GenDb::DbDataValueVec> rkey_vec;
    return CassSelectFromTableInternal(table, rkey_vec,
        GenDb::ColumnNameRange(), GenDb::FieldNamesToReadVec());
}

static GenDb::DbDataValue CassValue2DbDataValue(
    interface::CassLibrary *cci, const CassValue *cvalue) {
    CassValueType cvtype(cci->GetCassValueType(cvalue));
    switch (cvtype) {
      case CASS_VALUE_TYPE_ASCII:
      case CASS_VALUE_TYPE_VARCHAR:
      case CASS_VALUE_TYPE_TEXT: {
        CassString ctstring;
        CassError rc(cci->CassValueGetString(cvalue, &ctstring.data,
            &ctstring.length));
        assert(rc == CASS_OK);
        return std::string(ctstring.data, ctstring.length);
      }
      case CASS_VALUE_TYPE_UUID: {
        CassUuid ctuuid;
        CassError rc(cci->CassValueGetUuid(cvalue, &ctuuid));
        assert(rc == CASS_OK);
        boost::uuids::uuid u;
        encode_uuid((char *)&u, ctuuid);
        return u;
      }
      case CASS_VALUE_TYPE_DOUBLE: {
        cass_double_t ctdouble;
        CassError rc(cci->CassValueGetDouble(cvalue, &ctdouble));
        assert(rc == CASS_OK);
        return (double)ctdouble;
      }
      case CASS_VALUE_TYPE_TINY_INT: {
        cass_int8_t ct8;
        CassError rc(cci->CassValueGetInt8(cvalue, &ct8));
        assert(rc == CASS_OK);
        return (uint8_t)ct8;
      }
      case CASS_VALUE_TYPE_SMALL_INT: {
        cass_int16_t ct16;
        CassError rc(cci->CassValueGetInt16(cvalue, &ct16));
        assert(rc == CASS_OK);
        return (uint16_t)ct16;
      }
      case CASS_VALUE_TYPE_INT: {
        cass_int32_t ct32;
        CassError rc(cci->CassValueGetInt32(cvalue, &ct32));
        assert(rc == CASS_OK);
        return (uint32_t)ct32;
      }
      case CASS_VALUE_TYPE_BIGINT: {
        cass_int64_t ct64;
        CassError rc(cci->CassValueGetInt64(cvalue, &ct64));
        assert(rc == CASS_OK);
        return (uint64_t)ct64;
      }
      case CASS_VALUE_TYPE_INET: {
        CassInet ctinet;
        CassError rc(cci->CassValueGetInet(cvalue, &ctinet));
        assert(rc == CASS_OK);
        IpAddress ipaddr;
        if (ctinet.address_length == CASS_INET_V4_LENGTH) {
            Ip4Address::bytes_type ipv4;
            memcpy(ipv4.c_array(), ctinet.address, CASS_INET_V4_LENGTH);
            ipaddr = Ip4Address(ipv4);
        } else if (ctinet.address_length == CASS_INET_V6_LENGTH) {
            Ip6Address::bytes_type ipv6;
            memcpy(ipv6.c_array(), ctinet.address, CASS_INET_V6_LENGTH);
            ipaddr = Ip6Address(ipv6);
        } else {
            assert(0);
        }
        return ipaddr;
      }
      case CASS_VALUE_TYPE_BLOB: {
        const cass_byte_t *bytes(NULL);
        size_t size(0);
        CassError rc(cci->CassValueGetBytes(cvalue, &bytes, &size));
        assert(rc == CASS_OK);
        return GenDb::Blob(bytes, size);
      }
      case CASS_VALUE_TYPE_UNKNOWN: {
        // null type
        return GenDb::DbDataValue();
      }
      default: {
        CQLIF_ERR_TRACE("Unhandled CassValueType: " << cvtype);
        assert(false && "Unhandled value type");
        return GenDb::DbDataValue();
      }
    }
}

static bool PrepareSync(interface::CassLibrary *cci,
    CassSession *session, const char* query,
    CassPreparedPtr *prepared) {
    CQLIF_DEBUG_TRACE( "PrepareSync: " << query);
    CassFuturePtr future(cci->CassSessionPrepare(session, query), cci);
    cci->CassFutureWait(future.get());

    CassError rc(cci->CassFutureErrorCode(future.get()));
    if (rc != CASS_OK) {
        CassString err;
        cci->CassFutureErrorMessage(future.get(), &err.data, &err.length);
        CQLIF_ERR_TRACE("PrepareSync: " << query << " FAILED: " << err.data);
    } else {
        *prepared = CassPreparedPtr(cci->CassFutureGetPrepared(future.get()),
            cci);
    }
    return rc == CASS_OK;
}

static bool ExecuteQuerySyncInternal(interface::CassLibrary *cci,
    CassSession *session,
    CassStatement *qstatement, CassResultPtr *result,
    CassConsistency consistency) {
    cci->CassStatementSetConsistency(qstatement, consistency);
    CassFuturePtr future(cci->CassSessionExecute(session, qstatement), cci);
    cci->CassFutureWait(future.get());

    CassError rc(cci->CassFutureErrorCode(future.get()));
    if (rc != CASS_OK) {
        CassString err;
        cci->CassFutureErrorMessage(future.get(), &err.data, &err.length);
        CQLIF_ERR_TRACE("SyncQuery: FAILED: " << err.data);
    } else {
        if (result) {
            *result = CassResultPtr(cci->CassFutureGetResult(future.get()),
                cci);
        }
    }
    return rc == CASS_OK;
}

static bool ExecuteQuerySync(interface::CassLibrary *cci,
    CassSession *session, const char *query, CassConsistency consistency) {
    CQLIF_DEBUG_TRACE( "SyncQuery: " << query);
    CassStatementPtr statement(cci->CassStatementNew(query, 0), cci);
    return ExecuteQuerySyncInternal(cci, session, statement.get(), NULL,
        consistency);
}

static bool ExecuteQueryResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    CassResultPtr *result, CassConsistency consistency) {
    CQLIF_DEBUG_TRACE( "SyncQuery: " << query);
    CassStatementPtr statement(cci->CassStatementNew(query, 0), cci);
    return ExecuteQuerySyncInternal(cci, session, statement.get(), result,
        consistency);
}

static bool ExecuteQueryStatementSync(interface::CassLibrary *cci,
    CassSession *session, CassStatement *statement,
    CassConsistency consistency) {
    return ExecuteQuerySyncInternal(cci, session, statement, NULL,
        consistency);
}

static GenDb::DbOpResult::type CassError2DbOpResult(CassError rc) {
    switch (rc) {
      case CASS_OK:
        return GenDb::DbOpResult::OK;
      case CASS_ERROR_LIB_NO_HOSTS_AVAILABLE:
      case CASS_ERROR_LIB_REQUEST_QUEUE_FULL:
      case CASS_ERROR_LIB_NO_AVAILABLE_IO_THREAD:
        return GenDb::DbOpResult::BACK_PRESSURE;
      default:
        return GenDb::DbOpResult::ERROR;
    }
}

static void DynamicCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, const GenDb::FieldNamesToReadVec &read_vec,
    GenDb::NewColVec *v_columns) {
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        GenDb::DbDataValueVec *cnames(new GenDb::DbDataValueVec);
        GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec);
        GenDb::DbDataValueVec *timestamps(new GenDb::DbDataValueVec);
        int i = 0;
        for (GenDb::FieldNamesToReadVec::const_iterator it = read_vec.begin();
             it != read_vec.end(); it++) {
            bool row_key = it->get<1>();
            bool row_column = it->get<2>();
            bool read_timestamp = it->get<3>();
            if (row_key) {
                i++;
                continue;
            }
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            if (row_column) {
                cnames->push_back(db_value);
            } else {
                values->push_back(db_value);
                if (read_timestamp) {
                    i++;
                    const CassValue *ctimestamp(cci->CassRowGetColumn(row, i));
                    assert(ctimestamp);
                    GenDb::DbDataValue time_value(CassValue2DbDataValue(cci, ctimestamp));
                    timestamps->push_back(time_value);
                }
            }
            i++;
        }
        GenDb::NewCol *column(new GenDb::NewCol(cnames, values, 0, timestamps));
        v_columns->push_back(column);
    }
}

static void DynamicCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, const GenDb::FieldNamesToReadVec &read_vec,
    GenDb::ColListVec *v_col_list) {
    std::auto_ptr<GenDb::ColList> col_list;
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        GenDb::DbDataValueVec rkey;
        GenDb::DbDataValueVec *cnames(new GenDb::DbDataValueVec);
        GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec);
        GenDb::DbDataValueVec *timestamps(new GenDb::DbDataValueVec);
        int i = 0;
        for (GenDb::FieldNamesToReadVec::const_iterator it = read_vec.begin();
             it != read_vec.end(); it++) {
            bool row_key = it->get<1>();
            bool row_column = it->get<2>();
            bool read_timestamp = it->get<3>();
            if (row_key) {
                // Partiiton key
                const CassValue *cvalue(cci->CassRowGetColumn(row, i));
                assert(cvalue);
                GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
                rkey.push_back(db_value);
                i++;
                continue;
            }
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            if (row_column) {
                cnames->push_back(db_value);
            } else {
                values->push_back(db_value);
                if (read_timestamp) {
                    i++;
                    const CassValue *ctimestamp(cci->CassRowGetColumn(row, i));
                    assert(ctimestamp);
                    GenDb::DbDataValue time_value(CassValue2DbDataValue(cci, ctimestamp));
                    timestamps->push_back(time_value);
                }
            }
            i++;
        }
        GenDb::NewCol *column(new GenDb::NewCol(cnames, values, 0, timestamps));
        // Do we need a new ColList?
        if (!col_list.get()) {
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        if (rkey != col_list->rowkey_) {
            v_col_list->push_back(col_list.release());
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        GenDb::NewColVec *v_columns(&col_list->columns_);
        v_columns->push_back(column);
    }
    if (col_list.get()) {
        v_col_list->push_back(col_list.release());
    }
}

static void DynamicCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, size_t rk_count,
    size_t ck_count, GenDb::NewColVec *v_columns) {
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        // Iterate over columns
        size_t ccount(cci->CassResultColumnCount(result->get()));
        // Clustering key
        GenDb::DbDataValueVec *cnames(new GenDb::DbDataValueVec);
        for (size_t i = rk_count; i < rk_count + ck_count; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            cnames->push_back(db_value);
        }
        // Values
        GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec);
        for (size_t i = rk_count + ck_count; i < ccount; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            values->push_back(db_value);
        }
        GenDb::NewCol *column(new GenDb::NewCol(cnames, values, 0));
        v_columns->push_back(column);
    }
}

void DynamicCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, size_t rk_count,
    size_t ck_count, GenDb::ColListVec *v_col_list) {
    std::auto_ptr<GenDb::ColList> col_list;
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        // Iterate over columns
        size_t ccount(cci->CassResultColumnCount(result->get()));
        // Partiiton key
        GenDb::DbDataValueVec rkey;
        for (size_t i = 0; i < rk_count; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            rkey.push_back(db_value);
        }
        // Clustering key
        GenDb::DbDataValueVec *cnames(new GenDb::DbDataValueVec);
        for (size_t i = rk_count; i < rk_count + ck_count; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            cnames->push_back(db_value);
        }
        // Values
        GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec);
        for (size_t i = rk_count + ck_count; i < ccount; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            values->push_back(db_value);
        }
        GenDb::NewCol *column(new GenDb::NewCol(cnames, values, 0));
        // Do we need a new ColList?
        if (!col_list.get()) {
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        if (rkey != col_list->rowkey_) {
            v_col_list->push_back(col_list.release());
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        GenDb::NewColVec *v_columns(&col_list->columns_);
        v_columns->push_back(column);
    }
    if (col_list.get()) {
        v_col_list->push_back(col_list.release());
    }
}

static void StaticCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, GenDb::NewColVec *v_columns) {
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        // Iterate over columns
        size_t ccount(cci->CassResultColumnCount(result->get()));
        for (size_t i = 0; i < ccount; i++) {
            CassString cname;
            CassError rc(cci->CassResultColumnName(result->get(), i,
                &cname.data, &cname.length));
            assert(rc == CASS_OK);
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            if (db_value.which() == GenDb::DB_VALUE_BLANK) {
                continue;
            }
            GenDb::NewCol *column(new GenDb::NewCol(
                std::string(cname.data, cname.length), db_value, 0));
            v_columns->push_back(column);
        }
    }
}

void StaticCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, size_t rk_count, GenDb::ColListVec *v_col_list) {
    std::auto_ptr<GenDb::ColList> col_list;
    // Row iterator
    CassIteratorPtr riterator(cci->CassIteratorFromResult(result->get()), cci);
    while (cci->CassIteratorNext(riterator.get())) {
        const CassRow *row(cci->CassIteratorGetRow(riterator.get()));
        // Iterate over columns
        size_t ccount(cci->CassResultColumnCount(result->get()));
        // Partiiton key
        GenDb::DbDataValueVec rkey;
        for (size_t i = 0; i < rk_count; i++) {
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            rkey.push_back(db_value);
        }
        // Do we need a new ColList?
        if (!col_list.get()) {
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        if (rkey != col_list->rowkey_) {
            v_col_list->push_back(col_list.release());
            col_list.reset(new GenDb::ColList);
            col_list->rowkey_ = rkey;
        }
        GenDb::NewColVec *v_columns(&col_list->columns_);
        for (size_t i = 0; i < ccount; i++) {
            CassString cname;
            CassError rc(cci->CassResultColumnName(result->get(), i,
                &cname.data, &cname.length));
            assert(rc == CASS_OK);
            const CassValue *cvalue(cci->CassRowGetColumn(row, i));
            assert(cvalue);
            GenDb::DbDataValue db_value(CassValue2DbDataValue(cci, cvalue));
            if (db_value.which() == GenDb::DB_VALUE_BLANK) {
                continue;
            }
            GenDb::NewCol *column(new GenDb::NewCol(
                std::string(cname.data, cname.length), db_value, 0));
            v_columns->push_back(column);
        }
    }
    if (col_list.get()) {
        v_col_list->push_back(col_list.release());
    }
}

static void OnExecuteQueryAsync(CassFuture *future, void *data) {
    assert(data);
    std::auto_ptr<CassAsyncQueryContext> ctx(
        boost::reinterpret_pointer_cast<CassAsyncQueryContext>(data));
    interface::CassLibrary *cci(ctx->cci_);
    CassError rc(cci->CassFutureErrorCode(future));
    GenDb::DbOpResult::type db_rc(CassError2DbOpResult(rc));
    if (rc != CASS_OK) {
        CassString err;
        cci->CassFutureErrorMessage(future, &err.data, &err.length);
        CQLIF_ERR_TRACE("AsyncQuery: " << ctx->query_id_ << " FAILED: "
            << err.data);
        ctx->cb_(db_rc, std::auto_ptr<GenDb::ColList>());
        return;
    }
    if (ctx->result_ctx_) {
        CassQueryResultContext *rctx(ctx->result_ctx_.get());
        CassResultPtr result(cci->CassFutureGetResult(future), cci);
        // In case of select parse the results
        if (cci->CassResultColumnCount(result.get())) {
            std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
            col_list->cfname_ = rctx->cf_name_;
            col_list->rowkey_ = rctx->row_key_;
            if (rctx->is_dynamic_cf_) {
                DynamicCfGetResult(cci, &result, rctx->rk_count_,
                    rctx->ck_count_, &col_list->columns_);
            } else {
                StaticCfGetResult(cci, &result, &col_list->columns_);
            }
            ctx->cb_(db_rc, col_list);
            return;
       }
    }
    ctx->cb_(db_rc, std::auto_ptr<GenDb::ColList>());
}

static void ExecuteQueryAsyncInternal(interface::CassLibrary *cci,
    CassSession *session, const char *qid, CassStatement *qstatement,
    CassConsistency consistency, CassAsyncQueryCallback cb,
    CassQueryResultContext *rctx = NULL) {
    cci->CassStatementSetConsistency(qstatement, consistency);
    CassFuturePtr future(cci->CassSessionExecute(session, qstatement), cci);
    std::auto_ptr<CassAsyncQueryContext> ctx(
        new CassAsyncQueryContext(qid, cb, cci, rctx));
    cci->CassFutureSetCallback(future.get(), OnExecuteQueryAsync,
        ctx.release());
}

static void ExecuteQueryAsync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    CassConsistency consistency, CassAsyncQueryCallback cb) {
    CQLIF_DEBUG_TRACE( "AsyncQuery: " << query);
    CassStatementPtr statement(cci->CassStatementNew(query, 0), cci);
    ExecuteQueryAsyncInternal(cci, session, query, statement.get(),
        consistency, cb);
}

static void ExecuteQueryStatementAsync(interface::CassLibrary *cci,
    CassSession *session, const char *query_id, CassStatement *qstatement,
    CassConsistency consistency, CassAsyncQueryCallback cb) {
    ExecuteQueryAsyncInternal(cci, session, query_id, qstatement, consistency,
        cb);
}

static void ExecuteQueryResultAsync(interface::CassLibrary *cci,
    CassSession *session, const char *query, CassConsistency consistency,
    CassAsyncQueryCallback cb, CassQueryResultContext *rctx) {
    CassStatementPtr statement(cci->CassStatementNew(query, 0), cci);
    ExecuteQueryAsyncInternal(cci, session, query, statement.get(),
        consistency, cb, rctx);
}

static bool DynamicCfGetResultAsync(interface::CassLibrary *cci,
    CassSession *session, const char *query, CassConsistency consistency,
    impl::CassAsyncQueryCallback cb, size_t rk_count, size_t ck_count,
    const std::string &cfname, const GenDb::DbDataValueVec &row_key) {
    std::auto_ptr<CassQueryResultContext> rctx(
        new CassQueryResultContext(cfname, true, row_key,
            rk_count, ck_count));
    ExecuteQueryResultAsync(cci, session, query, consistency, cb,
        rctx.release());
    return true;
}

static bool DynamicCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    const GenDb::FieldNamesToReadVec &read_vec,
    CassConsistency consistency, GenDb::NewColVec *v_columns) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    DynamicCfGetResult(cci, &result, read_vec, v_columns);
    return success;
}

static bool DynamicCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    const GenDb::FieldNamesToReadVec &read_vec,
    CassConsistency consistency, GenDb::ColListVec *v_columns) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    DynamicCfGetResult(cci, &result, read_vec, v_columns);
    return success;
}

static bool DynamicCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    size_t rk_count, size_t ck_count, CassConsistency consistency,
    GenDb::NewColVec *v_columns) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    DynamicCfGetResult(cci, &result, rk_count, ck_count, v_columns);
    return success;
}

static bool DynamicCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    size_t rk_count, size_t ck_count, CassConsistency consistency,
    GenDb::ColListVec *v_col_list) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    DynamicCfGetResult(cci, &result, rk_count, ck_count, v_col_list);
    return success;
}

static bool StaticCfGetResultAsync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    CassConsistency consistency, impl::CassAsyncQueryCallback cb,
    const std::string& cfname, const GenDb::DbDataValueVec &row_key) {
    std::auto_ptr<CassQueryResultContext> rctx(
        new CassQueryResultContext(cfname, false, row_key));
    ExecuteQueryResultAsync(cci, session, query, consistency, cb,
        rctx.release());
    return true;
}

static bool StaticCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query,
    CassConsistency consistency, GenDb::NewColVec *v_columns) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    StaticCfGetResult(cci, &result, v_columns);
    return success;
}

static bool StaticCfGetResultSync(interface::CassLibrary *cci,
    CassSession *session, const char *query, size_t rk_count,
    CassConsistency consistency, GenDb::ColListVec *v_col_list) {
    CassResultPtr result(NULL, cci);
    bool success(ExecuteQueryResultSync(cci, session, query, &result,
        consistency));
    if (!success) {
        return success;
    }
    StaticCfGetResult(cci, &result, rk_count, v_col_list);
    return success;
}

static bool SyncFutureWait(interface::CassLibrary *cci,
    CassFuture *future) {
    cci->CassFutureWait(future);
    CassError rc(cci->CassFutureErrorCode(future));
    if (rc != CASS_OK) {
        CassString err;
        cci->CassFutureErrorMessage(future, &err.data, &err.length);
        CQLIF_ERR_TRACE("SyncWait: FAILED: " << err.data);
    }
    return rc == CASS_OK;
}

static const CassTableMeta * GetCassTableMeta(
    interface::CassLibrary *cci, const CassSchemaMeta *schema_meta,
    const std::string &keyspace, const std::string &table, bool log_error) {
    const CassKeyspaceMeta *keyspace_meta(
        cci->CassSchemaMetaKeyspaceByName(schema_meta, keyspace.c_str()));
    if (keyspace_meta == NULL) {
        if (log_error) {
            CQLIF_ERR_TRACE("No keyspace schema: Keyspace: " << keyspace <<
                ", Table: " << table);
        }
        return NULL;
    }
    std::string table_lower(table);
    boost::algorithm::to_lower(table_lower);
    const CassTableMeta *table_meta(
        cci->CassKeyspaceMetaTableByName(keyspace_meta, table_lower.c_str()));
    if (table_meta == NULL) {
        if (log_error) {
            CQLIF_ERR_TRACE("No table schema: Keyspace: " << keyspace <<
                ", Table: " << table_lower);
        }
        return NULL;
    }
    return table_meta;
}

static bool IsCassTableMetaPresent(interface::CassLibrary *cci,
    CassSession *session,
    const std::string &keyspace, const std::string &table) {
    impl::CassSchemaMetaPtr schema_meta(cci->CassSessionGetSchemaMeta(
        session), cci);
    if (schema_meta.get() == NULL) {
        CQLIF_DEBUG_TRACE( "No schema meta: Keyspace: " << keyspace <<
            ", Table: " << table);
        return false;
    }
    bool log_error(false);
    const CassTableMeta *table_meta(impl::GetCassTableMeta(cci,
        schema_meta.get(), keyspace, table, log_error));
    if (table_meta == NULL) {
        return false;
    }
    return true;
}

static bool GetCassTableClusteringKeyCount(
    interface::CassLibrary *cci,
    CassSession *session, const std::string &keyspace,
    const std::string &table, size_t *ck_count) {
    impl::CassSchemaMetaPtr schema_meta(cci->CassSessionGetSchemaMeta(
        session), cci);
    if (schema_meta.get() == NULL) {
        CQLIF_ERR_TRACE("No schema meta: Keyspace: " << keyspace <<
            ", Table: " << table);
        return false;
    }
    bool log_error(true);
    const CassTableMeta *table_meta(impl::GetCassTableMeta(cci,
        schema_meta.get(), keyspace, table, log_error));
    if (table_meta == NULL) {
        return false;
    }
    *ck_count = cci->CassTableMetaClusteringKeyCount(table_meta);
    return true;
}

static bool GetCassTablePartitionKeyCount(
    interface::CassLibrary *cci, CassSession *session,
    const std::string &keyspace, const std::string &table, size_t *rk_count) {
    impl::CassSchemaMetaPtr schema_meta(cci->CassSessionGetSchemaMeta(
        session), cci);
    if (schema_meta.get() == NULL) {
        CQLIF_ERR_TRACE("No schema meta: Keyspace: " << keyspace <<
            ", Table: " << table);
        return false;
    }
    bool log_error(true);
    const CassTableMeta *table_meta(impl::GetCassTableMeta(cci,
        schema_meta.get(), keyspace, table, log_error));
    if (table_meta == NULL) {
        return false;
    }
    *rk_count = cci->CassTableMetaPartitionKeyCount(table_meta);
    return true;
}

static log4cplus::LogLevel Cass2log4Level(CassLogLevel clevel) {
    switch (clevel) {
      case CASS_LOG_DISABLED:
        return log4cplus::OFF_LOG_LEVEL;
      case CASS_LOG_CRITICAL:
        return log4cplus::FATAL_LOG_LEVEL;
      case CASS_LOG_ERROR:
        return log4cplus::ERROR_LOG_LEVEL;
      case CASS_LOG_WARN:
        return log4cplus::WARN_LOG_LEVEL;
      case CASS_LOG_INFO:
        return log4cplus::INFO_LOG_LEVEL;
      case CASS_LOG_DEBUG:
        return log4cplus::DEBUG_LOG_LEVEL;
      case CASS_LOG_TRACE:
        return log4cplus::TRACE_LOG_LEVEL;
      default:
        return log4cplus::ALL_LOG_LEVEL;
    }
}

static CassLogLevel Log4Level2CassLogLevel(log4cplus::LogLevel level) {
    switch (level) {
      case log4cplus::OFF_LOG_LEVEL:
        return CASS_LOG_DISABLED;
      case log4cplus::FATAL_LOG_LEVEL:
        return CASS_LOG_CRITICAL;
      case log4cplus::ERROR_LOG_LEVEL:
        return CASS_LOG_ERROR;
      case log4cplus::WARN_LOG_LEVEL:
        return CASS_LOG_WARN;
      case log4cplus::INFO_LOG_LEVEL:
        return CASS_LOG_INFO;
      case log4cplus::DEBUG_LOG_LEVEL:
        return CASS_LOG_DEBUG;
      case log4cplus::TRACE_LOG_LEVEL:
        return CASS_LOG_TRACE;
      default:
        assert(false && "Invalid Log4Level");
        return CASS_LOG_DISABLED;
    }
}

static void CassLibraryLog(const CassLogMessage* message, void *data) {
    if (LoggingDisabled()) {
        return;
    }
    log4cplus::LogLevel log4level(Cass2log4Level(message->severity));
    std::stringstream buf;
    buf << "CassLibrary: " << message->file << ":" << message->line <<
            " " << message->function << "] " << message->message;
    CASS_LIB_TRACE(log4level, buf.str());
}


class WorkerTask : public Task {
 public:
    typedef boost::function<void(void)> FunctionPtr;
    WorkerTask(FunctionPtr func, int task_id, int task_instance) :
        Task(task_id, task_instance),
        func_(func) {
    }
    bool Run() {
        func_();
        return true;
    }
    std::string Description() const {
        return "cass::cql::impl::WorkerTask";
    }
 private:
    FunctionPtr func_;
};

}  // namespace impl

//
// CqlIfImpl
//
CqlIfImpl::CqlIfImpl(EventManager *evm,
                     const std::vector<std::string> &cassandra_ips,
                     int cassandra_port,
                     const std::string &cassandra_user,
                     const std::string &cassandra_password,
                     interface::CassLibrary *cci) :
    evm_(evm),
    cci_(cci),
    cluster_(cci_->CassClusterNew(), cci_),
    session_(cci_->CassSessionNew(), cci_),
    reconnect_timer_(TimerManager::CreateTimer(*evm->io_service(),
        "CqlIfImpl Reconnect Timer",
        TaskScheduler::GetInstance()->GetTaskId(kTaskName),
        kTaskInstance)),
    connect_cb_(NULL),
    disconnect_cb_(NULL),
    keyspace_(),
    io_thread_count_(2) {
    // Set session state to INIT
    session_state_ = SessionState::INIT;
    // Set contact points and port
    std::string contact_points(boost::algorithm::join(cassandra_ips, ","));
    cci_->CassClusterSetContactPoints(cluster_.get(), contact_points.c_str());
    cci_->CassClusterSetPort(cluster_.get(), cassandra_port);
    // Set credentials for plain text authentication
    if (!cassandra_user.empty() && !cassandra_password.empty()) {
        cci_->CassClusterSetCredentials(cluster_.get(), cassandra_user.c_str(),
            cassandra_password.c_str());
    }
    // Set number of IO threads to half the number of cores
    cci_->CassClusterSetNumThreadsIo(cluster_.get(), io_thread_count_);
    cci_->CassClusterSetPendingRequestsHighWaterMark(cluster_.get(), 10000);
    cci_->CassClusterSetPendingRequestsLowWaterMark(cluster_.get(), 5000);
    cci_->CassClusterSetWriteBytesHighWaterMark(cluster_.get(), 128000);
    cci_->CassClusterSetWriteBytesLowWaterMark(cluster_.get(), 96000);
}

CqlIfImpl::~CqlIfImpl() {
    assert(session_state_ == SessionState::INIT ||
        session_state_ == SessionState::DISCONNECTED);
    TimerManager::DeleteTimer(reconnect_timer_);
    reconnect_timer_ = NULL;
}

bool CqlIfImpl::CreateKeyspaceIfNotExistsSync(const std::string &keyspace,
    const std::string &replication_factor, CassConsistency consistency) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    char buf[512];
    int n(snprintf(buf, sizeof(buf), kQCreateKeyspaceIfNotExists,
        keyspace.c_str(), replication_factor.c_str()));
    if (n < 0 || n >= (int)sizeof(buf)) {
        CQLIF_ERR_TRACE("FAILED (" << n << "): Keyspace: " <<
            keyspace << ", RF: " << replication_factor);
        return false;
    }
    return impl::ExecuteQuerySync(cci_, session_.get(), buf, consistency);
}

bool CqlIfImpl::UseKeyspaceSync(const std::string &keyspace,
    CassConsistency consistency) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    char buf[512];
    int n(snprintf(buf, sizeof(buf), kQUseKeyspace, keyspace.c_str()));
    if (n < 0 || n >= (int)sizeof(buf)) {
        CQLIF_ERR_TRACE("FAILED (" << n << "): Keyspace: " <<
            keyspace);
        return false;
    }
    bool success(impl::ExecuteQuerySync(cci_, session_.get(), buf,
        consistency));
    if (!success) {
        return false;
    }
    // Update keyspace
    keyspace_ = keyspace;
    return success;
}

bool CqlIfImpl::CreateTableIfNotExistsSync(const GenDb::NewCf &cf,
    const std::string &compaction_strategy, CassConsistency consistency) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    // There are two types of tables - Static (SQL) and Dynamic (NOSQL)
    // column family. Static column family has more or less fixed rows,
    // and dynamic column family has wide rows
    std::string query;
    switch (cf.cftype_) {
      case GenDb::NewCf::COLUMN_FAMILY_SQL:
        query = impl::StaticCf2CassCreateTableIfNotExists(cf,
            compaction_strategy);
        break;
      case GenDb::NewCf::COLUMN_FAMILY_NOSQL:
        query = impl::DynamicCf2CassCreateTableIfNotExists(cf,
            compaction_strategy);
        break;
      default:
        return false;
    }
    return impl::ExecuteQuerySync(cci_, session_.get(), query.c_str(),
        consistency);
}

bool CqlIfImpl::LocatePrepareInsertIntoTable(const GenDb::NewCf &cf) {
    const std::string &table_name(cf.cfname_);
    impl::CassPreparedPtr prepared(NULL, cci_);
    // Check if the prepared statement exists
    if (GetPrepareInsertIntoTable(table_name, &prepared)) {
        return true;
    }
    bool success(PrepareInsertIntoTableSync(cf, &prepared));
    if (!success) {
        return success;
    }
    // Store the prepared statement into the map
    tbb::mutex::scoped_lock lock(map_mutex_);
    success = (insert_prepared_map_.insert(
        std::make_pair(table_name, prepared))).second;
    assert(success);
    return success;
}

bool CqlIfImpl::GetPrepareInsertIntoTable(const std::string &table_name,
    impl::CassPreparedPtr *prepared) const {
    tbb::mutex::scoped_lock lock(map_mutex_);
    CassPreparedMapType::const_iterator it(
        insert_prepared_map_.find(table_name));
    if (it == insert_prepared_map_.end()) {
        return false;
    }
    *prepared = it->second;
    return true;
}

bool CqlIfImpl::IsTablePresent(const std::string &table) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    return impl::IsCassTableMetaPresent(cci_, session_.get(), keyspace_,
        table);
}

bool CqlIfImpl::IsTableStatic(const std::string &table) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    size_t ck_count;
    assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
        keyspace_, table, &ck_count));
    return ck_count == 0;
}

bool CqlIfImpl::SelectFromTableAsync(const std::string &cfname,
    const GenDb::DbDataValueVec &rkey, CassConsistency consistency,
    impl::CassAsyncQueryCallback cb) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(impl::PartitionKey2CassSelectFromTable(cfname,rkey));
    if (IsTableStatic(cfname)) {
        return impl::StaticCfGetResultAsync(cci_, session_.get(),
            query.c_str(), consistency, cb, cfname.c_str(), rkey);
    } else {
        size_t rk_count;
        assert(impl::GetCassTablePartitionKeyCount(cci_, session_.get(),
            keyspace_, cfname, &rk_count));
        size_t ck_count;
        assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
            keyspace_, cfname, &ck_count));
        return impl::DynamicCfGetResultAsync(cci_, session_.get(),
            query.c_str(), consistency, cb, rk_count, ck_count,
            cfname, rkey);
   }
}

bool CqlIfImpl::SelectFromTableClusteringKeyRangeAsync(
    const std::string &cfname, const GenDb::DbDataValueVec &rkey,
    const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
    impl::CassAsyncQueryCallback cb) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(
        impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(cfname,
        rkey, ck_range));
    assert(IsTableDynamic(cfname));
    size_t rk_count;
    assert(impl::GetCassTablePartitionKeyCount(cci_, session_.get(),
        keyspace_, cfname, &rk_count));
    size_t ck_count;
    assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
        keyspace_, cfname, &ck_count));
    return impl::DynamicCfGetResultAsync(cci_, session_.get(),
        query.c_str(), consistency, cb, rk_count, ck_count, cfname.c_str(),
        rkey);
}

bool CqlIfImpl::IsTableDynamic(const std::string &table) {
    return !IsTableStatic(table);
}

bool CqlIfImpl::InsertIntoTableSync(std::auto_ptr<GenDb::ColList> v_columns,
    CassConsistency consistency) {
    return InsertIntoTableInternal(v_columns, consistency, true, NULL);
}

bool CqlIfImpl::InsertIntoTableAsync(std::auto_ptr<GenDb::ColList> v_columns,
    CassConsistency consistency, impl::CassAsyncQueryCallback cb) {
    return InsertIntoTableInternal(v_columns, consistency, false, cb);
}

bool CqlIfImpl::InsertIntoTablePrepareAsync(std::auto_ptr<GenDb::ColList> v_columns,
    CassConsistency consistency, impl::CassAsyncQueryCallback cb) {
    return InsertIntoTablePrepareInternal(v_columns, consistency, false,
        cb);
}

bool CqlIfImpl::IsInsertIntoTablePrepareSupported(const std::string &table) {
    return IsTableDynamic(table);
}

bool CqlIfImpl::SelectFromTableSync(const std::string &cfname,
    const GenDb::DbDataValueVec &rkey, CassConsistency consistency,
    GenDb::NewColVec *out) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(impl::PartitionKey2CassSelectFromTable(cfname,
        rkey));
    if (IsTableStatic(cfname)) {
        return impl::StaticCfGetResultSync(cci_, session_.get(),
            query.c_str(), consistency, out);
    } else {
        size_t rk_count;
        assert(impl::GetCassTablePartitionKeyCount(cci_, session_.get(),
            keyspace_, cfname, &rk_count));
        size_t ck_count;
        assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
            keyspace_, cfname, &ck_count));
        return impl::DynamicCfGetResultSync(cci_, session_.get(),
            query.c_str(), rk_count, ck_count, consistency, out);
    }
}

bool CqlIfImpl::SelectFromTableSync(const std::string &cfname,
    CassConsistency consistency, GenDb::ColListVec *out) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(impl::CassSelectFromTable(cfname));
    size_t rk_count;
    assert(impl::GetCassTablePartitionKeyCount(cci_, session_.get(),
        keyspace_, cfname, &rk_count));
    if (IsTableStatic(cfname)) {
        return impl::StaticCfGetResultSync(cci_, session_.get(),
            query.c_str(), rk_count, consistency, out);
    } else {
        size_t ck_count;
        assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
            keyspace_, cfname, &ck_count));
        return impl::DynamicCfGetResultSync(cci_, session_.get(),
            query.c_str(), rk_count, ck_count, consistency, out);
    }
}


bool CqlIfImpl::SelectFromTableClusteringKeyRangeFieldNamesSync(const std::string &cfname,
    const GenDb::DbDataValueVec &rkey,
    const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
    const GenDb::FieldNamesToReadVec &read_vec,
    GenDb::NewColVec *out) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(
        impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(cfname,
        rkey, ck_range, read_vec));
    assert(IsTableDynamic(cfname));
    return impl::DynamicCfGetResultSync(cci_, session_.get(),
        query.c_str(), read_vec, consistency, out);
}

bool CqlIfImpl::SelectFromTableClusteringKeyRangeFieldNamesSync(const std::string &cfname,
    const std::vector<GenDb::DbDataValueVec> &rkeys,
    const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
    const GenDb::FieldNamesToReadVec &read_vec,
    GenDb::ColListVec *out) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(
        impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(cfname,
        rkeys, ck_range, read_vec));
    assert(IsTableDynamic(cfname));
    return impl::DynamicCfGetResultSync(cci_, session_.get(),
        query.c_str(), read_vec, consistency, out);
}


bool CqlIfImpl::SelectFromTableClusteringKeyRangeSync(const std::string &cfname,
    const GenDb::DbDataValueVec &rkey,
    const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
    GenDb::NewColVec *out) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query(
        impl::PartitionKeyAndClusteringKeyRange2CassSelectFromTable(cfname,
        rkey, ck_range));
    assert(IsTableDynamic(cfname));
    size_t rk_count;
    assert(impl::GetCassTablePartitionKeyCount(cci_, session_.get(),
        keyspace_, cfname, &rk_count));
    size_t ck_count;
    assert(impl::GetCassTableClusteringKeyCount(cci_, session_.get(),
        keyspace_, cfname, &ck_count));
    return impl::DynamicCfGetResultSync(cci_, session_.get(),
        query.c_str(), rk_count, ck_count, consistency, out);
}

void CqlIfImpl::ConnectAsync() {
    session_state_ = SessionState::CONNECT_PENDING;
    impl::CassFuturePtr future(cci_->CassSessionConnect(session_.get(),
        cluster_.get()), cci_);
    if (connect_cb_.empty()) {
        connect_cb_ = boost::bind(&CqlIfImpl::ConnectCallbackProcess,
            this, _1);
    }
    cci_->CassFutureSetCallback(future.get(), ConnectCallback, this);
}

bool CqlIfImpl::ConnectSync() {
    impl::CassFuturePtr future(cci_->CassSessionConnect(session_.get(),
        cluster_.get()), cci_);
    bool success(impl::SyncFutureWait(cci_, future.get()));
    if (success) {
        session_state_ = SessionState::CONNECTED;
        CQLIF_INFO_TRACE( "ConnectSync Done");
    } else {
        CQLIF_ERR_TRACE("ConnectSync FAILED");
    }
    return success;
}

void CqlIfImpl::DisconnectAsync() {
    // Close all session and pending queries
    session_state_ = SessionState::DISCONNECT_PENDING;
    impl::CassFuturePtr future(cci_->CassSessionClose(session_.get()), cci_);
    if (disconnect_cb_.empty()) {
        disconnect_cb_ = boost::bind(&CqlIfImpl::DisconnectCallbackProcess,
            this, _1);
    }
    cci_->CassFutureSetCallback(future.get(), DisconnectCallback, this);
}

bool CqlIfImpl::DisconnectSync() {
    // Close all session and pending queries
    impl::CassFuturePtr future(cci_->CassSessionClose(session_.get()), cci_);
    bool success(impl::SyncFutureWait(cci_, future.get()));
    if (success) {
        session_state_ = SessionState::DISCONNECTED;
        CQLIF_INFO_TRACE( "DisconnectSync Done");
    } else {
        CQLIF_ERR_TRACE("DisconnectSync FAILED");
    }
    return success;
}

void CqlIfImpl::GetMetrics(Metrics *metrics) const {
    CassMetrics cass_metrics;
    cci_->CassSessionGetMetrics(session_.get(), &cass_metrics);
    // Requests
    metrics->requests.min = cass_metrics.requests.min;
    metrics->requests.max = cass_metrics.requests.max;
    metrics->requests.mean = cass_metrics.requests.mean;
    metrics->requests.stddev = cass_metrics.requests.stddev;
    metrics->requests.median = cass_metrics.requests.median;
    metrics->requests.percentile_75th =
        cass_metrics.requests.percentile_75th;
    metrics->requests.percentile_95th =
        cass_metrics.requests.percentile_95th;
    metrics->requests.percentile_98th =
        cass_metrics.requests.percentile_98th;
    metrics->requests.percentile_99th =
        cass_metrics.requests.percentile_99th;
    metrics->requests.percentile_999th =
        cass_metrics.requests.percentile_999th;
    metrics->requests.mean_rate = cass_metrics.requests.mean_rate;
    metrics->requests.one_minute_rate =
        cass_metrics.requests.one_minute_rate;
    metrics->requests.five_minute_rate =
        cass_metrics.requests.five_minute_rate;
    metrics->requests.fifteen_minute_rate =
        cass_metrics.requests.fifteen_minute_rate;
    // Stats
    metrics->stats.total_connections =
        cass_metrics.stats.total_connections;
    metrics->stats.available_connections =
        cass_metrics.stats.available_connections;
    metrics->stats.exceeded_pending_requests_water_mark =
        cass_metrics.stats.exceeded_pending_requests_water_mark;
    metrics->stats.exceeded_write_bytes_water_mark =
        cass_metrics.stats.exceeded_write_bytes_water_mark;
    // Errors
    metrics->errors.connection_timeouts =
        cass_metrics.errors.connection_timeouts;
    metrics->errors.pending_request_timeouts =
        cass_metrics.errors.pending_request_timeouts;
    metrics->errors.request_timeouts =
        cass_metrics.errors.request_timeouts;
}

void CqlIfImpl::ConnectCallback(CassFuture *future, void *data) {
    CqlIfImpl *impl_ = (CqlIfImpl *)data;
    impl_->connect_cb_(future);
}

void CqlIfImpl::DisconnectCallback(CassFuture *future, void *data) {
    CqlIfImpl *impl_ = (CqlIfImpl *)data;
    impl_->disconnect_cb_(future);
}

bool CqlIfImpl::ReconnectTimerExpired() {
    ConnectAsync();
    return false;
}

void CqlIfImpl::ReconnectTimerErrorHandler(std::string error_name,
    std::string error_message) {
    CQLIF_ERR_TRACE(error_name << " " << error_message);
}

void CqlIfImpl::ConnectCallbackProcess(CassFuture *future) {
    CassError code(cci_->CassFutureErrorCode(future));
    if (code != CASS_OK) {
        impl::CassString err;
        cci_->CassFutureErrorMessage(future, &err.data, &err.length);
        CQLIF_INFO_TRACE( err.data);
        // Start a timer to reconnect
        reconnect_timer_->Start(kReconnectInterval,
            boost::bind(&CqlIfImpl::ReconnectTimerExpired, this),
            boost::bind(&CqlIfImpl::ReconnectTimerErrorHandler, this,
                _1, _2));
        return;
    }
    session_state_ = SessionState::CONNECTED;
}

void CqlIfImpl::DisconnectCallbackProcess(CassFuture *future) {
    CassError code(cci_->CassFutureErrorCode(future));
    if (code != CASS_OK) {
        impl::CassString err;
        cci_->CassFutureErrorMessage(future, &err.data, &err.length);
        CQLIF_ERR_TRACE(err.data);
    }
    session_state_ = SessionState::DISCONNECTED;
}

bool CqlIfImpl::InsertIntoTableInternal(std::auto_ptr<GenDb::ColList> v_columns,
    CassConsistency consistency, bool sync,
    impl::CassAsyncQueryCallback cb) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query;
    if (IsTableStatic(v_columns->cfname_)) {
        query = impl::StaticCf2CassInsertIntoTable(v_columns.get());
    } else {
        query = impl::DynamicCf2CassInsertIntoTable(v_columns.get());
    }
    if (sync) {
        return impl::ExecuteQuerySync(cci_, session_.get(), query.c_str(),
            consistency);
    } else {
        impl::ExecuteQueryAsync(cci_, session_.get(), query.c_str(),
            consistency, cb);
        return true;
    }
}

bool CqlIfImpl::PrepareInsertIntoTableSync(const GenDb::NewCf &cf,
    impl::CassPreparedPtr *prepared) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    std::string query;
    switch (cf.cftype_) {
      case GenDb::NewCf::COLUMN_FAMILY_SQL:
        query = impl::StaticCf2CassPrepareInsertIntoTable(cf);
        break;
      case GenDb::NewCf::COLUMN_FAMILY_NOSQL:
        query = impl::DynamicCf2CassPrepareInsertIntoTable(cf);
        break;
      default:
        return false;
    }
    return impl::PrepareSync(cci_, session_.get(), query.c_str(),
        prepared);
}

bool CqlIfImpl::InsertIntoTablePrepareInternal(
    std::auto_ptr<GenDb::ColList> v_columns,
    CassConsistency consistency, bool sync,
    impl::CassAsyncQueryCallback cb) {
    if (session_state_ != SessionState::CONNECTED) {
        return false;
    }
    impl::CassPreparedPtr prepared(NULL, cci_);
    bool success(GetPrepareInsertIntoTable(v_columns->cfname_, &prepared));
    if (!success) {
        CQLIF_ERR_TRACE("CassPrepared statement NOT found: " <<
            v_columns->cfname_);
        return false;
    }
    impl::CassStatementPtr qstatement(cci_->CassPreparedBind(prepared.get()),
        cci_);
    if (IsTableStatic(v_columns->cfname_)) {
        success = impl::StaticCf2CassPrepareBind(cci_, qstatement.get(),
            v_columns.get());
    } else {
        success = impl::DynamicCf2CassPrepareBind(cci_, qstatement.get(),
            v_columns.get());
    }
    if (!success) {
        return false;
    }
    if (sync) {
        return impl::ExecuteQueryStatementSync(cci_, session_.get(),
            qstatement.get(), consistency);
    } else {
        std::string qid("Prepare: " + v_columns->cfname_);
        impl::ExecuteQueryStatementAsync(cci_, session_.get(), qid.c_str(),
            qstatement.get(), consistency, cb);
        return true;
    }
}

const char * CqlIfImpl::kQCreateKeyspaceIfNotExists(
    "CREATE KEYSPACE IF NOT EXISTS \"%s\" WITH "
    "replication = { 'class' : 'SimpleStrategy', 'replication_factor' : %s }");
const char * CqlIfImpl::kQUseKeyspace("USE \"%s\"");
const char * CqlIfImpl::kTaskName("CqlIfImpl::Task");

//
// CqlIf
//
CqlIf::CqlIf(EventManager *evm,
             const std::vector<std::string> &cassandra_ips,
             int cassandra_port,
             const std::string &cassandra_user,
             const std::string &cassandra_password) :
    cci_(new interface::CassDatastaxLibrary),
    impl_(new CqlIfImpl(evm, cassandra_ips, cassandra_port,
        cassandra_user, cassandra_password, cci_.get())),
    use_prepared_for_insert_(true) {
    // Setup library logging
    cci_->CassLogSetLevel(impl::Log4Level2CassLogLevel(
        log4cplus::Logger::getRoot().getLogLevel()));
    cci_->CassLogSetCallback(impl::CassLibraryLog, NULL);
    initialized_ = false;
    BOOST_FOREACH(const std::string &cassandra_ip, cassandra_ips) {
        boost::system::error_code ec;
        boost::asio::ip::address cassandra_addr(
            boost::asio::ip::address::from_string(cassandra_ip, ec));
        GenDb::Endpoint endpoint(cassandra_addr, cassandra_port);
        endpoints_.push_back(endpoint);
    }
}

CqlIf::CqlIf() : impl_(NULL) {
}

CqlIf::~CqlIf() {
}

// Init/Uninit
bool CqlIf::Db_Init() {
    return impl_->ConnectSync();
}

void CqlIf::Db_Uninit() {
    Db_UninitUnlocked();
}

void CqlIf::Db_UninitUnlocked() {
    impl_->DisconnectSync();
}

void CqlIf::Db_SetInitDone(bool init_done) {
    initialized_ = init_done;
}

// Tablespace
bool CqlIf::Db_AddSetTablespace(const std::string &tablespace,
    const std::string &replication_factor) {
    bool success(impl_->CreateKeyspaceIfNotExistsSync(tablespace,
        replication_factor, CASS_CONSISTENCY_QUORUM));
    if (!success) {
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_TABLESPACE);
        return success;
    }
    success = impl_->UseKeyspaceSync(tablespace, CASS_CONSISTENCY_ONE);
    if (!success) {
        IncrementErrors(GenDb::IfErrors::ERR_READ_TABLESPACE);
        return success;
    }
    return success;
}

bool CqlIf::Db_SetTablespace(const std::string &tablespace) {
    bool success(impl_->UseKeyspaceSync(tablespace, CASS_CONSISTENCY_ONE));
    if (!success) {
        IncrementErrors(GenDb::IfErrors::ERR_READ_TABLESPACE);
        return success;
    }
    return success;
}

// Column family
bool CqlIf::Db_AddColumnfamily(const GenDb::NewCf &cf,
    const std::string &compaction_strategy) {
    bool success(
        impl_->CreateTableIfNotExistsSync(cf, compaction_strategy,
            CASS_CONSISTENCY_QUORUM));
    if (!success) {
        IncrementTableWriteFailStats(cf.cfname_);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN_FAMILY);
        return success;
    }
    // Locate (add if not exists) INSERT INTO prepare statement
    success = impl_->LocatePrepareInsertIntoTable(cf);
    if (!success) {
        IncrementTableWriteFailStats(cf.cfname_);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN_FAMILY);
        return success;
    }
    IncrementTableWriteStats(cf.cfname_);
    return success;
}

bool CqlIf::Db_UseColumnfamily(const GenDb::NewCf &cf) {
    // Check existence of table
    return Db_UseColumnfamily(cf.cfname_);
}

bool CqlIf::Db_UseColumnfamily(const std::string &cfname) {
    // Check existence of table
    bool success(impl_->IsTablePresent(cfname));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
        return success;
    }
    IncrementTableReadStats(cfname);
    return success;
}

// Column
void CqlIf::OnAsyncColumnAddCompletion(GenDb::DbOpResult::type drc,
    std::auto_ptr<GenDb::ColList> row,
    std::string cfname, GenDb::GenDbIf::DbAddColumnCb cb) {
    if (drc == GenDb::DbOpResult::OK) {
        IncrementTableWriteStats(cfname);
    } else if (drc == GenDb::DbOpResult::BACK_PRESSURE) {
        IncrementTableWriteBackPressureFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN);
    } else {
        IncrementTableWriteFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN);
    }
    if (!cb.empty()) {
        cb(drc);
    }
}

struct AsyncRowGetCallbackContext {
    AsyncRowGetCallbackContext(GenDb::GenDbIf::DbGetRowCb cb,
        GenDb::DbOpResult::type drc, std::auto_ptr<GenDb::ColList> row) :
        cb_(cb),
        drc_(drc),
        row_(row) {
    }
    GenDb::GenDbIf::DbGetRowCb cb_;
    GenDb::DbOpResult::type drc_;
    std::auto_ptr<GenDb::ColList> row_;
};

static void AsyncRowGetCompletionCallback(
    boost::shared_ptr<AsyncRowGetCallbackContext> cb_ctx) {
    cb_ctx->cb_(cb_ctx->drc_, cb_ctx->row_);
}

void CqlIf::OnAsyncRowGetCompletion(GenDb::DbOpResult::type drc,
    std::auto_ptr<GenDb::ColList> row, std::string cfname,
    GenDb::GenDbIf::DbGetRowCb cb, bool use_worker, int task_id,
    int task_instance) {
    if (drc == GenDb::DbOpResult::OK) {
        IncrementTableReadStats(cfname);
    } else if (drc == GenDb::DbOpResult::BACK_PRESSURE) {
        IncrementTableReadBackPressureFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
    } else {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
    }
    if (use_worker) {
        if (!cb.empty()) {
            boost::shared_ptr<AsyncRowGetCallbackContext> ctx(
                new AsyncRowGetCallbackContext(cb, drc, row));
            impl::WorkerTask *worker(new impl::WorkerTask(
                boost::bind(&AsyncRowGetCompletionCallback, ctx),
                    task_id, task_instance));
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            scheduler->Enqueue(worker);
        }
    } else {
        if (!cb.empty()) {
            cb(drc, row);
        }
    }
}

void CqlIf::OnAsyncRowGetCompletion(GenDb::DbOpResult::type drc,
    std::auto_ptr<GenDb::ColList> row, std::string cfname,
    GenDb::GenDbIf::DbGetRowCb cb) {
    OnAsyncRowGetCompletion(drc, row, cfname, cb, false, -1, -2);
}
bool CqlIf::Db_AddColumn(std::auto_ptr<GenDb::ColList> cl,
    GenDb::DbConsistency::type dconsistency,
    GenDb::GenDbIf::DbAddColumnCb cb) {
    std::string cfname(cl->cfname_);
    if (!initialized_) {
        IncrementTableWriteFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN);
        return false;
    }
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success;
    if (use_prepared_for_insert_ &&
        impl_->IsInsertIntoTablePrepareSupported(cfname)) {
        success = impl_->InsertIntoTablePrepareAsync(cl, consistency,
            boost::bind(&CqlIf::OnAsyncColumnAddCompletion, this, _1, _2, cfname,
            cb));
    } else {
        success = impl_->InsertIntoTableAsync(cl, consistency,
            boost::bind(&CqlIf::OnAsyncColumnAddCompletion, this, _1, _2, cfname,
            cb));
    }
    if (!success) {
        IncrementTableWriteFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN);
        return success;
    }
    return success;
}

bool CqlIf::Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl,
    GenDb::DbConsistency::type dconsistency) {
    std::string cfname(cl->cfname_);
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->InsertIntoTableSync(cl, consistency));
    if (!success) {
        IncrementTableWriteFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_WRITE_COLUMN);
        return success;
    }
    IncrementTableWriteStats(cfname);
    return success;
}

// Read
bool CqlIf::Db_GetRowAsync(const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey, const GenDb::ColumnNameRange &crange,
    GenDb::DbConsistency::type dconsistency, GenDb::GenDbIf::DbGetRowCb cb) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableClusteringKeyRangeAsync(cfname, rowkey,
        crange, consistency, boost::bind(&CqlIf::OnAsyncRowGetCompletion, this,
        _1, _2, cfname, cb)));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
    }
    return success;
}

bool CqlIf::Db_GetRowAsync(const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey, const GenDb::ColumnNameRange &crange,
    GenDb::DbConsistency::type dconsistency, int task_id, int task_instance,
    GenDb::GenDbIf::DbGetRowCb cb) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableClusteringKeyRangeAsync(cfname, rowkey,
        crange, consistency, boost::bind(&CqlIf::OnAsyncRowGetCompletion, this,
        _1, _2, cfname, cb, true, task_id, task_instance)));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
    }
    return success;
}

bool CqlIf::Db_GetRowAsync(const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey,
    GenDb::DbConsistency::type dconsistency,
    GenDb::GenDbIf::DbGetRowCb cb) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableAsync(cfname, rowkey,
        consistency, boost::bind(&CqlIf::OnAsyncRowGetCompletion, this, _1, _2,
        cfname, cb)));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
    }
    return success;
}

bool CqlIf::Db_GetRowAsync(const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey,
    GenDb::DbConsistency::type dconsistency, int task_id, int task_instance,
    GenDb::GenDbIf::DbGetRowCb cb) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableAsync(cfname, rowkey,
        consistency, boost::bind(&CqlIf::OnAsyncRowGetCompletion, this, _1, _2,
        cfname, cb, true, task_id, task_instance)));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
    }
    return success;
}

bool CqlIf::Db_GetRow(GenDb::ColList *out, const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey,
    GenDb::DbConsistency::type dconsistency) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableSync(cfname, rowkey,
        consistency, &out->columns_));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
        return success;
    }
    IncrementTableReadStats(cfname);
    return success;
}

bool CqlIf::Db_GetRow(GenDb::ColList *out, const std::string &cfname,
    const GenDb::DbDataValueVec &rowkey,
    GenDb::DbConsistency::type dconsistency,
    const GenDb::ColumnNameRange &crange,
    const GenDb::FieldNamesToReadVec &read_vec) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableClusteringKeyRangeFieldNamesSync(cfname,
       rowkey, crange, consistency, read_vec, &out->columns_));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
        return success;
    }
    IncrementTableReadStats(cfname);
    return success;
}

bool CqlIf::Db_GetMultiRow(GenDb::ColListVec *out, const std::string &cfname,
    const std::vector<GenDb::DbDataValueVec> &v_rowkey) {
    BOOST_FOREACH(const GenDb::DbDataValueVec &rkey, v_rowkey) {
        std::auto_ptr<GenDb::ColList> v_columns(new GenDb::ColList);
        // Partition Key
        v_columns->rowkey_ = rkey;
        bool success(impl_->SelectFromTableSync(cfname, rkey,
            CASS_CONSISTENCY_ONE, &v_columns->columns_));
        if (!success) {
            CQLIF_ERR_TRACE("SELECT FROM Table: " << cfname << " Partition Key: "
                << GenDb::DbDataValueVecToString(rkey) << " FAILED");
            IncrementTableReadFailStats(cfname);
            IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
            return false;
        }
        out->push_back(v_columns.release());
    }
    IncrementTableReadStats(cfname, v_rowkey.size());
    return true;
}

bool CqlIf::Db_GetMultiRow(GenDb::ColListVec *out, const std::string &cfname,
    const std::vector<GenDb::DbDataValueVec> &v_rowkey,
    const GenDb::ColumnNameRange &crange) {
    BOOST_FOREACH(const GenDb::DbDataValueVec &rkey, v_rowkey) {
        std::auto_ptr<GenDb::ColList> v_columns(new GenDb::ColList);
        // Partition Key
        v_columns->rowkey_ = rkey;
        bool success(impl_->SelectFromTableClusteringKeyRangeSync(cfname,
            rkey, crange, CASS_CONSISTENCY_ONE, &v_columns->columns_));
        if (!success) {
            CQLIF_ERR_TRACE("SELECT FROM Table: " << cfname << " Partition Key: "
                << GenDb::DbDataValueVecToString(rkey) <<
                " Clustering Key Range: " << crange.ToString() << " FAILED");
            IncrementTableReadFailStats(cfname);
            IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
            return false;
        }
        out->push_back(v_columns.release());
    }
    IncrementTableReadStats(cfname, v_rowkey.size());
    return true;
}

bool CqlIf::Db_GetMultiRow(GenDb::ColListVec *out, const std::string &cfname,
    const std::vector<GenDb::DbDataValueVec> &v_rowkey,
    const GenDb::ColumnNameRange &crange,
    const GenDb::FieldNamesToReadVec &read_vec) {
    bool success(impl_->SelectFromTableClusteringKeyRangeFieldNamesSync(cfname,
        v_rowkey, crange, CASS_CONSISTENCY_ONE, read_vec, out));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
        return false;
    }
    IncrementTableReadStats(cfname, v_rowkey.size());
    return true;
}

bool CqlIf::Db_GetAllRows(GenDb::ColListVec *out, const std::string &cfname,
    GenDb::DbConsistency::type dconsistency) {
    CassConsistency consistency(impl::Db2CassConsistency(dconsistency));
    bool success(impl_->SelectFromTableSync(cfname, consistency, out));
    if (!success) {
        IncrementTableReadFailStats(cfname);
        IncrementErrors(GenDb::IfErrors::ERR_READ_COLUMN);
        return success;
    }
    IncrementTableReadStats(cfname);
    return success;
}

// Queue
bool CqlIf::Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const {
    //return impl_->Db_GetQueueStats(queue_count, enqueues);
    return true;
}

void CqlIf::Db_SetQueueWaterMark(bool high, size_t queue_count,
        GenDb::GenDbIf::DbQueueWaterMarkCb cb) {
    //impl_->Db_SetQueueWaterMark(high, queue_count, cb);
}

void CqlIf::Db_ResetQueueWaterMarks() {
    //impl_->Db_ResetQueueWaterMarks();
}

// Stats
bool CqlIf::Db_GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.GetDiffs(vdbti, dbe);
    return true;
}

bool CqlIf::Db_GetCumulativeStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) const {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.GetCumulative(vdbti, dbe);
    return true;
}

void CqlIf::Db_GetCqlMetrics(Metrics *metrics) const {
    impl_->GetMetrics(metrics);
}

void CqlIf::Db_GetCqlStats(DbStats *db_stats) const {
    Metrics metrics;
    impl_->GetMetrics(&metrics);
    db_stats->requests_one_minute_rate = metrics.requests.one_minute_rate;
    db_stats->stats = metrics.stats;
    db_stats->errors = metrics.errors;
}

void CqlIf::IncrementTableWriteStats(const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableWrite(table_name);
}

void CqlIf::IncrementTableWriteStats(const std::string &table_name,
    uint64_t num_writes) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableWrite(table_name, num_writes);
}

void CqlIf::IncrementTableWriteFailStats(const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableWriteFail(table_name);
}

void CqlIf::IncrementTableWriteFailStats(const std::string &table_name,
    uint64_t num_writes) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableWriteFail(table_name, num_writes);
}

void CqlIf::IncrementTableWriteBackPressureFailStats(
    const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableWriteBackPressureFail(table_name);
}

void CqlIf::IncrementTableReadBackPressureFailStats(
    const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableReadBackPressureFail(table_name);
}

void CqlIf::IncrementTableReadStats(const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableRead(table_name);
}

void CqlIf::IncrementTableReadStats(const std::string &table_name,
    uint64_t num_reads) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableRead(table_name, num_reads);
}

void CqlIf::IncrementTableReadFailStats(const std::string &table_name) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableReadFail(table_name);
}

void CqlIf::IncrementTableReadFailStats(const std::string &table_name,
    uint64_t num_reads) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementTableReadFail(table_name, num_reads);
}

void CqlIf::IncrementErrors(GenDb::IfErrors::Type err_type) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    stats_.IncrementErrors(err_type);
}

// Connection
std::vector<GenDb::Endpoint> CqlIf::Db_GetEndpoints() const {
    return endpoints_;
}

namespace interface {

//
// CassDatastaxLibrary
//
CassDatastaxLibrary::CassDatastaxLibrary() {
}

CassDatastaxLibrary::~CassDatastaxLibrary() {
}

// CassCluster
CassCluster* CassDatastaxLibrary::CassClusterNew() {
    return cass_cluster_new();
}

void CassDatastaxLibrary::CassClusterFree(CassCluster* cluster) {
    cass_cluster_free(cluster);
}

CassError CassDatastaxLibrary::CassClusterSetContactPoints(
    CassCluster* cluster, const char* contact_points) {
    return cass_cluster_set_contact_points(cluster, contact_points);
}

CassError CassDatastaxLibrary::CassClusterSetPort(CassCluster* cluster,
    int port) {
    return cass_cluster_set_port(cluster, port);
}

void CassDatastaxLibrary::CassClusterSetCredentials(CassCluster* cluster,
    const char* username, const char* password) {
    cass_cluster_set_credentials(cluster, username, password);
}

CassError CassDatastaxLibrary::CassClusterSetNumThreadsIo(CassCluster* cluster,
    unsigned num_threads) {
    return cass_cluster_set_num_threads_io(cluster, num_threads);
}

CassError CassDatastaxLibrary::CassClusterSetPendingRequestsHighWaterMark(
    CassCluster* cluster, unsigned num_requests) {
    return cass_cluster_set_pending_requests_high_water_mark(cluster,
        num_requests);
}

CassError CassDatastaxLibrary::CassClusterSetPendingRequestsLowWaterMark(
    CassCluster* cluster, unsigned num_requests) {
    return cass_cluster_set_pending_requests_low_water_mark(cluster,
        num_requests);
}

CassError CassDatastaxLibrary::CassClusterSetWriteBytesHighWaterMark(
    CassCluster* cluster, unsigned num_bytes) {
    return cass_cluster_set_write_bytes_high_water_mark(cluster, num_bytes);
}

CassError CassDatastaxLibrary::CassClusterSetWriteBytesLowWaterMark(
    CassCluster* cluster, unsigned num_bytes) {
    return cass_cluster_set_write_bytes_low_water_mark(cluster, num_bytes);
}

// CassSession
CassSession* CassDatastaxLibrary::CassSessionNew() {
    return cass_session_new();
}

void CassDatastaxLibrary::CassSessionFree(CassSession* session) {
    cass_session_free(session);
}

CassFuture* CassDatastaxLibrary::CassSessionConnect(CassSession* session,
    const CassCluster* cluster) {
    return cass_session_connect(session, cluster);
}

CassFuture* CassDatastaxLibrary::CassSessionClose(CassSession* session) {
    return cass_session_close(session);
}

CassFuture* CassDatastaxLibrary::CassSessionExecute(CassSession* session,
    const CassStatement* statement) {
    return cass_session_execute(session, statement);
}

const CassSchemaMeta* CassDatastaxLibrary::CassSessionGetSchemaMeta(
    const CassSession* session) {
    return cass_session_get_schema_meta(session);
}

CassFuture* CassDatastaxLibrary::CassSessionPrepare(CassSession* session,
    const char* query) {
    return cass_session_prepare(session, query);
}

void CassDatastaxLibrary::CassSessionGetMetrics(const CassSession* session,
    CassMetrics* output) {
    cass_session_get_metrics(session, output);
}

// CassSchema
void CassDatastaxLibrary::CassSchemaMetaFree(
    const CassSchemaMeta* schema_meta) {
    cass_schema_meta_free(schema_meta);
}

const CassKeyspaceMeta* CassDatastaxLibrary::CassSchemaMetaKeyspaceByName(
    const CassSchemaMeta* schema_meta, const char* keyspace) {
    return cass_schema_meta_keyspace_by_name(schema_meta, keyspace);
}

const CassTableMeta* CassDatastaxLibrary::CassKeyspaceMetaTableByName(
    const CassKeyspaceMeta* keyspace_meta, const char* table) {
    return cass_keyspace_meta_table_by_name(keyspace_meta, table);
}

size_t CassDatastaxLibrary::CassTableMetaPartitionKeyCount(
    const CassTableMeta* table_meta) {
    return cass_table_meta_partition_key_count(table_meta);
}

size_t CassDatastaxLibrary::CassTableMetaClusteringKeyCount(
    const CassTableMeta* table_meta) {
    return cass_table_meta_clustering_key_count(table_meta);
}

// CassFuture
void CassDatastaxLibrary::CassFutureFree(CassFuture* future) {
    cass_future_free(future);
}

CassError CassDatastaxLibrary::CassFutureSetCallback(CassFuture* future,
    CassFutureCallback callback, void* data) {
    return cass_future_set_callback(future, callback, data);
}

void CassDatastaxLibrary::CassFutureWait(CassFuture* future) {
    cass_future_wait(future);
}

const CassResult* CassDatastaxLibrary::CassFutureGetResult(
    CassFuture* future) {
    return cass_future_get_result(future);
}

void CassDatastaxLibrary::CassFutureErrorMessage(CassFuture* future,
    const char** message, size_t* message_length) {
    cass_future_error_message(future, message, message_length);
}

CassError CassDatastaxLibrary::CassFutureErrorCode(CassFuture* future) {
    return cass_future_error_code(future);
}

const CassPrepared* CassDatastaxLibrary::CassFutureGetPrepared(
    CassFuture* future) {
    return cass_future_get_prepared(future);
}

// CassResult
void CassDatastaxLibrary::CassResultFree(const CassResult* result) {
    cass_result_free(result);
}

size_t CassDatastaxLibrary::CassResultColumnCount(const CassResult* result) {
    return cass_result_column_count(result);
}

CassError CassDatastaxLibrary::CassResultColumnName(const CassResult *result,
    size_t index, const char** name, size_t* name_length) {
    return cass_result_column_name(result, index, name, name_length);
}

// CassIterator
void CassDatastaxLibrary::CassIteratorFree(CassIterator* iterator) {
    cass_iterator_free(iterator);
}

CassIterator* CassDatastaxLibrary::CassIteratorFromResult(
    const CassResult* result) {
    return cass_iterator_from_result(result);
}

cass_bool_t CassDatastaxLibrary::CassIteratorNext(CassIterator* iterator) {
    return cass_iterator_next(iterator);
}

const CassRow* CassDatastaxLibrary::CassIteratorGetRow(
    const CassIterator* iterator) {
    return cass_iterator_get_row(iterator);
}

// CassStatement
CassStatement* CassDatastaxLibrary::CassStatementNew(const char* query,
    size_t parameter_count) {
    return cass_statement_new(query, parameter_count);
}

void CassDatastaxLibrary::CassStatementFree(CassStatement* statement) {
    cass_statement_free(statement);
}

CassError CassDatastaxLibrary::CassStatementSetConsistency(
    CassStatement* statement, CassConsistency consistency) {
    return cass_statement_set_consistency(statement, consistency);
}

CassError CassDatastaxLibrary::CassStatementBindStringN(
    CassStatement* statement,
    size_t index, const char* value, size_t value_length) {
    return cass_statement_bind_string_n(statement, index, value, value_length);
}

CassError CassDatastaxLibrary::CassStatementBindInt32(CassStatement* statement,
    size_t index, cass_int32_t value) {
    return cass_statement_bind_int32(statement, index, value);
}

CassError CassDatastaxLibrary::CassStatementBindInt64(CassStatement* statement,
    size_t index, cass_int64_t value) {
    return cass_statement_bind_int64(statement, index, value);
}

CassError CassDatastaxLibrary::CassStatementBindUuid(CassStatement* statement,
    size_t index, CassUuid value) {
    return cass_statement_bind_uuid(statement, index, value);
}

CassError CassDatastaxLibrary::CassStatementBindDouble(
    CassStatement* statement, size_t index, cass_double_t value) {
    return cass_statement_bind_double(statement, index, value);
}

CassError CassDatastaxLibrary::CassStatementBindInet(CassStatement* statement,
    size_t index, CassInet value) {
    return cass_statement_bind_inet(statement, index, value);
}

CassError CassDatastaxLibrary::CassStatementBindBytes(
    CassStatement* statement,
    size_t index, const cass_byte_t* value, size_t value_length) {
    return cass_statement_bind_bytes(statement, index, value, value_length);
}

CassError CassDatastaxLibrary::CassStatementBindStringByNameN(
    CassStatement* statement,
    const char* name, size_t name_length, const char* value,
    size_t value_length) {
    return cass_statement_bind_string_by_name_n(statement, name, name_length,
        value, value_length);
}

CassError CassDatastaxLibrary::CassStatementBindInt32ByName(
    CassStatement* statement, const char* name, cass_int32_t value) {
    return cass_statement_bind_int32_by_name(statement, name, value);
}

CassError CassDatastaxLibrary::CassStatementBindInt64ByName(
    CassStatement* statement, const char* name, cass_int64_t value) {
    return cass_statement_bind_int64_by_name(statement, name, value);
}

CassError CassDatastaxLibrary::CassStatementBindUuidByName(
    CassStatement* statement, const char* name, CassUuid value) {
    return cass_statement_bind_uuid_by_name(statement, name, value);
}

CassError CassDatastaxLibrary::CassStatementBindDoubleByName(
    CassStatement* statement, const char* name, cass_double_t value) {
    return cass_statement_bind_double_by_name(statement, name, value);
}

CassError CassDatastaxLibrary::CassStatementBindInetByName(
    CassStatement* statement, const char* name, CassInet value) {
    return cass_statement_bind_inet_by_name(statement, name, value);
}

CassError CassDatastaxLibrary::CassStatementBindBytesByNameN(
    CassStatement* statement,
    const char* name, size_t name_length, const cass_byte_t* value,
    size_t value_length) {
    return cass_statement_bind_bytes_by_name_n(statement, name, name_length,
        value, value_length);
}

// CassPrepare
void CassDatastaxLibrary::CassPreparedFree(const CassPrepared* prepared) {
    cass_prepared_free(prepared);
}

CassStatement* CassDatastaxLibrary::CassPreparedBind(
    const CassPrepared* prepared)  {
    return cass_prepared_bind(prepared);
}

// CassValue
CassValueType CassDatastaxLibrary::GetCassValueType(const CassValue* value) {
    return cass_value_type(value);
}

CassError CassDatastaxLibrary::CassValueGetString(const CassValue* value,
    const char** output, size_t* output_size) {
    return cass_value_get_string(value, output, output_size);
}

CassError CassDatastaxLibrary::CassValueGetInt8(const CassValue* value,
    cass_int8_t* output) {
    return cass_value_get_int8(value, output);
}

CassError CassDatastaxLibrary::CassValueGetInt16(const CassValue* value,
    cass_int16_t* output) {
    return cass_value_get_int16(value, output);
}

CassError CassDatastaxLibrary::CassValueGetInt32(const CassValue* value,
    cass_int32_t* output) {
    return cass_value_get_int32(value, output);
}

CassError CassDatastaxLibrary::CassValueGetInt64(const CassValue* value,
    cass_int64_t* output) {
    return cass_value_get_int64(value, output);
}

CassError CassDatastaxLibrary::CassValueGetUuid(const CassValue* value,
    CassUuid* output) {
    return cass_value_get_uuid(value, output);
}

CassError CassDatastaxLibrary::CassValueGetDouble(const CassValue* value,
    cass_double_t* output) {
    return cass_value_get_double(value, output);
}

CassError CassDatastaxLibrary::CassValueGetInet(const CassValue* value,
    CassInet* output) {
    return cass_value_get_inet(value, output);
}
    
CassError CassDatastaxLibrary::CassValueGetBytes(const CassValue* value,
    const cass_byte_t** output, size_t* output_size) {
    return cass_value_get_bytes(value, output, output_size);
}

// CassInet
CassInet CassDatastaxLibrary::CassInetInitV4(
    const cass_uint8_t* address) {
    return cass_inet_init_v4(address);
}

CassInet CassDatastaxLibrary::CassInetInitV6(
    const cass_uint8_t* address) {
    return cass_inet_init_v6(address);
}

// CassRow
const CassValue* CassDatastaxLibrary::CassRowGetColumn(const CassRow* row,
    size_t index) {
    return cass_row_get_column(row, index);
}

// CassLog
void CassDatastaxLibrary::CassLogSetLevel(CassLogLevel log_level) {
    cass_log_set_level(log_level);
}

void CassDatastaxLibrary::CassLogSetCallback(CassLogCallback callback,
    void* data) {
    cass_log_set_callback(callback, data);
}

}  // namespace interface
}  // namespace cql
}  // namespace cass
