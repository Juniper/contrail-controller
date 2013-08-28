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

class EventManager;
class QueryEngine;
// This class represents the interface between the Query Engine and 
// the OpServer. It will internally talk to the OpServer using Redis

class QEOpServerProxy {
public:

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
            uint16_t port);
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
    typedef std::pair<std::string,
                      std::vector<OutRowT> > BufferT;
    
    void QueryResult(void *, int error, std::auto_ptr<BufferT> res);

private:
    EventManager * const evm_;
    QueryEngine * const qe_;

    class QEOpServerImpl;
    boost::scoped_ptr<QEOpServerImpl> impl_;

    friend class QEOpServerImpl;
};
#endif
