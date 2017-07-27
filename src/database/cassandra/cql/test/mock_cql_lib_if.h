//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_CQL_TEST_MOCK_CQL_LIB_IF_H_
#define DATABASE_CASSANDRA_CQL_TEST_MOCK_CQL_LIB_IF_H_

#include <cassandra.h>
#include <database/cassandra/cql/cql_lib_if.h>

namespace cass {
namespace cql {
namespace test {

class MockCassLibrary : public interface::CassLibrary {
 public:

    // CassCluster
    MOCK_METHOD0(CassClusterNew, CassCluster* ());
    MOCK_METHOD1(CassClusterFree, void (CassCluster* cluster));
    MOCK_METHOD2(CassClusterSetContactPoints, CassError (CassCluster* cluster,
        const char* contact_points));
    MOCK_METHOD2(CassClusterSetPort, CassError (CassCluster* cluster,
        int port));
    MOCK_METHOD3(CassClusterSetCredentials, void (CassCluster* cluster,
        const char* username, const char* password));
    MOCK_METHOD2(CassClusterSetNumThreadsIo, CassError (CassCluster* cluster,
        unsigned num_threads));
    MOCK_METHOD2(CassClusterSetPendingRequestsHighWaterMark, CassError (
        CassCluster* cluster, unsigned num_requests));
    MOCK_METHOD2(CassClusterSetPendingRequestsLowWaterMark, CassError (
        CassCluster* cluster, unsigned num_requests));
    MOCK_METHOD2(CassClusterSetWriteBytesHighWaterMark, CassError (
        CassCluster* cluster, unsigned num_bytes));
    MOCK_METHOD2(CassClusterSetWriteBytesLowWaterMark, CassError (
        CassCluster* cluster, unsigned num_bytes));

    // CassSession
    MOCK_METHOD0(CassSessionNew, CassSession* ());
    MOCK_METHOD1(CassSessionFree, void (CassSession* session));
    MOCK_METHOD2(CassSessionConnect, CassFuture* (CassSession* session,
        const CassCluster* cluster));
    MOCK_METHOD1(CassSessionClose, CassFuture* (CassSession* session));
    MOCK_METHOD2(CassSessionExecute, CassFuture* (CassSession* session,
        const CassStatement* statement));
    MOCK_METHOD1(CassSessionGetSchemaMeta, const CassSchemaMeta* (
        const CassSession* session));
    MOCK_METHOD2(CassSessionPrepare, CassFuture* (CassSession* session,
        const char* query));
    MOCK_METHOD2(CassSessionGetMetrics, void (const CassSession* session,
        CassMetrics* output));

    // CassSchema
    MOCK_METHOD1(CassSchemaMetaFree, void (const CassSchemaMeta* schema_meta));
    MOCK_METHOD2(CassSchemaMetaKeyspaceByName, const CassKeyspaceMeta* (
        const CassSchemaMeta* schema_meta, const char* keyspace));
    MOCK_METHOD2(CassKeyspaceMetaTableByName, const CassTableMeta* (
        const CassKeyspaceMeta* keyspace_meta, const char* table));
    MOCK_METHOD1(CassTableMetaPartitionKeyCount, size_t (
        const CassTableMeta* table_meta));
    MOCK_METHOD1(CassTableMetaClusteringKeyCount, size_t (
        const CassTableMeta* table_meta));

    // CassFuture
    MOCK_METHOD1(CassFutureFree, void (CassFuture* future));
    MOCK_METHOD3(CassFutureSetCallback, CassError (CassFuture* future,
        CassFutureCallback callback, void* data));
    MOCK_METHOD1(CassFutureWait, void (CassFuture* future));
    MOCK_METHOD1(CassFutureGetResult, const CassResult* (CassFuture* future));
    MOCK_METHOD3(CassFutureErrorMessage, void (CassFuture* future,
        const char** message, size_t* message_length));
    MOCK_METHOD1(CassFutureErrorCode, CassError (CassFuture* future));
    MOCK_METHOD1(CassFutureGetPrepared, const CassPrepared* (
        CassFuture* future));

    // CassResult
    MOCK_METHOD1(CassResultFree, void (const CassResult* result));
    MOCK_METHOD1(CassResultColumnCount, size_t (const CassResult* result));
    MOCK_METHOD4(CassResultColumnName, CassError (const CassResult *result,
        size_t index, const char** name, size_t* name_length));

    // CassIterator
    MOCK_METHOD1(CassIteratorFree, void (CassIterator* iterator));
    MOCK_METHOD1(CassIteratorFromResult, CassIterator* (
        const CassResult* result));
    MOCK_METHOD1(CassIteratorNext, cass_bool_t (CassIterator* iterator));
    MOCK_METHOD1(CassIteratorGetRow, const CassRow* (
        const CassIterator* iterator));

