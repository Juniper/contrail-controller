//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_
#define DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_

#include <string>

#include <boost/unordered_map.hpp>

#include <cassandra.h>

#include <io/event_manager.h>
#include <base/timer.h>
#include <sandesh/sandesh.h>
#include <database/gendb_if.h>
#include <database/cassandra/cql/cql_types.h>
#include <database/cassandra/cql/cql_lib_if.h>

namespace cass {
namespace cql {
namespace impl {

std::string StaticCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf,
    const std::string &compaction_strategy);
std::string DynamicCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf,
    const std::string &compaction_strategy);
std::string StaticCf2CassInsertIntoTable(const GenDb::ColList *v_columns);
std::string DynamicCf2CassInsertIntoTable(const GenDb::ColList *v_columns);
std::string StaticCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf);
std::string DynamicCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf);
std::string CassSelectFromTable(const std::string &table);
std::string PartitionKey2CassSelectFromTable(const std::string &table,
    const GenDb::DbDataValueVec &rkeys);
std::string PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
    const std::string &table, const GenDb::DbDataValueVec &rkeys,
    const GenDb::ColumnNameRange &crange,
    const GenDb::FieldNamesToReadVec &read_vec = GenDb::FieldNamesToReadVec());
std::string PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
    const std::string &table, const std::vector<GenDb::DbDataValueVec> &rkeys,
    const GenDb::ColumnNameRange &crange,
    const GenDb::FieldNamesToReadVec &read_vec = GenDb::FieldNamesToReadVec());

// CQL Library Shared Pointers to handle library free calls
template<class T>
struct Deleter;

