/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VIZ_MESSAGE_H__
#define __VIZ_MESSAGE_H__

#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <pugixml/pugixml.hpp>

#include <sandesh/sandesh_types.h>

#define VIZD_ASSERT(condition) assert((condition));

class SandeshMessage;

/* message format used to store in the cassandra */
struct VizMsg {
    VizMsg(const SandeshMessage *msg,
           boost::uuids::uuid unm) :
        msg(msg),
        unm(unm) {}
    ~VizMsg() {}

    const SandeshMessage *msg;
    boost::uuids::uuid unm; /* uuid key for this message in the global table */
};

class SandeshStats;
class SandeshLogLevelStats;
class SandeshMessageInfo;

struct VizMsgStats {
    VizMsgStats() : messages(0), bytes(0), last_msg_timestamp(0) {}

    void Update(const VizMsg *vmsg);
    template <typename K, typename T> void Get(K &key, T &stats) const;
    friend VizMsgStats operator+(const VizMsgStats &a, const VizMsgStats &b);
    friend VizMsgStats operator-(const VizMsgStats &a, const VizMsgStats &b);

    uint64_t messages;
    uint64_t bytes;
    uint64_t last_msg_timestamp;
};

struct VizMsgStatistics {
    VizMsgStatistics() {}

    void Update(const VizMsg *vmsg);
    void Get(std::vector<SandeshStats> &ssv) const;
    void Get(std::vector<SandeshLogLevelStats> &lsv) const;
    void Get(std::vector<SandeshMessageInfo> &sms);

    typedef boost::ptr_map<std::string, VizMsgStats> TypeMap;
    TypeMap type_map;

    typedef boost::ptr_map<std::string, VizMsgStats> LevelMap;
    LevelMap level_map;

    typedef std::pair<std::string, std::string> TypeLevelKey;
    typedef boost::ptr_map<TypeLevelKey, VizMsgStats> TypeLevelMap;
    TypeLevelMap type_level_map;
    TypeLevelMap otype_level_map;
};

/* generic message for ruleeng processing */
struct RuleMsg {
public:
    RuleMsg(const VizMsg *vmsgp);
    ~RuleMsg();

    int field_value(const std::string& field_id, std::string& type,
        std::string& value) const;

    struct RuleMsgPredicate {
        RuleMsgPredicate(const std::string &name) : tmp_(name) { }
        bool operator()(pugi::xml_attribute attr) const {
            return (strcmp(attr.name(), tmp_.c_str()) == 0);
        }
        bool operator()(pugi::xml_node node) const {
            return (strcmp(node.name(), tmp_.c_str()) == 0); 
        }
        std::string tmp_;
    };

    SandeshHeader hdr;
    std::string messagetype;

private:
    int field_value_recur(const std::string& field_id, std::string& type,
        std::string& value, pugi::xml_node doc) const;

    pugi::xml_node message_node_;
};

#endif // __VIZ_MESSAGE_H__

