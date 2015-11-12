/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <exception>
#include <cstdlib>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include <base/util.h>
#include <base/logging.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h> 
#include <sandesh/sandesh_trace.h>
#include <sandesh/sandesh_message_builder.h>
#include <sandesh/protocol/TXMLProtocol.h>
#include "ruleparser/ruleglob.h"
#include "db_handler.h"
#include "OpServerProxy.h"
#include "collector_uve_types.h"
#include "viz_constants.h"
#include "ruleeng.h"
#include "stat_walker.h"

using std::string;
using std::vector;
using std::map;
using std::pair;
using std::make_pair;

using namespace contrail::sandesh::protocol;

int Ruleeng::RuleBuilderID = 0;
int Ruleeng::RuleWorkerID = 0;

SandeshTraceBufferPtr UVETraceBuf(SandeshTraceBufferCreate("UveTrace", 25000));

Ruleeng::Ruleeng(DbHandler *db_handler, OpServerProxy *osp) :
    db_handler_(db_handler), osp_(osp), rulelist_(new t_rulelist()) {
}

Ruleeng::~Ruleeng() { 
    delete rulelist_;
}

void Ruleeng::Init() {
    LOG(DEBUG, "Ruleeng::" << __func__ << " Begin");
    DbHandler::RuleMap rulemap;

    db_handler_->GetRuleMap(rulemap);
    DbHandler::RuleMap::iterator iter;

    for (iter = rulemap.begin(); iter != rulemap.end(); iter++) {
        Buildrules((*iter).first, (*iter).second);
    }
    LOG(DEBUG, "Ruleeng::" << __func__ << " Done");
}

bool Ruleeng::Buildrules(const std::string& rulesrc, const std::string& rulebuf) { 
    Builder *task = new Builder(this, rulesrc, rulebuf);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);

    return true;
}

bool Ruleeng::Parserules(char *base, size_t sz) { 
    parse(rulelist_, base, sz);

    return true;
}

bool Ruleeng::Parserules(const char *bytes, int len) { 
    parse(rulelist_, bytes, len);

    return true;
}

bool Ruleeng::rule_present(const VizMsg *vmsgp) {
    t_rulemsgtype msgtype(vmsgp->msg->GetMessageType());

    const SandeshHeader &header(vmsgp->msg->GetHeader());
    if (header.__isset.Context) {
        msgtype.has_context_ = true;
        msgtype.context_ = header.Context;
    }

    return rulelist_->rule_present(msgtype);
}


void Ruleeng::remove_identifier(const pugi::xml_node &parent) {
    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        node.remove_attribute("identifier");
        remove_identifier(node);
    }
}

static DbHandler::Var ParseNode(const pugi::xml_node& node, bool silent = false) {
    DbHandler::Var sample;

    if (node.empty()) {
        LOG(ERROR, __func__ << "Parsing Empty node");
        return sample;
    }
    string attype = node.attribute("type").value();
    if (attype == "string") {
        std::string val(node.child_value());
        TXMLProtocol::unescapeXMLControlChars(val);
        sample = val;
    } else if (attype == "double") {   
        sample = (double) strtod(node.child_value(), NULL);
    } else if ((attype == "u16") ||  (attype == "u32") || (attype == "u64")) {
        sample = (uint64_t) strtoul(node.child_value(), NULL, 10);
    } else {
        if (!silent)
            LOG(ERROR, __func__ << " Bad Stat Type " << attype <<
                " for attr " << node.name());
    }
    return sample;
}

