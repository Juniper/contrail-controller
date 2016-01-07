/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef DATABASE_GENDB_IF_H_
#define DATABASE_GENDB_IF_H_

#include <string>
#include <vector>
#include <map>
#include <boost/function.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/variant.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/scoped_ptr.hpp>
#include <database/gendb_types.h>
#include <net/address.h>

namespace GenDb {

/* New stuff */
typedef boost::variant<boost::blank, std::string, uint64_t, uint32_t,
    boost::uuids::uuid, uint8_t, uint16_t, double, IpAddress> DbDataValue;
enum DbDataValueType {
    DB_VALUE_BLANK = 0,
    DB_VALUE_STRING = 1,
    DB_VALUE_UINT64 = 2,
    DB_VALUE_UINT32 = 3,
    DB_VALUE_UUID = 4,
    DB_VALUE_UINT8 = 5,
    DB_VALUE_UINT16 = 6,
    DB_VALUE_DOUBLE = 7,
    DB_VALUE_INET = 8
};
typedef std::vector<DbDataValue> DbDataValueVec;
typedef std::vector<GenDb::DbDataType::type> DbDataTypeVec;

std::string DbDataValueVecToString(const GenDb::DbDataValueVec &v_db_value);
std::string DbDataValueToString(const GenDb::DbDataValue &db_value);

class DbDataValueCqlPrinter : public boost::static_visitor<> {
 public:
    DbDataValueCqlPrinter(std::ostream &os, bool quote_strings) :
        os_(os),
        quote_strings_(quote_strings) {
    }
    DbDataValueCqlPrinter(std::ostream &os) :
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
    void operator()(const uint8_t &t8) const {
        os_ << (uint16_t)t8;
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

struct NewCf {
    enum ColumnFamilyType {
        COLUMN_FAMILY_INVALID = 0,
        COLUMN_FAMILY_SQL = 1,
        COLUMN_FAMILY_NOSQL = 2,
    };
    typedef std::map<std::string, GenDb::DbDataType::type> SqlColumnMap; /* columns meta-data */

    NewCf(const std::string& cfname, const DbDataTypeVec& key_type,
            const SqlColumnMap& cfcolumns) :
        cfname_(cfname),
        cftype_(COLUMN_FAMILY_SQL),
        key_validation_class(key_type),
        cfcolumns_(cfcolumns) {
    }

    NewCf(const std::string& cfname, const DbDataTypeVec& key_type,
            const DbDataTypeVec& comp_type,
            const DbDataTypeVec& valid_class) :
        cfname_(cfname),
        cftype_(COLUMN_FAMILY_NOSQL),
        key_validation_class(key_type),
        comparator_type(comp_type),
        default_validation_class(valid_class) {
    }

    ~NewCf() {}

    std::string cfname_;
    ColumnFamilyType cftype_;
    DbDataTypeVec key_validation_class; /* for key-value comparison */
    SqlColumnMap cfcolumns_; /* columns meta-data */
    DbDataTypeVec comparator_type; /* for column-name comparison */
    DbDataTypeVec default_validation_class; /* for column-value comparison */
};

struct NewCol {
    NewCol(DbDataValueVec* n, DbDataValueVec* v, int ttl) :
        cftype_(NewCf::COLUMN_FAMILY_NOSQL), name(n), value(v), ttl(ttl) {}

    NewCol(const std::string& n, const DbDataValue& v, int ttl) :
        cftype_(NewCf::COLUMN_FAMILY_SQL), name(new DbDataValueVec(1, n)),
        value(new DbDataValueVec(1, v)), ttl(ttl) {}

    NewCol(const NewCol &rhs) :
        cftype_(rhs.cftype_), name(new DbDataValueVec(*rhs.name)),
        value(new DbDataValueVec(*rhs.value)), ttl(rhs.ttl) {}

    bool operator==(const NewCol &rhs) const {
        return (*rhs.name == *name &&
                *rhs.value == *value);
    }

    size_t GetSize() const;

    NewCf::ColumnFamilyType cftype_;
    boost::scoped_ptr<DbDataValueVec> name;
    boost::scoped_ptr<DbDataValueVec> value;
    int ttl;
};

typedef boost::ptr_vector<NewCol> NewColVec;

struct ColList {
    ColList() {
    }

    ~ColList() {
    }

    size_t GetSize() const;

    std::string cfname_; /* column family name */
    DbDataValueVec rowkey_; /* rowkey-value */
    NewColVec columns_;
};

typedef boost::ptr_vector<ColList> ColListVec;

struct ColumnNameRange {
    ColumnNameRange() : count_(0) {
    }

    ~ColumnNameRange() {
    }

    bool IsEmpty() const {
        return start_.empty() &&
            finish_.empty() &&
            count_ == 0;
    }

    std::string ToString() const;

    DbDataValueVec start_;
    DbDataValueVec finish_;
    uint32_t count_;
};

class GenDbIf {
public:
    typedef boost::function<void(void)> DbErrorHandler;
    typedef boost::function<void(size_t)> DbQueueWaterMarkCb;

    GenDbIf() {}
    virtual ~GenDbIf() {}

    // Init/Uninit
    virtual bool Db_Init(const std::string& task_id, int task_instance) = 0;
    virtual void Db_Uninit(const std::string& task_id, int task_instance) = 0;
    virtual void Db_UninitUnlocked(const std::string& task_id,
        int task_instance) = 0;
    virtual void Db_SetInitDone(bool init_done) = 0;
    // Tablespace
    virtual bool Db_SetTablespace(const std::string& tablespace) = 0;
    virtual bool Db_AddSetTablespace(const std::string& tablespace,
        const std::string& replication_factor="1") = 0;
    // Column family
    virtual bool Db_AddColumnfamily(const NewCf& cf) = 0;
    virtual bool Db_UseColumnfamily(const NewCf& cf) = 0;
    // Column
    virtual bool Db_AddColumn(std::auto_ptr<ColList> cl) = 0;
    virtual bool Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl) = 0;
    // Read/Get
    virtual bool Db_GetRow(ColList *ret, const std::string& cfname,
        const DbDataValueVec& rowkey) = 0;
    virtual bool Db_GetMultiRow(ColListVec *ret,
        const std::string& cfname, const std::vector<DbDataValueVec>& key) = 0;
    virtual bool Db_GetMultiRow(ColListVec *ret,
        const std::string& cfname, const std::vector<DbDataValueVec>& key,
        const GenDb::ColumnNameRange& crange) = 0;
    // Queue
    virtual bool Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const = 0;
    virtual void Db_SetQueueWaterMark(bool high, size_t queue_count,
        DbQueueWaterMarkCb cb) = 0;
    virtual void Db_ResetQueueWaterMarks() = 0;
    // Stats
    virtual bool Db_GetStats(std::vector<DbTableInfo> *vdbti,
        DbErrors *dbe) = 0;
    // Connection
    virtual std::string Db_GetHost() const = 0;
    virtual int Db_GetPort() const = 0;
};

} // namespace GenDb

#endif // DATABASE_GENDB_IF_H_
