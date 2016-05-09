/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CDB_IF_H__
#define __CDB_IF_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/ptr_container/ptr_unordered_map.hpp>

#include <protocol/TBinaryProtocol.h>
#include <transport/TSocketPool.h>
#include <transport/TTransportUtils.h>
#include "gen-cpp/Cassandra.h"

#include <base/queue_task.h>
#include "gendb_if.h"
#include "gendb_statistics.h"

class CdbIf : public GenDb::GenDbIf {
public:
    CdbIf(DbErrorHandler, const std::vector<std::string>&,
        const std::vector<int>&, std::string name,
        bool only_sync);
    CdbIf();
    ~CdbIf();
    // Init/Uninit
    virtual bool Db_Init(const std::string& task_id, int task_instance);
    virtual void Db_Uninit(const std::string& task_id, int task_instance);
    virtual void Db_UninitUnlocked(const std::string& task_id,
        int task_instance);
    virtual void Db_SetInitDone(bool);
    // Tablespace
    virtual bool Db_AddTablespace(const std::string& tablespace,
        const std::string& replication_factor);
    virtual bool Db_SetTablespace(const std::string& tablespace);
    virtual bool Db_AddSetTablespace(const std::string& tablespace,
        const std::string& replication_factor = "1");
    virtual bool Db_FindTablespace(const std::string& tablespace);
    // Column family 
    virtual bool Db_AddColumnfamily(const GenDb::NewCf& cf);
    virtual bool Db_UseColumnfamily(const GenDb::NewCf& cf);
    // Column
    virtual bool Db_AddColumn(std::auto_ptr<GenDb::ColList> cl);
    virtual bool Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl);
    // Read 
    virtual bool Db_GetRow(GenDb::ColList& ret, const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey);
    virtual bool Db_GetMultiRow(GenDb::ColListVec& ret,
        const std::string& cfname,
        const std::vector<GenDb::DbDataValueVec>& key,
        GenDb::ColumnNameRange *crange_ptr = NULL);
    bool Db_GetRangeSlices(GenDb::ColList& col_list,
        const std::string& cfname, const GenDb::ColumnNameRange& crange,
        const GenDb::DbDataValueVec& key);
    // Queue
    virtual bool Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const;
    virtual void Db_SetQueueWaterMark(bool high, size_t queue_count,
        DbQueueWaterMarkCb cb);
    virtual void Db_ResetQueueWaterMarks();
    // Stats
    virtual bool Db_GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe);
    // Connection
    virtual std::string Db_GetHost() const;
    virtual int Db_GetPort() const;

