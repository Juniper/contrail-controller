/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __QEOPSERVERPROXY_H__
#define __QEOPSERVERPROXY_H__

#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <boost/variant.hpp>
#include <boost/uuid/uuid.hpp>

class EventManager;
class QueryEngine;
class QueryResultMetaData;
// This class represents the interface between the Query Engine and 
// the OpServer. It will internally talk to the OpServer using Redis

class QEOpServerProxy {
public:
    static const int nMaxChunks = 16;
    
    // This is the client-provided function that will be called when the OpServer
    // issues a query. Besides the QID, the function will get a map ,
    // indexed by field name.
    //
    // The field names are expected to be as follows:
    //   start_time, end_time, from_table, sorted, select_fields, where_and_over_or
    // The values are expected to be in the form of JSON-encoded strings.
    typedef boost::function<bool(void *, std::string qid, std::string startTime,
            std::map<std::string, std::string>)> QECallbackFn;

    // In addition to the Redis parameters and the EventManager,
    // the client should provide a callback function to be called
    // when the OpServer issues a Query.
    QEOpServerProxy(EventManager *evm, QueryEngine *qe,
            const std::string & hostname,
            uint16_t port, int max_chunks = nMaxChunks);
    virtual ~QEOpServerProxy();

    // When the result of a Query is available, the client should
    // call this function with the QID, error code, and a vector of rows.
    // The rows are JSON-encoded query output rows.
    // Do not call QueryResult in the thread of execution of 
    // QECallbackFn.
    //
    // Ownership of the vector is transferred to QEOpServerProxy
    // after this call; the client should not free it.
    //
    // If the error field is non-zero, the vector should be empty
    // Some useful errors:
    //    EBADMSG         Bad message (JSON could not be parsed)
    //    EINVAL          Invalid argument (select/where parameters are invalid)
    //    ENOENT          No such file or directory (Invalid table name)
    //    EIO             Input/output error (Cassandra is down)
    typedef std::map<std::string /* Col Name */,
                     std::string /* Col Value */> OutRowT;
    typedef boost::shared_ptr<QueryResultMetaData> MetadataT;
    typedef std::pair<OutRowT, MetadataT> ResultRowT; 
    typedef std::vector<ResultRowT> BufferT;

    typedef boost::variant<boost::blank, std::string, uint64_t, double, boost::uuids::uuid> SubVal;
    enum VarType {
        BLANK=0,
        STRING=1,
        UINT64=2,
        DOUBLE=3,
        UUID=4
    };
    enum AggOper {
        INVALID = 0,
        SUM = 1,
        COUNT = 2,
        CLASS = 3
    };

    // This is a map of aggregations for an output row
    // The key is the operation type and attribute name
    // The value is a vector of attribute values (or a single agg'ed value)
    typedef std::map<std::pair<AggOper,std::string>, SubVal> AggRowT;

    // The key of the multimap is a vector of sort-by values.
    // The last element of this vector is a hash of all unique cols
    typedef std::multimap<std::vector<SubVal>, 
                // The first element of this pair is itself a map
                // which includes all unique cols
                std::pair<std::map<std::string, SubVal>,
                          // This second element of the pair is a map of aggregations
                          AggRowT> > OutRowMultimapT;

    struct QPerfInfo {
        QPerfInfo(uint32_t w, uint32_t s, uint32_t p) :
            chunk_where_time(w), chunk_select_time(s), chunk_postproc_time(p),
            error(0) {}
        QPerfInfo() : 
            chunk_where_time(0), chunk_select_time(0), chunk_postproc_time(0),
            error(0) {}
        uint32_t chunk_where_time;
        uint32_t chunk_select_time; 
        uint32_t chunk_postproc_time;
        int error; 
    };

    void QueryResult(void *, QPerfInfo qperf, std::auto_ptr<BufferT> res,
            std::auto_ptr<OutRowMultimapT> mres);
private:
    EventManager * const evm_;
    QueryEngine * const qe_;

    class QEOpServerImpl;
    boost::scoped_ptr<QEOpServerImpl> impl_;

    friend class QEOpServerImpl;
};

#endif
