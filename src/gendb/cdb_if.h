/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CDB_IF_H__
#define __CDB_IF_H__

#include "gendb_if.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include <tbb/task.h>
#include <tbb/mutex.h>

#include <protocol/TBinaryProtocol.h>
#include <transport/TSocket.h>
#include <transport/TTransportUtils.h>
#include "gen-cpp/Cassandra.h"

#include "base/logging.h"
#include "base/queue_task.h"
#include "base/timer.h"

#define CDBIF_HANDLE_EXCEPTION_RETF(msg) \
    LOG(ERROR, msg); \
    return false;

#define CDBIF_HANDLE_EXCEPTION(msg) LOG(ERROR, msg)

#define CDBIF_CONDCHECK_LOG_RETF(cond) \
    if (!(cond)) { \
        LOG(ERROR, __FILE__ << ":" << __LINE__ << ": cond: (" << #cond << ") failed"); \
        return false; \
    }

#define CDBIF_CONDCHECK_LOG(cond) \
    if (!(cond)) \
        LOG(ERROR, __FILE__ << ":" << __LINE__ << ": cond: (" << #cond << ") failed");

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

using namespace ::std;
using namespace ::boost;

using namespace ::org::apache::cassandra;

using tbb::task;

using namespace GenDb;
using GenDb::GenDbIf;
using GenDb::DbDataValue;

class CdbIf : public GenDbIf {
    public:
        CdbIf(boost::asio::io_service *, DbErrorHandler, std::string, unsigned short, int ttl,
                std::string name);
        CdbIf();
        ~CdbIf();

        virtual bool Db_Init(std::string task_id, int task_instance);
        virtual void Db_Uninit(bool shutdown);
        virtual void Db_SetInitDone(bool);
        virtual bool Db_AddTablespace(const std::string& tablespace,const std::string& replication_factor);
        virtual bool Db_SetTablespace(const std::string& tablespace);
        virtual bool Db_AddSetTablespace(const std::string& tablespace,const std::string& replication_factor = "1");
        virtual bool Db_FindTablespace(const std::string& tablespace);
        /* api to add a column family in the current table space */
        virtual bool NewDb_AddColumnfamily(const GenDb::NewCf& cf);

        virtual bool Db_UseColumnfamily(const GenDb::NewCf& cf);

        /* api to add a column in the current table space */
        virtual bool NewDb_AddColumn(std::auto_ptr<GenDb::ColList> cl);
        virtual bool AddColumnSync(std::auto_ptr<GenDb::ColList> cl);

        virtual bool Db_GetRow(GenDb::ColList& ret, const std::string& cfname,
                const GenDb::DbDataValueVec& rowkey);
        virtual bool Db_GetMultiRow(std::vector<GenDb::ColList>& ret,
                const std::string& cfname, const std::vector<GenDb::DbDataValueVec>& key,
                GenDb::ColumnNameRange *crange_ptr = NULL);
        /* api to get range of column data for a range of rows */
        bool Db_GetRangeSlices(GenDb::ColList& col_list,
                const std::string& cfname,
                const GenDb::ColumnNameRange& crange,
                const GenDb::DbDataValueVec& key);
        virtual bool Db_GetQueueStats(uint64_t &queue_count, uint64_t &enqueues) const;
        virtual void Db_SetQueueWaterMark(bool high, size_t queue_count, DbQueueWaterMarkCb cb);
        virtual void Db_ResetQueueWaterMarks();

    private:
        friend class CdbIfTest;

        /* api to get range of column data for a range of rows 
         * Number of columns returned is less than or equal to count field
         * in crange
         */
        bool Db_GetRangeSlices_Internal(GenDb::ColList& col_list,
                const std::string& cfname,
                const GenDb::ColumnNameRange& crange,
                const GenDb::DbDataValueVec& key);


        static const int max_query_rows = 5000;
        static const int PeriodicTimeSec = 10;

        typedef boost::function<std::string(const DbDataValue&)> Db_encode_composite_fn;
        typedef boost::function<DbDataValue(const char *input, int& used)> Db_decode_composite_fn;
        typedef boost::function<std::string(const DbDataValue&)> Db_encode_non_composite_fn;
        typedef boost::function<DbDataValue(const std::string&)> Db_decode_non_composite_fn;
        struct CdbIfTypeInfo {
            CdbIfTypeInfo(std::string cassandra_type,
                    Db_encode_composite_fn encode_composite_fn,
                    Db_decode_composite_fn decode_composite_fn,
                    Db_encode_non_composite_fn encode_non_composite_fn,
                    Db_decode_non_composite_fn decode_non_composite_fn) :
                cassandra_type_(cassandra_type),
                encode_composite_fn_(encode_composite_fn),
                decode_composite_fn_(decode_composite_fn),
                encode_non_composite_fn_(encode_non_composite_fn),
                decode_non_composite_fn_(decode_non_composite_fn) {
            }

            std::string cassandra_type_;
            Db_encode_composite_fn encode_composite_fn_;
            Db_decode_composite_fn decode_composite_fn_;
            Db_encode_non_composite_fn encode_non_composite_fn_;
            Db_decode_non_composite_fn decode_non_composite_fn_;
        };
        typedef std::map<GenDb::DbDataType::type, CdbIfTypeInfo> CdbIfTypeMapDef;
        static CdbIfTypeMapDef CdbIfTypeMap;

        struct CdbIfCfInfo {
            CdbIfCfInfo() {
            }
            CdbIfCfInfo(CfDef *cfdef) {
                cfdef_.reset(cfdef);
            }
            CdbIfCfInfo(CfDef *cfdef, GenDb::NewCf *cf) {
                cfdef_.reset(cfdef);
                cf_.reset(cf);
            }
            ~CdbIfCfInfo() {
            }
            std::auto_ptr<CfDef> cfdef_;
            std::auto_ptr<GenDb::NewCf> cf_;
        };
        typedef boost::ptr_map<std::string, CdbIfCfInfo> CdbIfCfListType;
        CdbIfCfListType CdbIfCfList;

        /*
         * structure for passing between sync and async add_column
         */
        struct CdbIfColList {
            CdbIfColList(std::auto_ptr<GenDb::ColList> cl) : new_cl(cl) { }

            std::auto_ptr<GenDb::ColList> new_cl;
        };

        bool DbDataTypeVecToCompositeType(std::string& res, const std::vector<GenDb::DbDataType::type>& db_type);
        bool ConstructDbDataValue(std::string& res, const std::string& cfname,
                const std::string& col_name, const GenDb::DbDataValue& value);
        bool ConstructDbDataValueKey(std::string& res, const std::string& cfname,
                const GenDb::DbDataValueVec& rowkey);
        bool ConstructDbDataValueColumnName(std::string& res, const std::string& cfname, const GenDb::DbDataValueVec& name);
        bool ConstructDbDataValueColumnValue(std::string& res, const std::string& cfname, const GenDb::DbDataValueVec& value);
        bool DbDataValueFromType(GenDb::DbDataValue& res, const GenDb::DbDataType::type& type, const std::string& input);
        bool DbDataValueFromType(GenDb::DbDataValue&, const string&, const string&);
        bool DbDataValueFromString(GenDb::DbDataValue&, const string&, const string&, const string&);
        bool DbDataValueToString(std::string&, const GenDb::DbDataType::type&, const DbDataValue&);
        bool DbDataValueToStringFromCf(std::string&, const string&, const string&, const DbDataValue&);
        bool DbDataValueVecToString(std::string&, const DbDataTypeVec&, const DbDataValueVec&);
        bool DbDataValueVecFromString(GenDb::DbDataValueVec&, const DbDataTypeVec&, const string&);
        bool ColListFromColumnOrSuper(GenDb::ColList&, std::vector<org::apache::cassandra::ColumnOrSuperColumn>&, const string&);

        bool Db_AsyncAddColumn(CdbIfColList *cl);
        bool Db_Columnfamily_present(const std::string& cfname);
        bool Db_GetColumnfamily(CdbIfCfInfo **info, const std::string& cfname);
        bool Db_IsInitDone() const;
        bool Db_FindColumnfamily(const std::string& cfname);

        /* encode/decode for non-composite */
        static std::string Db_encode_string_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_string_non_composite(const std::string& input);
        static std::string Db_encode_Unsigned8_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned8_non_composite(const std::string& input);
        static std::string Db_encode_Unsigned16_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned16_non_composite(const std::string& input);
        static std::string Db_encode_Unsigned32_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned32_non_composite(const std::string& input);
        static std::string Db_encode_Unsigned64_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned64_non_composite(const std::string& input);
        static std::string Db_encode_Double_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Double_non_composite(const std::string& input);
        static std::string Db_encode_UUID_non_composite(const DbDataValue& value);
        static DbDataValue Db_decode_UUID_non_composite(const std::string& input);

        /* encode/decode for composite */
        static std::string Db_encode_string_composite(const DbDataValue& value);
        static DbDataValue Db_decode_string_composite(const char *input, int& used);
        static std::string Db_encode_Unsigned_int_composite(uint64_t input);
        static std::string Db_encode_Unsigned8_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned8_composite(const char *input, int& used);
        static std::string Db_encode_Unsigned16_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned16_composite(const char *input, int& used);
        static std::string Db_encode_Unsigned32_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned32_composite(const char *input, int& used);
        static std::string Db_encode_Unsigned64_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Unsigned64_composite(const char *input, int& used);
        static std::string Db_encode_Double_composite(const DbDataValue& value);
        static DbDataValue Db_decode_Double_composite(const char *input, int& used);
        static std::string Db_encode_UUID_composite(const DbDataValue& value);
        static DbDataValue Db_decode_UUID_composite(const char *input, int& used);

        shared_ptr<TTransport> socket_;
        shared_ptr<TTransport> transport_;
        shared_ptr<TProtocol> protocol_;
        boost::scoped_ptr<CassandraClient> client_;
        boost::asio::io_service *ioservice_;
        DbErrorHandler errhandler_;

        bool db_init_done_;
        std::string tablespace_;

        typedef WorkQueue<CdbIfColList *> CdbIfQueue;
        boost::scoped_ptr<CdbIfQueue> cdbq_;
        Timer *periodic_timer_;
        std::string name_;

        int cassandra_ttl_;
};

#endif