static bool ParseDomTags(const std::string& tstr,
        std::vector<std::string> *toptags,
        const std::vector<std::pair<std::string,pugi::xml_node> >* elem_chain,
        StatWalker::TagMap *tagmap) {
    size_t pos;
    size_t npos = 0;

    // If the tags string is empty, there's nothing to parse
    if (tstr.empty()) return true;

    do {
        if (npos)
            pos = npos+1;
        else
            pos = 0;
        
        npos = tstr.find(',' , pos);
        string term;
        if (npos == string::npos)
            term = tstr.substr(pos, string::npos);
        else 
            term = tstr.substr(pos, npos - pos);

        // Separating this term into a prefix and suffix
        // Prefix may not be present
        size_t spos = term.find(':');
        string pterm, sterm;

        if (spos == string::npos) {
            // Single Tag case
            sterm = term;
        } else {
            // Double Tag case 
            pterm = term.substr(0,spos);
            sterm = term.substr(spos+1,string::npos);
        }

        if (sterm.empty()) return false;

        if (toptags) {
            assert(elem_chain==NULL);
            assert(tagmap==NULL);
            if (sterm[0] != '.') {
                // These are top-level tags

                // We do not allow prefixes with top-level tags            
                if (!pterm.empty()) return false;

                toptags->push_back(sterm);
            }
            
            continue;
        } else {
            assert(elem_chain);
            assert(elem_chain->size()>1);
            assert(tagmap);
            if (sterm[0] != '.') {
                // We are not processing at the top level
                // Ignore top-level tags 
                continue;
            }
        }

        string sname, pname;

        // strip out the leading "."
        sname = sterm.substr(1, string::npos);

        size_t sz = elem_chain->size();
        pugi::xml_node anode_s = elem_chain->at(sz-1).second.child(sname.c_str());
        StatWalker::TagVal tv;
        tv.val = ParseNode(anode_s);

        if (!pterm.empty()) {
            // The prefix is a child of the deepest node,
            // or a child at the current level (2nd deepest node)
            size_t idx = ((pterm[0] == '.') ? sz-1 : sz-2);
            for (size_t ix=1; ix<=idx; ix++) {
                if (!pname.empty()) pname.append(".");
                pname.append(elem_chain->at(ix).first);
            }
            if (pterm[0] != '.') {
                if (!pname.empty()) pname.append(".");
            }
            pname.append(pterm);
            size_t found = pterm.rfind('.');
            string pattr = pterm.substr(found+1, string::npos);
            pugi::xml_node anode_p = elem_chain->at(idx).second.child(pattr.c_str());
            DbHandler::Var pv = ParseNode(anode_p);
            tv.prefix = make_pair(pname, pv);
        }

        tagmap->insert(make_pair(sname, tv));

    } while (npos != string::npos);

    return true;
}

static bool DomValidElems(pugi::xml_node node,
        std::vector<pugi::xml_node> &elem_list, std::string& ltype) {

    if (strcmp(node.attribute("type").value(), "list") == 0) {
        pugi::xml_node subs = node.child("list");
        ltype = subs.attribute("type").value();
        if (ltype == "struct") {
            for (pugi::xml_node elem = subs.first_child(); elem;
                    elem = elem.next_sibling()) {
                elem_list.push_back(elem);
            }
        } else {
            return false;
        }
    } else if (strcmp(node.attribute("type").value(), "struct") == 0) {
        elem_list.push_back(node.first_child());
    } else {
        return false;
    }
    return true;
}

/* This function recursively walks the XML DOM
   for objectlog and UVE messages and processes
   them for stats.
   It is invoked after finding a top-level
   stats attribute
*/
static bool DomStatWalker(StatWalker& sw,
        const std::string& tstr,
        std::vector<std::pair<std::string,pugi::xml_node> > elem_chain) {

    pugi::xml_node object = elem_chain.at(0).second;
    size_t sz = elem_chain.size();
    pugi::xml_node node = elem_chain.at(sz-1).second;
    string node_name = elem_chain.at(sz-1).first;
    StatWalker::TagMap tagmap;
    // Parse the tags annotation to find all tags that will
    // be used to index stats samples
    if (ParseDomTags(tstr, NULL, &elem_chain, &tagmap)) {
        DbHandler::AttribMap attribs;
        // For this map:
        //     the key is the attribute name
        //     the value is the tags string, and the child DOM elements
        map<string,pair<string, vector<pugi::xml_node> > > elem_map;
        // Load all tags and non-tags
        for (pugi::xml_node sattr = node.first_child(); sattr;
                sattr = sattr.next_sibling()) {
            DbHandler::Var sample = ParseNode(sattr, true);
            if (sample.type == DbHandler::INVALID) {
            //  for structs or lists, look for the tags annotation
            //  and process for stats.
            //  Ignore any struct or list that does not have the
            //  tags annotation
                if (sattr.attribute("tags").empty()) {
                    LOG(ERROR, __func__ << " Message: "  << 
                      " Name: " << object.name() <<  " Child Node: " << sattr.name()  <<
                      " No tags annotation ");
                    continue;
                }
                string ltype;
                vector<pugi::xml_node> elem_list;
                if (!DomValidElems(sattr, elem_list, ltype)) {
                    LOG(ERROR, __func__ << " Message: "  << 
                      " Name: " << object.name() <<  " Node: " << node.name()  <<
                      (ltype.empty() ? " Bad Stat type in walk" : " Bad Stat list type in walk") <<
                      (ltype.empty() ? node.attribute("type").value() : ltype));
                    continue;
                }
                elem_map[sattr.name()] = make_pair(sattr.attribute("tags").value(),
                                                   elem_list);
            } else { 
            // For simple attributes (strings, integers and doubles)
            // record the attributes in the stat tables
                attribs.insert(make_pair(sattr.name(), sample));
            }
        }
        sw.Push(node_name, tagmap, attribs);

        for (map<string, pair<string, vector<pugi::xml_node> > >::iterator ei =
                elem_map.begin(); ei != elem_map.end(); ei++) {
            vector<pugi::xml_node>& elem_list = ei->second.second;
            string & tstr_sub(ei->second.first);
            for (size_t idx=0; idx<elem_list.size(); idx++) {
                vector<pair<string,pugi::xml_node> > elem_parent = elem_chain;
                elem_parent.push_back(make_pair(ei->first,elem_list[idx]));
                // recursive invokation to process stats of child
                // structs and lists that have the tags annotation
                if (!DomStatWalker(sw, tstr_sub, elem_parent)) {
                    LOG(ERROR, __func__ << 
                      " Name: " << object.name() <<  " Node: " << node.name()  <<
                      " Bad element " << elem_list[idx].name());
                }
            }
        }
        sw.Pop();

    } else {
        LOG(ERROR, __func__ <<
          " Name: " << object.name() <<  " Node: " << node.name()  <<
          " Bad tags " << tstr); 
        return false;
    }
    return true;
}

