//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_CQL_CQL_LIB_IF_H_
#define DATABASE_CASSANDRA_CQL_CQL_LIB_IF_H_

#include <cassandra.h>

namespace cass {
namespace cql {
namespace interface {

class CassLibrary {
 public:
    virtual ~CassLibrary() {}

    // CassCluster
    virtual CassCluster* CassClusterNew() = 0;
    virtual void CassClusterFree(CassCluster* cluster) = 0;
    virtual CassError CassClusterSetContactPoints(CassCluster* cluster,
        const char* contact_points) = 0;
    virtual CassError CassClusterSetPort(CassCluster* cluster,
        int port) = 0;
    virtual void CassClusterSetCredentials(CassCluster* cluster,
        const char* username, const char* password) = 0;
    virtual CassError CassClusterSetNumThreadsIo(CassCluster* cluster,
        unsigned num_threads) = 0;
    virtual CassError CassClusterSetPendingRequestsHighWaterMark(
        CassCluster* cluster, unsigned num_requests) = 0;
    virtual CassError CassClusterSetPendingRequestsLowWaterMark(
        CassCluster* cluster, unsigned num_requests) = 0;
    virtual CassError CassClusterSetWriteBytesHighWaterMark(
        CassCluster* cluster, unsigned num_bytes) = 0;
    virtual CassError CassClusterSetWriteBytesLowWaterMark(
        CassCluster* cluster, unsigned num_bytes) = 0;
    virtual void CassClusterSetWhitelistFiltering(CassCluster* cluster,
        const char* hosts) = 0;

    // CassSession
    virtual CassSession* CassSessionNew() = 0;
    virtual void CassSessionFree(CassSession* session) = 0;
    virtual CassFuture* CassSessionConnect(CassSession* session,
        const CassCluster* cluster) = 0;
    virtual CassFuture* CassSessionClose(CassSession* session) = 0;
    virtual CassFuture* CassSessionExecute(CassSession* session,
        const CassStatement* statement) = 0;
    virtual const CassSchemaMeta* CassSessionGetSchemaMeta(
        const CassSession* session) = 0;
    virtual CassFuture* CassSessionPrepare(CassSession* session,
        const char* query) = 0;
    virtual void CassSessionGetMetrics(const CassSession* session,
        CassMetrics* output) = 0;

    // CassSchema
    virtual void CassSchemaMetaFree(const CassSchemaMeta* schema_meta) = 0;
    virtual const CassKeyspaceMeta* CassSchemaMetaKeyspaceByName(
        const CassSchemaMeta* schema_meta, const char* keyspace) = 0;
    virtual const CassTableMeta* CassKeyspaceMetaTableByName(
        const CassKeyspaceMeta* keyspace_meta, const char* table) = 0;
    virtual size_t CassTableMetaPartitionKeyCount(
        const CassTableMeta* table_meta) = 0;
    virtual size_t CassTableMetaClusteringKeyCount(
        const CassTableMeta* table_meta) = 0;

    // CassFuture
    virtual void CassFutureFree(CassFuture* future) = 0;
    virtual CassError CassFutureSetCallback(CassFuture* future,
        CassFutureCallback callback, void* data) = 0;
    virtual void CassFutureWait(CassFuture* future) = 0;
    virtual const CassResult* CassFutureGetResult(CassFuture* future) = 0;
    virtual void CassFutureErrorMessage(CassFuture* future,
        const char** message, size_t* message_length) = 0;
    virtual CassError CassFutureErrorCode(CassFuture* future) = 0;
    virtual const CassPrepared* CassFutureGetPrepared(CassFuture* future) = 0;

    // CassResult
    virtual void CassResultFree(const CassResult* result) = 0;
    virtual size_t CassResultColumnCount(const CassResult* result) = 0;
    virtual CassError CassResultColumnName(const CassResult *result,
        size_t index, const char** name, size_t* name_length) = 0;

