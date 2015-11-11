/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef DATABASE_CASSANDRA_THRIFT_THRIFT_IF_IMPL_H_
#define DATABASE_CASSANDRA_THRIFT_THRIFT_IF_IMPL_H_

#include <boost/scoped_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/ptr_container/ptr_unordered_map.hpp>

#include <database/cassandra/thrift/gen-cpp/Cassandra.h>

#include <base/queue_task.h>
#include <database/gendb_if.h>
#include <database/gendb_statistics.h>

class ThriftIfImpl {
 public:
    ThriftIfImpl(GenDb::GenDbIf::DbErrorHandler,
        const std::vector<std::string>&,
        const std::vector<int>&, std::string name,
        bool only_sync, const std::string& cassandra_user,
        const std::string& cassandra_password);
    ThriftIfImpl();
    virtual ~ThriftIfImpl();
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
    // Queue
    virtual bool Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const;
    virtual void Db_SetQueueWaterMark(bool high, size_t queue_count,
        GenDb::GenDbIf::DbQueueWaterMarkCb cb);
    virtual void Db_ResetQueueWaterMarks();
    // Stats
    virtual bool Db_GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe);
    // Connection
    virtual std::string Db_GetHost() const;
    virtual int Db_GetPort() const;

 private:
    friend class ThriftIfTest;
    class InitTask;
    class CleanupTask;
    struct ThriftIfCfInfo {
        ThriftIfCfInfo() {
        }
        ThriftIfCfInfo(org::apache::cassandra::CfDef *cfdef) {
            cfdef_.reset(cfdef);
        }
        ThriftIfCfInfo(org::apache::cassandra::CfDef *cfdef, GenDb::NewCf *cf) {
            cfdef_.reset(cfdef);
            cf_.reset(cf);
        }
        ~ThriftIfCfInfo() {
        }
        std::auto_ptr<org::apache::cassandra::CfDef> cfdef_;
        std::auto_ptr<GenDb::NewCf> cf_;
    };
    typedef boost::ptr_unordered_map<std::string, ThriftIfCfInfo> ThriftIfCfListType;
    ThriftIfCfListType ThriftIfCfList;

    struct ThriftIfColList {
        GenDb::ColList *gendb_cl;
    };

    // Init/Uninit
    bool Db_IsInitDone() const;
    // Column family
    bool Db_Columnfamily_present(const std::string& cfname);
    bool Db_GetColumnfamily(ThriftIfCfInfo **info, const std::string& cfname);
    bool Db_FindColumnfamily(const std::string& cfname);
    // Column
    bool Db_AsyncAddColumn(ThriftIfColList &cl);
    bool Db_AsyncAddColumnLocked(ThriftIfColList &cl);
    void Db_BatchAddColumn(bool done);
    bool DB_IsCfSchemaChanged(org::apache::cassandra::CfDef *cfdef,
                              org::apache::cassandra::CfDef *newcfdef);
    // Encode and decode
    bool DbDataValueFromString(GenDb::DbDataValue&, const std::string&,
        const std::string&, const std::string&);
    bool ColListFromColumnOrSuper(GenDb::ColList&,
        std::vector<org::apache::cassandra::ColumnOrSuperColumn>&,
        const std::string&);
    // Read
    static const int kMaxQueryRows = 50;

    // Statistics
    struct ThriftIfStats {
        ThriftIfStats() {
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
            THRIFTIF_STATS_ERR_NO_ERROR,
            THRIFTIF_STATS_ERR_WRITE_TABLESPACE,
            THRIFTIF_STATS_ERR_READ_TABLESPACE,
            THRIFTIF_STATS_ERR_WRITE_COLUMN_FAMILY,
            THRIFTIF_STATS_ERR_READ_COLUMN_FAMILY,
            THRIFTIF_STATS_ERR_WRITE_COLUMN,
            THRIFTIF_STATS_ERR_WRITE_BATCH_COLUMN,
            THRIFTIF_STATS_ERR_READ_COLUMN,
        };
        enum CfOp {
            THRIFTIF_STATS_CF_OP_NONE,
            THRIFTIF_STATS_CF_OP_WRITE,
            THRIFTIF_STATS_CF_OP_WRITE_FAIL,
            THRIFTIF_STATS_CF_OP_READ,
            THRIFTIF_STATS_CF_OP_READ_FAIL,
        };
        void IncrementErrors(ErrorType type);
        void UpdateCf(const std::string &cf_name, bool write, bool fail);
        void Get(std::vector<GenDb::DbTableInfo> *vdbti, GenDb::DbErrors *dbe);
        GenDb::DbTableStatistics cf_stats_;
        Errors db_errors_;
        Errors odb_errors_;
    };

    friend ThriftIfStats::Errors operator+(const ThriftIfStats::Errors &a,
        const ThriftIfStats::Errors &b);
    friend ThriftIfStats::Errors operator-(const ThriftIfStats::Errors &a,
        const ThriftIfStats::Errors &b);

    void UpdateCfStats(ThriftIfStats::CfOp op, const std::string &cf_name);
    void UpdateCfWriteStats(const std::string &cf_name);
    void UpdateCfWriteFailStats(const std::string &cf_name);
    void UpdateCfReadStats(const std::string &cf_name);
    void UpdateCfReadFailStats(const std::string &cf_name);

    static const size_t kQueueSize = 200 * 1024 * 1024; // 200 MB
    typedef WorkQueue<ThriftIfColList> ThriftIfQueue;
    friend class WorkQueue<ThriftIfColList>;
    typedef boost::tuple<bool, size_t, GenDb::GenDbIf::DbQueueWaterMarkCb>
        DbQueueWaterMarkInfo;
    void Db_SetQueueWaterMarkInternal(ThriftIfQueue *queue,
        const std::vector<DbQueueWaterMarkInfo> &vwmi);
    void Db_SetQueueWaterMarkInternal(ThriftIfQueue *queue,
        const DbQueueWaterMarkInfo &wmi);
    bool set_keepalive();

    boost::shared_ptr<apache::thrift::transport::TTransport> socket_;
    boost::shared_ptr<apache::thrift::transport::TTransport> transport_;
    boost::shared_ptr<apache::thrift::protocol::TProtocol> protocol_;
    boost::scoped_ptr<org::apache::cassandra::CassandraClient> client_;
    GenDb::GenDbIf::DbErrorHandler errhandler_;
    tbb::atomic<bool> db_init_done_;
    std::string tablespace_;
    boost::scoped_ptr<ThriftIfQueue> q_;
    std::string name_;
    mutable tbb::mutex q_mutex_;
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
    ThriftIfStats stats_;
    std::vector<DbQueueWaterMarkInfo> q_wm_info_;
    std::string cassandra_user_;
    std::string cassandra_password_;
    // Connection timeout to a server (before moving to next server)
    static const int connectionTimeout = 3000;
    static const int keepaliveIdleSec = 15;
    static const int keepaliveIntvlSec = 3;
    static const int keepaliveProbeCount = 5;
    static const int tcpUserTimeoutMs = 30000;
};