template<>
struct Deleter<CassCluster> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(CassCluster *ptr) {
        if (ptr != NULL) {
            cci_->CassClusterFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<CassSession> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(CassSession* ptr) {
        if (ptr != NULL) {
            cci_->CassSessionFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<CassFuture> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(CassFuture* ptr) {
        if (ptr != NULL) {
            cci_->CassFutureFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<CassStatement> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(CassStatement* ptr) {
        if (ptr != NULL) {
            cci_->CassStatementFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<const CassResult> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(const CassResult* ptr) {
        if (ptr != NULL) {
            cci_->CassResultFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<CassIterator> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(CassIterator* ptr) {
        if (ptr != NULL) {
            cci_->CassIteratorFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<const CassPrepared> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(const CassPrepared* ptr) {
        if (ptr != NULL) {
            cci_->CassPreparedFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template<>
struct Deleter<const CassSchemaMeta> {
    Deleter(interface::CassLibrary *cci) :
       cci_(cci) {}
    void operator()(const CassSchemaMeta* ptr) {
        if (ptr != NULL) {
            cci_->CassSchemaMetaFree(ptr);
        }
    }
    interface::CassLibrary *cci_;
};

template <class T>
class CassSharedPtr : public boost::shared_ptr<T> {
 public:
    CassSharedPtr(T* ptr, interface::CassLibrary *cci) :
    boost::shared_ptr<T>(ptr, Deleter<T>(cci)) {}
};

typedef CassSharedPtr<CassCluster> CassClusterPtr;
typedef CassSharedPtr<CassSession> CassSessionPtr;
typedef CassSharedPtr<CassFuture> CassFuturePtr;
typedef CassSharedPtr<CassStatement> CassStatementPtr;
typedef CassSharedPtr<const CassResult> CassResultPtr;
typedef CassSharedPtr<CassIterator> CassIteratorPtr;
typedef CassSharedPtr<const CassPrepared> CassPreparedPtr;
typedef CassSharedPtr<const CassSchemaMeta> CassSchemaMetaPtr;

typedef boost::function<void(GenDb::DbOpResult::type,
    std::auto_ptr<GenDb::ColList>)> CassAsyncQueryCallback;

struct CassQueryResultContext {
    CassQueryResultContext(const std::string &cf_name, bool is_dynamic_cf,
        const GenDb::DbDataValueVec &row_key, size_t rk_count = 0,
        size_t ck_count = 0) :
        cf_name_(cf_name),
        is_dynamic_cf_(is_dynamic_cf),
        row_key_(row_key),
        rk_count_(rk_count),
        ck_count_(ck_count) {
    }
    std::string cf_name_;
    bool is_dynamic_cf_;
    GenDb::DbDataValueVec row_key_;
    size_t rk_count_;
    size_t ck_count_;
};

struct CassAsyncQueryContext {
    CassAsyncQueryContext(const char *query_id, CassAsyncQueryCallback cb,
        interface::CassLibrary *cci, CassQueryResultContext *rctx = NULL) :
        query_id_(query_id),
        cb_(cb),
        cci_(cci),
        result_ctx_(rctx) {
    }
    std::string query_id_;
    CassAsyncQueryCallback cb_;
    interface::CassLibrary *cci_;
    boost::scoped_ptr<CassQueryResultContext> result_ctx_;
};

void DynamicCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, size_t rk_count,
    size_t ck_count, GenDb::ColListVec *v_col_list);
void StaticCfGetResult(interface::CassLibrary *cci,
    CassResultPtr *result, size_t rk_count,
    GenDb::ColListVec *v_col_list);

}  // namespace impl

//
// CqlIfImpl
//
class CqlIfImpl {
 public:
    CqlIfImpl(EventManager *evm,
        const std::vector<std::string> &cassandra_ips,
        int cassandra_port,
        const std::string &cassandra_user,
        const std::string &cassandra_password,
        interface::CassLibrary *cci);
    virtual ~CqlIfImpl();

    bool CreateKeyspaceIfNotExistsSync(const std::string &keyspace,
        const std::string &replication_factor, CassConsistency consistency);
    bool UseKeyspaceSync(const std::string &keyspace,
        CassConsistency consistency);

    bool CreateTableIfNotExistsSync(const GenDb::NewCf &cf,
        const std::string &compaction_strategy, CassConsistency consistency);
    bool LocatePrepareInsertIntoTable(const GenDb::NewCf &cf);
    bool IsTablePresent(const std::string &table);
    bool IsTableStatic(const std::string &table);
    bool IsTableDynamic(const std::string &table);

    bool InsertIntoTableSync(std::auto_ptr<GenDb::ColList> v_columns,
        CassConsistency consistency);
    bool InsertIntoTableAsync(std::auto_ptr<GenDb::ColList> v_columns,
        CassConsistency consistency, impl::CassAsyncQueryCallback cb);
    bool InsertIntoTablePrepareAsync(std::auto_ptr<GenDb::ColList> v_columns,
        CassConsistency consistency, impl::CassAsyncQueryCallback cb);
    bool IsInsertIntoTablePrepareSupported(const std::string &table);

    bool SelectFromTableSync(const std::string &cfname,
        const GenDb::DbDataValueVec &rkey, CassConsistency consistency,
        GenDb::NewColVec *out);
    bool SelectFromTableSync(const std::string &cfname,
        CassConsistency consistency, GenDb::ColListVec *out);
    bool SelectFromTableClusteringKeyRangeSync(const std::string &cfname,
        const GenDb::DbDataValueVec &rkey,
        const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
        GenDb::NewColVec *out);
    bool SelectFromTableClusteringKeyRangeFieldNamesSync(const std::string &cfname,
        const GenDb::DbDataValueVec &rkey,
        const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
        const GenDb::FieldNamesToReadVec &read_vec,
        GenDb::NewColVec *out);

    bool SelectFromTableClusteringKeyRangeFieldNamesSync(const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &rkeys,
        const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
        const GenDb::FieldNamesToReadVec &read_vec,
        GenDb::ColListVec *out);

    bool SelectFromTableAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rkey, CassConsistency consistency,
        cass::cql::impl::CassAsyncQueryCallback cb);
    bool SelectFromTableClusteringKeyRangeAsync(const std::string &cfname,
        const GenDb::DbDataValueVec &rkey,
        const GenDb::ColumnNameRange &ck_range, CassConsistency consistency,
        cass::cql::impl::CassAsyncQueryCallback cb);

    void ConnectAsync();
    bool ConnectSync();
    void DisconnectAsync();
    bool DisconnectSync();

    void GetMetrics(Metrics *metrics) const;

 private:
    typedef boost::function<void(CassFuture *)> ConnectCbFn;
    typedef boost::function<void(CassFuture *)> DisconnectCbFn;

    static void ConnectCallback(CassFuture *future, void *data);
    static void DisconnectCallback(CassFuture *future, void *data);
    bool ReconnectTimerExpired();
    void ReconnectTimerErrorHandler(std::string error_name,
        std::string error_message);
    void ConnectCallbackProcess(CassFuture *future);
    void DisconnectCallbackProcess(CassFuture *future);

    bool InsertIntoTableInternal(std::auto_ptr<GenDb::ColList> v_columns,
        CassConsistency consistency, bool sync,
        impl::CassAsyncQueryCallback cb);
    bool GetPrepareInsertIntoTable(const std::string &table_name,
        impl::CassPreparedPtr *prepared) const;
    bool PrepareInsertIntoTableSync(const GenDb::NewCf &cf,
        impl::CassPreparedPtr *prepared);
    bool InsertIntoTablePrepareInternal(std::auto_ptr<GenDb::ColList> v_columns,
        CassConsistency consistency, bool sync,
        impl::CassAsyncQueryCallback cb);

    static const char * kQCreateKeyspaceIfNotExists;
    static const char * kQUseKeyspace;
    static const char * kTaskName;
    static const int kTaskInstance = -1;
    static const int kReconnectInterval = 5 * 1000;

    struct SessionState {
        enum type {
            INIT,
            CONNECT_PENDING,
            CONNECTED,
            DISCONNECT_PENDING,
            DISCONNECTED,
        };
    };

    EventManager *evm_;
    interface::CassLibrary *cci_;
    impl::CassClusterPtr cluster_;
    impl::CassSessionPtr session_;
    tbb::atomic<SessionState::type> session_state_;
    Timer *reconnect_timer_;
    ConnectCbFn connect_cb_;
    DisconnectCbFn disconnect_cb_;
    std::string keyspace_;
    int io_thread_count_;
    typedef boost::unordered_map<std::string, impl::CassPreparedPtr>
        CassPreparedMapType;
    CassPreparedMapType insert_prepared_map_;
    mutable tbb::mutex map_mutex_;
};

}  // namespace cql
}  // namespace cass

#endif // DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_
