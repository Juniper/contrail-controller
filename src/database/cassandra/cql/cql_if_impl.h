//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_
#define DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_

#include <string>

#include <database/gendb_if.h>

namespace cass {
namespace cql {
namespace impl {

std::string StaticCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf);
std::string DynamicCf2CassCreateTableIfNotExists(const GenDb::NewCf &cf);
std::string StaticCf2CassInsertIntoTable(const GenDb::ColList *v_columns);
std::string DynamicCf2CassInsertIntoTable(const GenDb::ColList *v_columns);
std::string StaticCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf);
std::string DynamicCf2CassPrepareInsertIntoTable(const GenDb::NewCf &cf);
std::string PartitionKey2CassSelectFromTable(const std::string &table,
    const GenDb::DbDataValueVec &rkeys);
std::string PartitionKeyAndClusteringKeyRange2CassSelectFromTable(
    const std::string &table, const GenDb::DbDataValueVec &rkeys,
    const GenDb::ColumnNameRange &crange);

}  // namespace impl
}  // namespace cql
}  // namespace cass

#endif // DATABASE_CASSANDRA_CQL_CQL_IF_IMPL_H_