    // CassStatement
    MOCK_METHOD2(CassStatementNew, CassStatement* (const char* query,
        size_t parameter_count));
    MOCK_METHOD1(CassStatementFree, void (CassStatement* statement));
    MOCK_METHOD2(CassStatementSetConsistency, CassError (
        CassStatement* statement, CassConsistency consistency));
    MOCK_METHOD2(CassStatementBindNull, CassError (CassStatement* statement,
        size_t index));
    MOCK_METHOD4(CassStatementBindStringN, CassError (CassStatement* statement,
        size_t index, const char* value, size_t value_length));
    MOCK_METHOD3(CassStatementBindInt32, CassError (CassStatement* statement,
        size_t index, cass_int32_t value));
    MOCK_METHOD3(CassStatementBindInt64, CassError (CassStatement* statement,
        size_t index, cass_int64_t value));
    MOCK_METHOD3(CassStatementBindUuid, CassError (CassStatement* statement,
        size_t index, CassUuid value));
    MOCK_METHOD3(CassStatementBindDouble, CassError (CassStatement* statement,
        size_t index, cass_double_t value));
    MOCK_METHOD3(CassStatementBindInet, CassError (CassStatement* statement,
        size_t index, CassInet value));
    MOCK_METHOD4(CassStatementBindBytes, CassError (CassStatement* statement,
        size_t index, const cass_byte_t* value, size_t value_length));
    MOCK_METHOD3(CassStatementBindCollection, CassError (
        CassStatement* statement, size_t index,
        const CassCollection* collection));
    MOCK_METHOD5(CassStatementBindStringByNameN, CassError (
        CassStatement* statement, const char* name, size_t name_length,
        const char* value, size_t value_length));
    MOCK_METHOD3(CassStatementBindInt32ByName, CassError (
        CassStatement* statement, const char* name, cass_int32_t value));
    MOCK_METHOD3(CassStatementBindInt64ByName, CassError (
        CassStatement* statement, const char* name, cass_int64_t value));
    MOCK_METHOD3(CassStatementBindUuidByName, CassError (
        CassStatement* statement, const char* name, CassUuid value));
    MOCK_METHOD3(CassStatementBindDoubleByName, CassError (
        CassStatement* statement, const char* name, cass_double_t value));
    MOCK_METHOD3(CassStatementBindInetByName, CassError (
        CassStatement* statement, const char* name, CassInet value));
    MOCK_METHOD5(CassStatementBindBytesByNameN, CassError (
        CassStatement* statement, const char* name, size_t name_length,
        const cass_byte_t* value, size_t value_length));
    MOCK_METHOD3(CassStatementBindCollectionByName, CassError (
        CassStatement* statement, const char* name,
        const CassCollection* collection));

    // CassPrepare
    MOCK_METHOD1(CassPreparedFree, void (const CassPrepared* prepared));
    MOCK_METHOD1(CassPreparedBind, CassStatement* (
        const CassPrepared* prepared));

    // CassValue
    MOCK_METHOD1(GetCassValueType, CassValueType (const CassValue* value));
    MOCK_METHOD3(CassValueGetString, CassError (const CassValue* value,
        const char** output, size_t* output_size));
    MOCK_METHOD2(CassValueGetInt8, CassError (const CassValue* value,
        cass_int8_t* output));
    MOCK_METHOD2(CassValueGetInt16, CassError (const CassValue* value,
        cass_int16_t* output));
    MOCK_METHOD2(CassValueGetInt32, CassError (const CassValue* value,
        cass_int32_t* output));
    MOCK_METHOD2(CassValueGetInt64, CassError (const CassValue* value,
        cass_int64_t* output));
    MOCK_METHOD2(CassValueGetUuid, CassError (const CassValue* value,
        CassUuid* output));
    MOCK_METHOD2(CassValueGetDouble, CassError (const CassValue* value,
        cass_double_t* output));
    MOCK_METHOD2(CassValueGetInet, CassError (const CassValue* value,
        CassInet* output));
    MOCK_METHOD3(CassValueGetBytes, CassError (const CassValue* value,
        const cass_byte_t** output, size_t* output_size));
    MOCK_METHOD1(CassValueIsNull, cass_bool_t (const CassValue* value));

    // CassCollection
    MOCK_METHOD2(CassCollectionNew, CassCollection* (CassCollectionType type,
        size_t size));
    MOCK_METHOD1(CassCollectionFree, void (CassCollection* collection));
    MOCK_METHOD2(CassCollectionAppendString, CassError (
        CassCollection * collection, const char* str));

    // CassInet
    MOCK_METHOD1(CassInetInitV4, CassInet (const cass_uint8_t* address));
    MOCK_METHOD1(CassInetInitV6, CassInet (const cass_uint8_t* address));

    // CassRow
    MOCK_METHOD2(CassRowGetColumn, const CassValue* (const CassRow* row,
        size_t index));

    // CassLog
    MOCK_METHOD1(CassLogSetLevel, void (CassLogLevel log_level));
    MOCK_METHOD2(CassLogSetCallback, void (CassLogCallback callback,
        void* data));
};

}  // namespace test
}  // namespace cql
}  // namespace cass

#endif  // DATABASE_CASSANDRA_CQL_TEST_MOCK_CQL_LIB_IF_H_
