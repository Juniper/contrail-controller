/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef DB_HANDLER_H_
#define DB_HANDLER_H_

#include <boost/scoped_ptr.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>

#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#include <boost/uuid/uuid_generators.hpp>
#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include <boost/tuple/tuple.hpp>

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

    bool DropMessage(const SandeshHeader &header, const VizMsg *vmsg);
    bool Init(bool initial, int instance);
    void UnInit(int instance);

    bool AllowMessageTableInsert(const SandeshHeader &header);
    bool MessageIndexTableInsert(const std::string& cfname,
        const SandeshHeader& header, const std::string& message_type,
        const boost::uuids::uuid& unm);
    virtual void MessageTableInsert(const VizMsg *vmsgp);
    void MessageTableOnlyInsert(const VizMsg *vmsgp);

    void GetRuleMap(RuleMap& rulemap);

    void ObjectTableInsert(const std::string &table, const std::string &rowkey,
        uint64_t &timestamp, const boost::uuids::uuid& unm);

    void StatTableInsert(uint64_t ts, 
            const std::string& statName,
            const std::string& statAttr,
            const TagMap & attribs_tag,
            const AttribMap & attribs_all);

    bool FlowTableInsert(const pugi::xml_node& parent,
        const SandeshHeader &header);
    bool GetStats(uint64_t &queue_count, uint64_t &enqueues,
        std::string &drop_level, std::vector<SandeshStats> &vdropmstats) const;
    bool GetStats(std::vector<GenDb::DbTableInfo> &vdbti,
        GenDb::DbErrors &dbe);

    void SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm);
    void ResetDbQueueWaterMarkInfo();

private:
    bool CreateTables();
    void SetDropLevel(size_t queue_count, SandeshLevel::type level);
    bool Setup(int instance);
    bool Initialize(int instance);

    boost::scoped_ptr<GenDb::GenDbIf> dbif_;

    // Random generator for UUIDs
    boost::uuids::random_generator umn_gen_;
    boost::uuids::string_generator s_gen_;
    std::string name_;
    std::string col_name_;
    SandeshLevel::type drop_level_;
    VizMsgStatistics dropped_msg_stats_;
    mutable tbb::mutex smutex_;

    DISALLOW_COPY_AND_ASSIGN(DbHandler);
};

/*
 * pugi walker to process flow message
 */
template <typename T>
class FlowDataIpv4ObjectWalker : public pugi::xml_tree_walker {
public:
    FlowDataIpv4ObjectWalker(T &values,
        boost::uuids::string_generator &s_gen) :
        values_(values),
        s_gen_(s_gen) {
    }
    ~FlowDataIpv4ObjectWalker() {}

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

private:
    T &values_;
    boost::uuids::string_generator &s_gen_;
};

#endif /* DB_HANDLER_H_ */