    // CassIterator
    virtual void CassIteratorFree(CassIterator* iterator) = 0;
    virtual CassIterator* CassIteratorFromResult(
        const CassResult* result) = 0;
    virtual cass_bool_t CassIteratorNext(CassIterator* iterator) = 0;
    virtual const CassRow* CassIteratorGetRow(
        const CassIterator* iterator) = 0;

    // CassStatement
    virtual CassStatement* CassStatementNew(const char* query,
        size_t parameter_count) = 0;
    virtual void CassStatementFree(CassStatement* statement) = 0;
    virtual CassError CassStatementSetConsistency(CassStatement* statement,
        CassConsistency consistency) = 0;
    virtual CassError CassStatementBindStringN(CassStatement* statement,
        size_t index, const char* value, size_t value_length) = 0;
    virtual CassError CassStatementBindInt32(CassStatement* statement,
        size_t index, cass_int32_t value) = 0;
    virtual CassError CassStatementBindInt64(CassStatement* statement,
        size_t index, cass_int64_t value) = 0;
    virtual CassError CassStatementBindUuid(CassStatement* statement,
        size_t index, CassUuid value) = 0;
    virtual CassError CassStatementBindDouble(CassStatement* statement,
        size_t index, cass_double_t value) = 0;
    virtual CassError CassStatementBindInet(CassStatement* statement,
        size_t index, CassInet value) = 0;
    virtual CassError CassStatementBindBytes(CassStatement* statement,
        size_t index, const cass_byte_t* value, size_t value_length) = 0;
    virtual CassError CassStatementBindStringByNameN(CassStatement* statement,
        const char* name, size_t name_length, const char* value,
        size_t value_length) = 0;
    virtual CassError CassStatementBindInt32ByName(CassStatement* statement,
        const char* name, cass_int32_t value) = 0;
    virtual CassError CassStatementBindInt64ByName(CassStatement* statement,
        const char* name, cass_int64_t value) = 0;
    virtual CassError CassStatementBindUuidByName(CassStatement* statement,
        const char* name, CassUuid value) = 0;
    virtual CassError CassStatementBindDoubleByName(CassStatement* statement,
        const char* name, cass_double_t value) = 0;
    virtual CassError CassStatementBindInetByName(CassStatement* statement,
        const char* name, CassInet value) = 0;
    virtual CassError CassStatementBindBytesByNameN(CassStatement* statement,
        const char* name, size_t name_length, const cass_byte_t* value,
        size_t value_length) = 0;

    // CassPrepare
    virtual void CassPreparedFree(const CassPrepared* prepared) = 0;
    virtual CassStatement* CassPreparedBind(const CassPrepared* prepared) = 0;

    // CassValue
    virtual CassValueType GetCassValueType(const CassValue* value) = 0;
    virtual CassError CassValueGetString(const CassValue* value,
        const char** output, size_t* output_size) = 0;
    virtual CassError CassValueGetInt8(const CassValue* value,
        cass_int8_t* output) = 0;
    virtual CassError CassValueGetInt16(const CassValue* value,
        cass_int16_t* output) = 0;
    virtual CassError CassValueGetInt32(const CassValue* value,
        cass_int32_t* output) = 0;
    virtual CassError CassValueGetInt64(const CassValue* value,
        cass_int64_t* output) = 0;
    virtual CassError CassValueGetUuid(const CassValue* value,
        CassUuid* output) = 0;
    virtual CassError CassValueGetDouble(const CassValue* value,
        cass_double_t* output) = 0;
    virtual CassError CassValueGetInet(const CassValue* value,
        CassInet* output) = 0;
    virtual CassError CassValueGetBytes(const CassValue* value,
        const cass_byte_t** output, size_t* output_size) = 0;
    virtual cass_bool_t CassValueIsNull(const CassValue* value) = 0;

    // CassInet
    virtual CassInet CassInetInitV4(const cass_uint8_t* address) = 0;
    virtual CassInet CassInetInitV6(const cass_uint8_t* address) = 0;

