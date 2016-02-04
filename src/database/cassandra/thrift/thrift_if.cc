//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#include <database/gendb_if.h>
#include <database/cassandra/thrift/thrift_if.h>
#include <database/cassandra/thrift/thrift_if_impl.h>

ThriftIf::ThriftIf(GenDb::GenDbIf::DbErrorHandler errhandler,
        const std::vector<std::string> &cassandra_ips,
        const std::vector<int> &cassandra_ports,
        std::string name, bool only_sync, const std::string& cassandra_user,
        const std::string& cassandra_password) {
    impl_ = new ThriftIfImpl(errhandler, cassandra_ips, cassandra_ports,
        name, only_sync, cassandra_user, cassandra_password);
}

ThriftIf::ThriftIf() : impl_(NULL) {
}

ThriftIf::~ThriftIf() {
    if (impl_) {
        delete impl_;
    }
}

// Init/Uninit
bool ThriftIf::Db_Init(const std::string& task_id, int task_instance) {
    return impl_->Db_Init(task_id, task_instance);
}

void ThriftIf::Db_Uninit(const std::string& task_id, int task_instance) {
    impl_->Db_Uninit(task_id, task_instance);
}

void ThriftIf::Db_UninitUnlocked(const std::string& task_id,
        int task_instance) {
    impl_->Db_UninitUnlocked(task_id, task_instance);
}

void ThriftIf::Db_SetInitDone(bool init_done) {
    impl_->Db_SetInitDone(init_done);
}

// Tablespace
bool ThriftIf::Db_SetTablespace(const std::string& tablespace) {
    return impl_->Db_SetTablespace(tablespace);
}

bool ThriftIf::Db_AddSetTablespace(const std::string& tablespace,
        const std::string& replication_factor) {
    return impl_->Db_AddSetTablespace(tablespace, replication_factor);
}

// Column family
bool ThriftIf::Db_AddColumnfamily(const GenDb::NewCf& cf) {
    return impl_->Db_AddColumnfamily(cf);
}

bool ThriftIf::Db_UseColumnfamily(const GenDb::NewCf& cf) {
    return impl_->Db_UseColumnfamily(cf);
}

// Column
bool ThriftIf::Db_AddColumn(std::auto_ptr<GenDb::ColList> cl) {
    return impl_->Db_AddColumn(cl);
}

bool ThriftIf::Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl) {
    return impl_->Db_AddColumnSync(cl);
}

// Read
bool ThriftIf::Db_GetRow(GenDb::ColList *ret, const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey) {
    return impl_->Db_GetRow(ret, cfname, rowkey);
}

bool ThriftIf::Db_GetMultiRow(GenDb::ColListVec *ret,
        const std::string& cfname,
        const std::vector<GenDb::DbDataValueVec>& key,
        const GenDb::ColumnNameRange& crange) {
    return impl_->Db_GetMultiRow(ret, cfname, key, crange);
}

bool ThriftIf::Db_GetMultiRow(GenDb::ColListVec *ret,
        const std::string& cfname,
        const std::vector<GenDb::DbDataValueVec>& key) {
    return impl_->Db_GetMultiRow(ret, cfname, key);
}

// Queue
bool ThriftIf::Db_GetQueueStats(uint64_t *queue_count,
        uint64_t *enqueues) const {
    return impl_->Db_GetQueueStats(queue_count, enqueues);
}

void ThriftIf::Db_SetQueueWaterMark(bool high, size_t queue_count,
        GenDb::GenDbIf::DbQueueWaterMarkCb cb) {
    impl_->Db_SetQueueWaterMark(high, queue_count, cb);
}

void ThriftIf::Db_ResetQueueWaterMarks() {
    impl_->Db_ResetQueueWaterMarks();
}

// Stats
bool ThriftIf::Db_GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) {
    return impl_->Db_GetStats(vdbti, dbe);
}

// Connection
std::vector<GenDb::Endpoint> ThriftIf::Db_GetEndpoints() const {
    return impl_->Db_GetEndpoints();
}
