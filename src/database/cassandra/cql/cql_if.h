//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_CQL_CQL_IF_H_
#define DATABASE_CASSANDRA_CQL_CQL_IF_H_

#include <string>
#include <vector>

#include <tbb/atomic.h>
#include <sandesh/sandesh.h>
#include <database/gendb_if.h>
#include <database/gendb_statistics.h>
#include <database/cassandra/cql/cql_types.h>
#include <database/cassandra/cql/cql_lib_if.h>
#include <database/cassandra/cql/cql_if_impl.h>

class EventManager;

namespace cass {
namespace cql {

class CqlIf : public GenDb::GenDbIf {
 public:
    CqlIf(EventManager *evm,
        const std::vector<std::string> &cassandra_ips,
        int cassandra_port,
        const std::string &cassandra_user,
        const std::string &cassandra_password);
    CqlIf();
    virtual ~CqlIf();
    // Init/Uninit
    virtual bool Db_Init();
    virtual void Db_Uninit();
    virtual void Db_UninitUnlocked();
    virtual void Db_SetInitDone(bool);
    // Tablespace
    virtual bool Db_SetTablespace(const std::string &tablespace);
    virtual bool Db_AddSetTablespace(const std::string &tablespace,
        const std::string &replication_factor = "1");
    // Column family
    virtual bool Db_AddColumnfamily(const GenDb::NewCf &cf,
        const std::string &compaction_strategy);
    virtual bool Db_UseColumnfamily(const GenDb::NewCf &cf);
    virtual bool Db_UseColumnfamily(const std::string &cfname);
    // Column
    virtual bool Db_AddColumn(std::auto_ptr<GenDb::ColList> cl,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbAddColumnCb cb);
    virtual bool Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl,
        GenDb::DbConsistency::type dconsistency);
    // Read
    virtual bool Db_GetRow(GenDb::ColList *out, const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        GenDb::DbConsistency::type dconsistency);
    virtual bool Db_GetRow(GenDb::ColList *out, const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        GenDb::DbConsistency::type dconsistency,
        const GenDb::ColumnNameRange &crange,
        const GenDb::FieldNamesToReadVec &read_vec);
    virtual bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey);
    virtual bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey,
        const GenDb::ColumnNameRange &crange);
    virtual bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey,
        const GenDb::ColumnNameRange &crange,
        const GenDb::FieldNamesToReadVec &read_vec);
    virtual bool Db_GetRowAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbGetRowCb cb);
    virtual bool Db_GetRowAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        GenDb::DbConsistency::type dconsistency, int task_id,
        int task_instance, GenDb::GenDbIf::DbGetRowCb cb);
    virtual bool Db_GetRowAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbGetRowCb cb);
    virtual bool Db_GetRowAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency, int task_id,
        int task_instance, GenDb::GenDbIf::DbGetRowCb cb);
    virtual bool Db_GetAllRows(GenDb::ColListVec *out,
        const std::string &cfname, GenDb::DbConsistency::type dconsistency);
    // Queue
    virtual bool Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const;
    virtual void Db_SetQueueWaterMark(bool high, size_t queue_count,
        DbQueueWaterMarkCb cb);
    virtual void Db_ResetQueueWaterMarks();
    // Stats
    virtual bool Db_GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe);
    virtual bool Db_GetCumulativeStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) const;
    virtual void Db_GetCqlMetrics(Metrics *metrics) const;
    virtual void Db_GetCqlStats(DbStats *db_stats) const;
    // Connection
    virtual std::vector<GenDb::Endpoint> Db_GetEndpoints() const;

 private:
    void OnAsyncColumnAddCompletion(GenDb::DbOpResult::type drc,
        std::auto_ptr<GenDb::ColList> row,
        std::string cfname, GenDb::GenDbIf::DbAddColumnCb cb);
    void OnAsyncRowGetCompletion(GenDb::DbOpResult::type drc,
        std::auto_ptr<GenDb::ColList> row,
        std::string cfname, GenDb::GenDbIf::DbGetRowCb cb);
    void OnAsyncRowGetCompletion(GenDb::DbOpResult::type drc,
        std::auto_ptr<GenDb::ColList> row,
        std::string cfname, GenDb::GenDbIf::DbGetRowCb cb,
        bool use_worker, int task_id, int task_instance);
    void IncrementTableWriteStats(const std::string &table_name);
    void IncrementTableWriteStats(const std::string &table_name,
        uint64_t num_writes);
    void IncrementTableWriteFailStats(const std::string &table_name);
    void IncrementTableWriteFailStats(const std::string &table_name,
        uint64_t num_writes);
    void IncrementTableWriteBackPressureFailStats(
        const std::string &table_name);
    void IncrementTableReadStats(const std::string &table_name);
    void IncrementTableReadStats(const std::string &table_name,
        uint64_t num_reads);
    void IncrementTableReadFailStats(const std::string &table_name);
    void IncrementTableReadFailStats(const std::string &table_name,
        uint64_t num_reads);
    void IncrementTableReadBackPressureFailStats(
        const std::string &table_name);
    void IncrementErrors(GenDb::IfErrors::Type err_type);

    boost::scoped_ptr<interface::CassLibrary> cci_;
    boost::scoped_ptr<CqlIfImpl> impl_;
    tbb::atomic<bool> initialized_;
    std::vector<GenDb::Endpoint> endpoints_;
    mutable tbb::mutex stats_mutex_;
    GenDb::GenDbIfStats stats_;
    bool use_prepared_for_insert_;
};

} // namespace cql
} // namespace cass

#endif // DATABASE_CASSANDRA_CQL_CQL_IF_H_