    // CassRow
    virtual const CassValue* CassRowGetColumn(const CassRow* row,
        size_t index) = 0;

    // CassLog
    virtual void CassLogSetLevel(CassLogLevel log_level) = 0;
    virtual void CassLogSetCallback(CassLogCallback callback, void* data) = 0;
};

class CassDatastaxLibrary : public CassLibrary {
 public:
    CassDatastaxLibrary();
    virtual ~CassDatastaxLibrary();

    // CassCluster
    virtual CassCluster* CassClusterNew();
    virtual void CassClusterFree(CassCluster* cluster);
    virtual CassError CassClusterSetContactPoints(CassCluster* cluster,
        const char* contact_points);
    virtual CassError CassClusterSetPort(CassCluster* cluster,
        int port);
    virtual void CassClusterSetCredentials(CassCluster* cluster,
        const char* username, const char* password);
    virtual CassError CassClusterSetNumThreadsIo(CassCluster* cluster,
        unsigned num_threads);
    virtual CassError CassClusterSetPendingRequestsHighWaterMark(
        CassCluster* cluster, unsigned num_requests);
    virtual CassError CassClusterSetPendingRequestsLowWaterMark(
        CassCluster* cluster, unsigned num_requests);
    virtual CassError CassClusterSetWriteBytesHighWaterMark(
        CassCluster* cluster, unsigned num_bytes);
    virtual CassError CassClusterSetWriteBytesLowWaterMark(
        CassCluster* cluster, unsigned num_bytes);
    virtual void CassClusterSetWhitelistFiltering(CassCluster* cluster,
        const char* hosts);

    // CassSession
    virtual CassSession* CassSessionNew();
    virtual void CassSessionFree(CassSession* session);
    virtual CassFuture* CassSessionConnect(CassSession* session,
        const CassCluster* cluster);
    virtual CassFuture* CassSessionClose(CassSession* session);
    virtual CassFuture* CassSessionExecute(CassSession* session,
        const CassStatement* statement);
    virtual const CassSchemaMeta* CassSessionGetSchemaMeta(
        const CassSession* session);
    virtual CassFuture* CassSessionPrepare(CassSession* session,
        const char* query);
    virtual void CassSessionGetMetrics(const CassSession* session,
        CassMetrics* output);

    // CassSchema
    virtual void CassSchemaMetaFree(const CassSchemaMeta* schema_meta);
    virtual const CassKeyspaceMeta* CassSchemaMetaKeyspaceByName(
        const CassSchemaMeta* schema_meta, const char* keyspace);
    virtual const CassTableMeta* CassKeyspaceMetaTableByName(
        const CassKeyspaceMeta* keyspace_meta, const char* table);
    virtual size_t CassTableMetaPartitionKeyCount(
        const CassTableMeta* table_meta);
    virtual size_t CassTableMetaClusteringKeyCount(
        const CassTableMeta* table_meta);

    // CassFuture
    virtual void CassFutureFree(CassFuture* future);
    virtual CassError CassFutureSetCallback(CassFuture* future,
        CassFutureCallback callback, void* data);
    virtual void CassFutureWait(CassFuture* future);
    virtual const CassResult* CassFutureGetResult(CassFuture* future);
    virtual void CassFutureErrorMessage(CassFuture* future,
        const char** message, size_t* message_length);
    virtual CassError CassFutureErrorCode(CassFuture* future);
    virtual const CassPrepared* CassFutureGetPrepared(CassFuture* future);

    // CassResult
    virtual void CassResultFree(const CassResult* result);
    virtual size_t CassResultColumnCount(const CassResult* result);
    virtual CassError CassResultColumnName(const CassResult *result,
        size_t index, const char** name, size_t* name_length);