private:
    friend class CdbIfTest;
    class InitTask;
    class CleanupTask;

    typedef boost::function<std::string(const GenDb::DbDataValue&)>
        DbEncodeCompositeFunc;
    typedef boost::function<GenDb::DbDataValue(const char *input, int &used)>
        DbDecodeCompositeFunc;
    typedef boost::function<std::string(const GenDb::DbDataValue&)>
        DbEncodeNonCompositeFunc;
    typedef boost::function<GenDb::DbDataValue(const std::string&)>
        DbDecodeNonCompositeFunc;

    struct CdbIfTypeInfo {
        CdbIfTypeInfo(std::string cassandra_type,
            DbEncodeCompositeFunc encode_composite_fn,
            DbDecodeCompositeFunc decode_composite_fn,
            DbEncodeNonCompositeFunc encode_non_composite_fn,
            DbDecodeNonCompositeFunc decode_non_composite_fn) :
            cassandra_type_("org.apache.cassandra.db.marshal." + cassandra_type),
            encode_composite_fn_(encode_composite_fn),
            decode_composite_fn_(decode_composite_fn),
            encode_non_composite_fn_(encode_non_composite_fn),
            decode_non_composite_fn_(decode_non_composite_fn) {
        }

        std::string cassandra_type_;
        DbEncodeCompositeFunc encode_composite_fn_;
        DbDecodeCompositeFunc decode_composite_fn_;
        DbEncodeNonCompositeFunc encode_non_composite_fn_;
        DbDecodeNonCompositeFunc decode_non_composite_fn_;
    };
    typedef boost::unordered_map<GenDb::DbDataType::type, CdbIfTypeInfo>
        CdbIfTypeMapDef;
    static CdbIfTypeMapDef CdbIfTypeMap;

    struct CdbIfCfInfo {
        CdbIfCfInfo() {
        }
        CdbIfCfInfo(org::apache::cassandra::CfDef *cfdef) {
            cfdef_.reset(cfdef);
        }
        CdbIfCfInfo(org::apache::cassandra::CfDef *cfdef, GenDb::NewCf *cf) {
            cfdef_.reset(cfdef);
            cf_.reset(cf);
        }
        ~CdbIfCfInfo() {
        }
        std::auto_ptr<org::apache::cassandra::CfDef> cfdef_;
        std::auto_ptr<GenDb::NewCf> cf_;
    };
    typedef boost::ptr_unordered_map<std::string, CdbIfCfInfo> CdbIfCfListType;
    CdbIfCfListType CdbIfCfList;

    struct CdbIfColList {
        GenDb::ColList *gendb_cl;
    };

    // Encode and decode
    bool DbDataTypeVecToCompositeType(std::string& res,
        const std::vector<GenDb::DbDataType::type>& db_type);
    bool DbDataValueFromType(GenDb::DbDataValue& res,
        const GenDb::DbDataType::type& type, const std::string& input);
    bool DbDataValueFromType(GenDb::DbDataValue&, const std::string&,
        const std::string&);
    bool DbDataValueFromString(GenDb::DbDataValue&, const std::string&,
        const std::string&, const std::string&);
    bool DbDataValueToStringNonComposite(std::string&,
        const GenDb::DbDataValue&);
    bool DbDataValueVecToString(std::string&, bool composite,
        const GenDb::DbDataValueVec&);
    bool ConstructDbDataValueKey(std::string&, const GenDb::NewCf *,
        const GenDb::DbDataValueVec&);
    bool ConstructDbDataValueColumnName(std::string&, const GenDb::NewCf *,
        const GenDb::DbDataValueVec&);
    bool ConstructDbDataValueColumnValue(std::string&, const GenDb::NewCf *,
        const GenDb::DbDataValueVec&);
    bool DbDataValueVecFromString(GenDb::DbDataValueVec&,
        const GenDb::DbDataTypeVec&, const std::string&);
    bool ColListFromColumnOrSuper(GenDb::ColList&,
        std::vector<org::apache::cassandra::ColumnOrSuperColumn>&,
        const std::string&);
    // Init/Uninit
    bool Db_IsInitDone() const;
    // Column family
    bool Db_Columnfamily_present(const std::string& cfname);
    bool Db_GetColumnfamily(CdbIfCfInfo **info, const std::string& cfname);
    bool Db_FindColumnfamily(const std::string& cfname);
    // Column
    bool Db_AsyncAddColumn(CdbIfColList &cl);
    bool Db_AsyncAddColumnLocked(CdbIfColList &cl);
    void Db_BatchAddColumn(bool done);
    bool DB_IsCfSchemaChanged(org::apache::cassandra::CfDef *cfdef,
                              org::apache::cassandra::CfDef *newcfdef);
    // Read
    static const int kMaxQueryRows = 50;

    // API to get range of column data for a range of rows 
    // Number of columns returned is less than or equal to count field
    // in crange
    bool Db_GetRangeSlicesInternal(GenDb::ColList& col_list,
        const GenDb::NewCf *cf, const GenDb::ColumnNameRange& crange,
        const GenDb::DbDataValueVec& key);

    // Statistics
    struct CdbIfStats {
        CdbIfStats() {
        }
        struct Errors {
            Errors() {
                write_tablespace_fails = 0;
                read_tablespace_fails = 0;
                write_column_family_fails = 0;
                read_column_family_fails = 0;
                write_column_fails = 0;
                write_batch_column_fails = 0;
                read_column_fails = 0;
            }
            void Get(GenDb::DbErrors *db_errors) const;
            tbb::atomic<uint64_t> write_tablespace_fails;
            tbb::atomic<uint64_t> read_tablespace_fails;
            tbb::atomic<uint64_t> write_column_family_fails;
            tbb::atomic<uint64_t> read_column_family_fails;
            tbb::atomic<uint64_t> write_column_fails;
            tbb::atomic<uint64_t> write_batch_column_fails;
            tbb::atomic<uint64_t> read_column_fails;
        };
        enum ErrorType {
            CDBIF_STATS_ERR_NO_ERROR,
            CDBIF_STATS_ERR_WRITE_TABLESPACE,
            CDBIF_STATS_ERR_READ_TABLESPACE,
            CDBIF_STATS_ERR_WRITE_COLUMN_FAMILY,
            CDBIF_STATS_ERR_READ_COLUMN_FAMILY,
            CDBIF_STATS_ERR_WRITE_COLUMN,
            CDBIF_STATS_ERR_WRITE_BATCH_COLUMN,
            CDBIF_STATS_ERR_READ_COLUMN,
        };
        enum CfOp {
            CDBIF_STATS_CF_OP_NONE,
            CDBIF_STATS_CF_OP_WRITE,
            CDBIF_STATS_CF_OP_WRITE_FAIL,
            CDBIF_STATS_CF_OP_READ,
            CDBIF_STATS_CF_OP_READ_FAIL,
        };
        void IncrementErrors(ErrorType type);
        void UpdateCf(const std::string &cf_name, bool write, bool fail);
        void Get(std::vector<GenDb::DbTableInfo> *vdbti, GenDb::DbErrors *dbe);
        GenDb::DbTableStatistics cf_stats_;
        Errors db_errors_;
        Errors odb_errors_;
    };

    friend CdbIfStats::Errors operator+(const CdbIfStats::Errors &a,
        const CdbIfStats::Errors &b);
    friend CdbIfStats::Errors operator-(const CdbIfStats::Errors &a,
        const CdbIfStats::Errors &b);

    void UpdateCfStats(CdbIfStats::CfOp op, const std::string &cf_name);
    void UpdateCfWriteStats(const std::string &cf_name);
    void UpdateCfWriteFailStats(const std::string &cf_name);
    void UpdateCfReadStats(const std::string &cf_name);
    void UpdateCfReadFailStats(const std::string &cf_name);

    static const size_t kQueueSize = 200 * 1024 * 1024; // 200 MB
    typedef WorkQueue<CdbIfColList> CdbIfQueue;
    friend class WorkQueue<CdbIfColList>;
    typedef boost::tuple<bool, size_t, DbQueueWaterMarkCb>
        DbQueueWaterMarkInfo;
    void Db_SetQueueWaterMarkInternal(CdbIfQueue *queue,
        const std::vector<DbQueueWaterMarkInfo> &vwmi);
    void Db_SetQueueWaterMarkInternal(CdbIfQueue *queue,
        const DbQueueWaterMarkInfo &wmi);

    boost::shared_ptr<apache::thrift::transport::TSocketPool> socket_;
    boost::shared_ptr<apache::thrift::transport::TTransport> transport_;
    boost::shared_ptr<apache::thrift::protocol::TProtocol> protocol_;
    boost::scoped_ptr<org::apache::cassandra::CassandraClient> client_;
    DbErrorHandler errhandler_;
    tbb::atomic<bool> db_init_done_;
    std::string tablespace_;
    boost::scoped_ptr<CdbIfQueue> cdbq_;
    std::string name_;
    mutable tbb::mutex cdbq_mutex_;
    InitTask *init_task_;
    CleanupTask *cleanup_task_;
    bool only_sync_;
    int task_instance_;
    int prev_task_instance_;
    bool task_instance_initialized_;
    typedef std::vector<org::apache::cassandra::Mutation> MutationList;
    typedef std::map<std::string, MutationList> CFMutationMap;
    typedef std::map<std::string, CFMutationMap> CassandraMutationMap;
    CassandraMutationMap mutation_map_;
    mutable tbb::mutex smutex_;
    CdbIfStats stats_;
    std::vector<DbQueueWaterMarkInfo> cdbq_wm_info_;
    // Connection timeout to a server (before moving to next server)
    static const int kConnectionTimeout = 3000;
    static const int kRecvTimeout = 30000;
};

