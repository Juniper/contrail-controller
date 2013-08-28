/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VIZ_MESSAGE_H__
#define __VIZ_MESSAGE_H__

#include <string>
#include <boost/asio/ip/tcp.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "pugixml/pugixml.hpp"
#include "sandesh/sandesh_types.h"

#define VIZD_ASSERT(condition) assert((condition));

/* message format used to store in the cassandra */
struct VizMsg {
    VizMsg(SandeshHeader hdr,
            std::string mtype,
            std::string xmlmessage,
            boost::uuids::uuid unm) :
        hdr(hdr),
        messagetype(mtype),
        xmlmessage(xmlmessage),
        unm(unm) {}
    ~VizMsg() {}

    SandeshHeader hdr;
    std::string messagetype;
    std::string xmlmessage;
    boost::uuids::uuid unm; /* uuid key for this message in the global table */
};

/* generic message for ruleeng processing */
struct RuleMsg {
    public:
        RuleMsg(const boost::shared_ptr<VizMsg> vmsgp);
        ~RuleMsg();

        int field_value(const std::string& field_id, std::string& type, std::string& value) const;
        SandeshHeader hdr;
        std::string messagetype;

        const pugi::xml_node get_doc() const {
            return doc_;
        }

        struct RuleMsgPredicate {
            bool operator()(pugi::xml_attribute attr) const {
                return (strcmp(attr.name(), tmp_.c_str()) == 0);
            }
            bool operator()(pugi::xml_node node) const {
                return (strcmp(node.name(), tmp_.c_str()) == 0); 
            }
            RuleMsgPredicate(const std::string &name) : tmp_(name) { }
            std::string tmp_;
        };

    private:

        int field_value_recur(const std::string& field_id, std::string& type, std::string& value, pugi::xml_node doc) const;

        pugi::xml_document doc_;
};

#endif

