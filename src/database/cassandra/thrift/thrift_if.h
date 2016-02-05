//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_THRIFT_THRIFT_IF_H_
#define DATABASE_CASSANDRA_THRIFT_THRIFT_IF_H_

#include <database/gendb_if.h>

class ThriftIfImpl;

class ThriftIf : public GenDb::GenDbIf {
 public:
    ThriftIf(DbErrorHandler, const std::vector<std::string>&,
        const std::vector<int>&, std::string name,
        bool only_sync, const std::string& cassandra_user,
        const std::string& cassandra_password);
    ThriftIf();
    virtual ~ThriftIf();
    // Init/Uninit
    virtual bool Db_Init(const std::string& task_id, int task_instance);
    virtual void Db_Uninit(const std::string& task_id, int task_instance);
    virtual void Db_UninitUnlocked(const std::string& task_id,
        int task_instance);
    virtual void Db_SetInitDone(bool);
    // Tablespace
    virtual bool Db_SetTablespace(const std::string& tablespace);
    virtual bool Db_AddSetTablespace(const std::string& tablespace,
        const std::string& replication_factor = "1");
    // Column family
    virtual bool Db_AddColumnfamily(const GenDb::NewCf& cf);
    virtual bool Db_UseColumnfamily(const GenDb::NewCf& cf);
    // Column
    virtual bool Db_AddColumn(std::auto_ptr<GenDb::ColList> cl);
    virtual bool Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl);
    // Read
    virtual bool Db_GetRow(GenDb::ColList *ret, const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey);
    virtual bool Db_GetMultiRow(GenDb::ColListVec *ret,
        const std::string& cfname,
        const std::vector<GenDb::DbDataValueVec>& key);
    virtual bool Db_GetMultiRow(GenDb::ColListVec *ret,
        const std::string& cfname,
        const std::vector<GenDb::DbDataValueVec>& key,
        const GenDb::ColumnNameRange& crange);
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
    virtual std::vector<GenDb::Endpoint> Db_GetEndpoints() const;

 private:
    ThriftIfImpl *impl_;
};

#endif // DATABASE_CASSANDRA_THRIFT_THRIFT_IF_H_