CdbIf::CdbIfStats::Errors operator+(const CdbIf::CdbIfStats::Errors &a,
    const CdbIf::CdbIfStats::Errors &b);
CdbIf::CdbIfStats::Errors operator-(const CdbIf::CdbIfStats::Errors &a,
    const CdbIf::CdbIfStats::Errors &b);

template<>
size_t CdbIf::CdbIfQueue::AtomicIncrementQueueCount(
    CdbIf::CdbIfColList *entry);

template<>
size_t CdbIf::CdbIfQueue::AtomicDecrementQueueCount(
    CdbIf::CdbIfColList *entry);

template<>
struct WorkQueueDelete<CdbIf::CdbIfColList> {
    template <typename QueueT>
    void operator()(QueueT &q, bool delete_entry) {
        CdbIf::CdbIfColList colList;
        while (q.try_pop(colList)) {    
            delete colList.gendb_cl;
            colList.gendb_cl = NULL;
        }
    }
};

// Composite
std::string DbEncodeStringComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeStringComposite(const char *input, int &used);
std::string DbEncodeUUIDComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeUUIDComposite(const char *input, int &used);
std::string DbEncodeDoubleComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeDoubleComposite(const char *input, int &used);
template <typename T> std::string DbEncodeIntegerComposite(
    const GenDb::DbDataValue &value);
template <typename T> GenDb::DbDataValue DbDecodeIntegerComposite(
    const char *input, int &used);

// Non composite
std::string DbEncodeStringNonComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeStringNonComposite(const std::string &input);
std::string DbEncodeUUIDNonComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeUUIDNonComposite(const std::string &input);
std::string DbEncodeDoubleNonComposite(const GenDb::DbDataValue &value);
GenDb::DbDataValue DbDecodeDoubleNonComposite(const std::string &input);
template <typename T> std::string DbEncodeIntegerNonComposite(
    const GenDb::DbDataValue &value);
template <typename T> GenDb::DbDataValue DbDecodeIntegerNonComposite(
    const std::string &input);

#endif // __CDB_IF_H__
