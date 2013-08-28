/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "policy_config_parser.h"

#include <sstream>

#include <boost/ptr_container/ptr_list.hpp>

#include "base/logging.h"
#include <pugixml/pugixml.hpp>
#include "schema/routing_policy_types.h"


using namespace std;
using namespace pugi;

struct PolicyTerm {
    std::string term_name;
    autogen::PolicyMatch match;
    autogen::PolicyAction action;
};

typedef boost::ptr_list<PolicyTerm> PolicyTerms;

typedef std::map<std::string, PolicyTerms> PolicyMap;

static bool ParseTerm(const xml_node &xterm, PolicyTerm *term) {
    xml_attribute name = xterm.attribute("name");
    term->term_name = name.value();

    xml_node xaction;
    xml_node xmatch = xterm.child("from"); 

    if (xmatch) {
        term->match.XmlParse(xmatch);
        xaction = xmatch.next_sibling("then");
    } else {
        xaction = xterm.child("then"); 
    }

    term->action.XmlParse(xaction);

    return true;
}

static bool ParsePolicy(const xml_node &node, PolicyMap *policy) {
    PolicyTerms policy_terms;
    xml_attribute name = node.attribute("name");
    for (xml_node xterm = node.child("term"); xterm;
         xterm = xterm.next_sibling("term")) {
        PolicyTerm *term_data = new PolicyTerm();
        ParseTerm(xterm, term_data);
        policy_terms.push_back(term_data);
    }

    policy->insert(std::make_pair(name.value(), policy_terms)); 
    return true;
}

PolicyConfigParser::PolicyConfigParser() {
}


bool PolicyConfigParser::Parse(const std::string &content)  {
    istringstream sstream(content);
    xml_document xdoc;
    xml_parse_result result = xdoc.load(sstream);
    if (!result) {
        LOG(WARN, "Unable to load XML document. (status="
            << result.status << ", offset=" << result.offset << ")");
        return false;
    }

    PolicyMap policy;
    for (xml_node node = xdoc.first_child(); node; node = node.next_sibling()) {
        if (strcmp(node.name(), "policy") == 0) {
            ParsePolicy(node, &policy);
        }
    }
    return true;
}
