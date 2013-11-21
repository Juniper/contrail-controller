/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef DB_HANDLER_H_
#define DB_HANDLER_H_

#include <boost/scoped_ptr.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


#include "Thrift.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "gendb_if.h"

#include "viz_message.h"

class DbHandler {
public:
    static const int DefaultDbTTL = 0;

    typedef enum {
        INVALID = 0,
        UINT64 = 1,
        STRING = 2,
        DOUBLE = 3,
        MAXVAL 
    } VarType;

    struct Var {
        Var() : type(INVALID), str(""), num(0), dbl(0) {}
        Var(const std::string &s) : type(STRING), str(s), num(0), dbl(0) {}
        Var(uint64_t v) : type(UINT64), str(""), num(v), dbl(0) {}
        Var(double d) : type(DOUBLE), str(""), num(0), dbl(d) {}
        VarType type;
        std::string str;
        uint64_t num;
        double dbl;
    };

    typedef std::map<std::string, std::string> RuleMap;

    typedef std::map<std::string, Var > AttribMap;
    typedef std::map<std::string, std::pair<Var, AttribMap> > TagMap;

    DbHandler(EventManager *evm, GenDb::GenDbIf::DbErrorHandler err_handler,
            std::string cassandra_ip, unsigned short cassandra_port, int analytics_ttl, std::string name);
    DbHandler(GenDb::GenDbIf *dbif);
    virtual ~DbHandler();

    bool Init();
    void UnInit(bool shutdown);

    bool CreateTables();
    inline bool AllowMessageTableInsert(std::string& message_type);
    inline bool MessageIndexTableInsert(const std::string& cfname,
            const SandeshHeader& header, const std::string& message_type, const boost::uuids::uuid& unm);
    void MessageTableInsert(boost::shared_ptr<VizMsg> vmsgp);

    void GetRuleMap(RuleMap& rulemap);

    void ObjectTableInsert(const std::string table, const std::string rowkey,
            const RuleMsg& rmsg, const boost::uuids::uuid& unm);

    void StatTableInsert(uint64_t ts, 
            const std::string& statName,
            const std::string& statAttr,
            const TagMap & attribs_tag,
            const AttribMap & attribs_all);

    bool FlowTableInsert(const RuleMsg& rmsg);
    bool GetStats(uint64_t &queue_count, uint64_t &enqueues) const;

    GenDb::GenDbIf *get_dbif() {
        return dbif_.get();
    }

private:
    boost::scoped_ptr<GenDb::GenDbIf> dbif_;

    // Random generator for UUIDs
    tbb::mutex rand_mutex_;
    boost::uuids::random_generator umn_gen_;

    DISALLOW_COPY_AND_ASSIGN(DbHandler);
};

/*
 * pugi walker to process flow message
 */
class FlowDataIpv4ObjectWalker : public pugi::xml_tree_walker {
    public:

        FlowDataIpv4ObjectWalker(GenDb::ColList *col_list) : col_list(col_list) {}
        ~FlowDataIpv4ObjectWalker() {}

        static std::map<std::string, GenDb::DbDataType::type> name_to_type;

        // Callback that is called when traversal begins
        virtual bool begin(pugi::xml_node& node) {
            return true;
        }

        // Callback that is called for each node traversed
        virtual bool for_each(pugi::xml_node& node);

        // Callback that is called when traversal ends
        virtual bool end(pugi::xml_node& node) {
            return true;
        }

        GenDb::ColList *col_list;
};
#endif /* DB_HANDLER_H_ */