/* 
    This function is used to analyse an objectlog or UVE for stats attributes
    and walk the XML DOM to process the stats
*/

static bool DomTopStatWalker(const pugi::xml_node& object,
        DbHandler *db,
        uint64_t timestamp,
        const pugi::xml_node& node,
        StatWalker::TagMap tmap,
        const std::string& source) {

    vector<pugi::xml_node> elem_list;
    string ltype;
    // We accept both structs and lists of structs for stats
    // For structs, we need to process a singe element.
    // Otherwise we process each element of the list
    if (!DomValidElems(node, elem_list, ltype)) {
        LOG(ERROR, __func__ << " Source: " << source << 
          " Name: " << object.name() <<  " Node: " << node.name()  <<
          (ltype.empty() ? " Bad Stat type " : " Bad Stat list type") <<
          (ltype.empty() ? node.attribute("type").value() : ltype));
        return false;
    }

    std::vector<std::string> toptags;

    string tstr(node.attribute("tags").value());

    // Get the top-level tags for this stat attribute
    if (ParseDomTags(tstr, &toptags, NULL, NULL)) {

        StatWalker::TagMap m1 = tmap;

        for (size_t idx=0; idx < toptags.size(); idx++) {
            pugi::xml_node anode_p =
                object.child(toptags.at(idx).c_str());
            StatWalker::TagVal th;
            th.val = ParseNode(anode_p);
            m1.insert(make_pair(toptags.at(idx), th));
        } 
        
        StatWalker sw(boost::bind(&DbHandler::StatTableInsert, db,
            _1, _2, _3, _4, _5), timestamp, object.name(), m1);

        vector<pair<string, pugi::xml_node> > parent_chain;
        parent_chain.push_back(make_pair(object.name(),object));

        // Process all elements the next level down for stats
        for (size_t idx=0; idx<elem_list.size(); idx++) {
            vector<pair<string, pugi::xml_node> > elem_chain = parent_chain; 
            elem_chain.push_back(make_pair(node.name(), elem_list[idx]));
            if (!DomStatWalker(sw, tstr, elem_chain)) {
                LOG(ERROR, __func__ << " Source: " << source <<
                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                  " Bad element " << elem_list[idx].name());
                continue;
            }
        }
    } else {
        LOG(ERROR, __func__ << " Source: " << source <<
          " Name: " << object.name() <<  " Node: " << node.name()  <<
          " Bad tags " << tstr); 
        return false;
    }
    return true;
}

/*
 * Walk the XML DOM to find keys to record this message against.
 * Write to the objectlog accordingly
 */
static size_t DomObjectWalk(const pugi::xml_node& parent, const VizMsg *rmsg,
        DbHandler *db, uint64_t timestamp) {
    std::map<std::string, std::string> keymap;
    std::map<std::string, std::string>::iterator it;
    const char *table;
    std::string rowkey;

    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        table = node.attribute("key").value();
        if (strcmp(table, "")) {
            rowkey = std::string(node.child_value());
            TXMLProtocol::unescapeXMLControlChars(rowkey);
            it = keymap.find(table);
            if (it != keymap.end()) {
                std::string tempstr(it->second);
                tempstr.append(":");
                tempstr.append(rowkey);
                keymap.erase(it);
                keymap.insert(std::pair<std::string, std::string>(table, tempstr));
            } else {
                keymap.insert(std::pair<std::string, std::string>(table, rowkey));
            }
        }
    }
    for (it = keymap.begin(); it != keymap.end(); it++) {
        db->ObjectTableInsert(it->first, it->second, 
            timestamp, rmsg->unm, rmsg);
    }
    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        DomObjectWalk(node, rmsg, db, timestamp);
    }
    return keymap.size();
}