ThriftIfImpl::ThriftIfStats::Errors operator+(
    const ThriftIfImpl::ThriftIfStats::Errors &a,
    const ThriftIfImpl::ThriftIfStats::Errors &b);
ThriftIfImpl::ThriftIfStats::Errors operator-(
    const ThriftIfImpl::ThriftIfStats::Errors &a,
    const ThriftIfImpl::ThriftIfStats::Errors &b);

template<>
size_t ThriftIfImpl::ThriftIfQueue::AtomicIncrementQueueCount(
    ThriftIfImpl::ThriftIfColList *entry);

template<>
size_t ThriftIfImpl::ThriftIfQueue::AtomicDecrementQueueCount(
    ThriftIfImpl::ThriftIfColList *entry);

template<>
struct WorkQueueDelete<ThriftIfImpl::ThriftIfColList> {
    template <typename QueueT>
    void operator()(QueueT &q, bool delete_entry) {
        ThriftIfImpl::ThriftIfColList colList;
        while (q.try_pop(colList)) {
            delete colList.gendb_cl;
            colList.gendb_cl = NULL;
        }
    }
};

// Encode and decode
bool DbDataValueVecToString(std::string& res, bool composite,
    const GenDb::DbDataValueVec& input);
bool DbDataValueVecFromString(GenDb::DbDataValueVec& res,
    const GenDb::DbDataTypeVec& typevec, const std::string& input);

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

#endif // DATABASE_CASSANDRA_THRIFT_THRIFT_IF_IMPL_H_