    // CassIterator
    virtual void CassIteratorFree(CassIterator* iterator);
    virtual CassIterator* CassIteratorFromResult(
        const CassResult* result);
    virtual cass_bool_t CassIteratorNext(CassIterator* iterator);
    virtual const CassRow* CassIteratorGetRow(
        const CassIterator* iterator);

    // CassStatement
    virtual CassStatement* CassStatementNew(const char* query,
        size_t parameter_count);
    virtual void CassStatementFree(CassStatement* statement);
    virtual CassError CassStatementSetConsistency(CassStatement* statement,
        CassConsistency consistency);
    virtual CassError CassStatementBindStringN(CassStatement* statement,
        size_t index, const char* value, size_t value_length);
    virtual CassError CassStatementBindInt32(CassStatement* statement,
        size_t index, cass_int32_t value);
    virtual CassError CassStatementBindInt64(CassStatement* statement,
        size_t index, cass_int64_t value);
    virtual CassError CassStatementBindUuid(CassStatement* statement,
        size_t index, CassUuid value);
    virtual CassError CassStatementBindDouble(CassStatement* statement,
        size_t index, cass_double_t value);
    virtual CassError CassStatementBindInet(CassStatement* statement,
        size_t index, CassInet value);
    virtual CassError CassStatementBindBytes(CassStatement* statement,
        size_t index, const cass_byte_t* value, size_t value_length);
    virtual CassError CassStatementBindStringByNameN(CassStatement* statement,
        const char* name, size_t name_length, const char* value,
        size_t value_length);
    virtual CassError CassStatementBindInt32ByName(CassStatement* statement,
        const char* name, cass_int32_t value);
    virtual CassError CassStatementBindInt64ByName(CassStatement* statement,
        const char* name, cass_int64_t value);
    virtual CassError CassStatementBindUuidByName(CassStatement* statement,
        const char* name, CassUuid value);
    virtual CassError CassStatementBindDoubleByName(CassStatement* statement,
        const char* name, cass_double_t value);
    virtual CassError CassStatementBindInetByName(CassStatement* statement,
        const char* name, CassInet value);
    virtual CassError CassStatementBindBytesByNameN(CassStatement* statement,
        const char* name, size_t name_length, const cass_byte_t* value,
        size_t value_length);

    // CassPrepare
    virtual void CassPreparedFree(const CassPrepared* prepared);
    virtual CassStatement* CassPreparedBind(const CassPrepared* prepared);

    // CassValue
    virtual CassValueType GetCassValueType(const CassValue* value);
    virtual CassError CassValueGetString(const CassValue* value,
        const char** output, size_t* output_size);
    virtual CassError CassValueGetInt8(const CassValue* value,
        cass_int8_t* output);
    virtual CassError CassValueGetInt16(const CassValue* value,
        cass_int16_t* output);
    virtual CassError CassValueGetInt32(const CassValue* value,
        cass_int32_t* output);
    virtual CassError CassValueGetInt64(const CassValue* value,
        cass_int64_t* output);
    virtual CassError CassValueGetUuid(const CassValue* value,
        CassUuid* output);
    virtual CassError CassValueGetDouble(const CassValue* value,
        cass_double_t* output);
    virtual CassError CassValueGetInet(const CassValue* value,
        CassInet* output);
    virtual CassError CassValueGetBytes(const CassValue* value,
        const cass_byte_t** output, size_t* output_size);
    virtual cass_bool_t CassValueIsNull(const CassValue* value);

    // CassInet
    virtual CassInet CassInetInitV4(const cass_uint8_t* address);
    virtual CassInet CassInetInitV6(const cass_uint8_t* address);

    // CassRow
    virtual const CassValue* CassRowGetColumn(const CassRow* row,
        size_t index);

    // CassLog
    virtual void CassLogSetLevel(CassLogLevel log_level);
    virtual void CassLogSetCallback(CassLogCallback callback, void* data);
};

}  // namespace interface
}  // namespace cql
}  // namespace cass

#endif  // DATABASE_CASSANDRA_CQL_CQL_LIB_IF_H_