/*
 * Check if any handling is needed for the message wrt to ObjectLog
 * Looks for the 'key' annotations for the table name and inserts
 * the object trace with the rowkey corresponding to the value of the
 * field
 */
void Ruleeng::handle_object_log(const pugi::xml_node& parent, const VizMsg *rmsg,
        DbHandler *db, const SandeshHeader &header) {
    if (!(header.get_Hints() & g_sandesh_constants.SANDESH_KEY_HINT)) {
        return;
    }
    uint64_t timestamp(header.get_Timestamp());
    std::string source(header.get_Source());
    std::string type(rmsg->msg->GetMessageType());
    std::string module(header.get_Module());
    std::string instance_id(header.get_InstanceId());
    std::string node_type(header.get_NodeType());
    SandeshType::type sandesh_type(header.get_Type());

    DomObjectWalk(parent, rmsg, db, timestamp);

    // UVE related stats are not processed here. See handle_uve_publish.
    if (sandesh_type == SandeshType::UVE) {
        return;
    }

    // All stats records the "name" field as a tag.
    // If no such field is present, use the source of
    // this message  
    DbHandler::Var nkey = source;

    StatWalker::TagMap m1;
    StatWalker::TagVal h1,h2; 
    h2.val = source;
    m1.insert(make_pair(g_viz_constants.STAT_SOURCE_FIELD, h2));

    pugi::xml_node object(parent);
    for (pugi::xml_node node = object.first_child(); node;
           node = node.next_sibling()) {
        if (!strcmp(node.name(), g_viz_constants.STAT_OBJECTID_FIELD.c_str())) {
            nkey = ParseNode(node);
        }
    }
    h1.val = nkey;
    m1.insert(make_pair(g_viz_constants.STAT_OBJECTID_FIELD, h1));
    
    for (pugi::xml_node node = object.first_child(); node;
           node = node.next_sibling()) {

        if (!node.attribute("tags").empty()) {
           DomTopStatWalker(object, db, timestamp, node,
                   m1, source);
        }
    }
}

bool Ruleeng::handle_uve_publish(const pugi::xml_node& parent,
    const VizMsg *rmsg, DbHandler *db, const SandeshHeader& header) {
    const SandeshType::type& sandesh_type(header.get_Type());
    if ((sandesh_type != SandeshType::UVE) &&
        (sandesh_type != SandeshType::ALARM)) {
        return true;
    }

    bool is_alarm = (sandesh_type == SandeshType::ALARM) ? true : false;

    std::string type(rmsg->msg->GetMessageType());
    std::string source(header.get_Source());
    std::string module(header.get_Module());
    std::string instance_id(header.get_InstanceId());
    std::string node_type(header.get_NodeType());
    int32_t seq(header.get_SequenceNum());
    int64_t ts(header.get_Timestamp());

    pugi::xml_node object(parent);
    if (!object) {
        LOG(ERROR, __func__ << " Message: " << type << " : " << source <<
            ":" << node_type << ":" << module << ":" << instance_id <<
            " object NOT PRESENT: " << rmsg->msg->ExtractMessage());
        return false;
    }

    object = object.child("data");
    object = object.first_child();

    std::string barekey;
    const char *tempstr;
    std::string rowkey;
    std::string table;

    bool deleted = false;
    for (pugi::xml_node node = object.first_child(); node;
            node = node.next_sibling()) {
        tempstr = node.attribute("key").value();
        if (strcmp(tempstr, "")) {
            rowkey = std::string(node.child_value());
            TXMLProtocol::unescapeXMLControlChars(rowkey);
            if (!barekey.empty()) {
                barekey.append(":");
                barekey.append(rowkey);
            } else {
                table = std::string(tempstr);
                barekey.append(rowkey);
                
            }
        }
        if (!strcmp(node.name(), "deleted")) {
            if (!strcmp(node.child_value(), "true")) {
                deleted = true;
            }
        }
    }

    std::string key = table + ":" + barekey;

    if (table.empty()) {
        LOG(ERROR, __func__ << " Message: " << type << " : " << source <<
            ":" << node_type << ":" << module << ":" << instance_id <<
            " key NOT PRESENT");
        return false;
    }

    map<string,string> emap, vmap;
    if (deleted) {
        if (!osp_->UVEDelete(object.name(), source, node_type, module, 
                             instance_id, key, seq, is_alarm)) {
            LOG(ERROR, __func__ << " Cannot Delete " << key);
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key,
                seq, false, node_type, instance_id);
        } else {
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key,
                seq, true, node_type, instance_id);
        }
        LOG(DEBUG, __func__ << " Deleted " << key);
        osp_->UVENotif(object.name(), 
            source, node_type, module, instance_id, table, barekey, emap ,deleted);
        return true;
    }

    for (pugi::xml_node node = object.first_child(); node;
           node = node.next_sibling()) {
        std::ostringstream ostr; 
        node.print(ostr, "", pugi::format_raw | pugi::format_no_escapes);
        std::string agg;
        std::string atyp;
        tempstr = node.attribute("key").value();
        if (strcmp(tempstr, "")) {
            continue;
        }
        vmap.insert(make_pair(node.name(), ostr.str()));
        tempstr = node.attribute("aggtype").value();
        if (strcmp(tempstr, "")) {
            agg = std::string(tempstr);
        } else {
            agg = std::string("None");
        }

        if (!node.attribute("tags").empty()) {

            // For messages send during UVE Sync, stats must be ignored
            if (header.get_Hints() & g_sandesh_constants.SANDESH_SYNC_HINT) {
                continue;
            }

            pugi::xml_node anode_p =
                object.child(g_viz_constants.STAT_OBJECTID_FIELD.c_str());
           
            StatWalker::TagMap m1;
            StatWalker::TagVal h1,h2;
            h1.val = ParseNode(anode_p);
            m1.insert(make_pair(g_viz_constants.STAT_OBJECTID_FIELD, h1));
            h2.val = source;
            m1.insert(make_pair(g_viz_constants.STAT_SOURCE_FIELD, h2));

            // Process this UVE's Stat attributes.
            // We will always index by Source and UVE key (name) 
            // Other indexes depend on the "tags" attribute
            if (!DomTopStatWalker(object, db, ts, node,
                    m1, source)) {
                continue;
            }

        }
        
        if (!osp_->UVEUpdate(object.name(), node.name(),
                             source, node_type, module, instance_id,
                             table, barekey, ostr.str(), seq,
                             agg, ts,
                             is_alarm)) {
            LOG(ERROR, __func__ << " Message: "  << type << " : " << source <<
              ":" << node_type << ":" << module << ":" << instance_id <<
              " Name: " << object.name() <<  " UVEUpdate Failed"); 
            PUBLISH_UVE_UPDATE_TRACE(UVETraceBuf, source, module, type, key, 
                node.name(), false, node_type, instance_id);
        } else {
            PUBLISH_UVE_UPDATE_TRACE(UVETraceBuf, source, module, type, key,
                node.name(), true, node_type, instance_id);
        }
    }

    // Publish on the Kafka bus that this UVE has changed
    if (!strcmp(object.name(), "UVEAlarms")) {
        osp_->UVENotif(object.name(), 
            source, node_type, module, instance_id, table, barekey, vmap, deleted);
    } else {
        osp_->UVENotif(object.name(), 
            source, node_type, module, instance_id, table, barekey, emap, deleted);
    }
    return true;
}

// handle flow message
bool Ruleeng::handle_flow_object(const pugi::xml_node &parent,
    DbHandler *db, const SandeshHeader &header) {
    if (header.get_Type() != SandeshType::FLOW) {
        return true;
    }

    if (!(db->FlowTableInsert(parent, header))) {
        return false;
    }
    return true;
}

bool Ruleeng::rule_execute(const VizMsg *vmsgp, bool uveproc, DbHandler *db) {
    const SandeshHeader &header(vmsgp->msg->GetHeader());
    if (db->DropMessage(header, vmsgp)) {
        return true;
    }
    // Insert into the message and message index tables
    db->MessageTableInsert(vmsgp);
    /*
     *  We would like to execute some actions globally here, before going
     *  through the ruleeng rules
     *  First set of actions is for Object Tracing
     */
    const SandeshXMLMessage *sxmsg = 
        static_cast<const SandeshXMLMessage *>(vmsgp->msg);
    const pugi::xml_node &parent(sxmsg->GetMessageNode());

    remove_identifier(parent);

    handle_object_log(parent, vmsgp, db, header);

    if (uveproc) handle_uve_publish(parent, vmsgp, db, header);

    handle_flow_object(parent, db, header);

    RuleMsg rmsg(vmsgp); 
    rulelist_->rule_execute(rmsg);
    return true;
}
