/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <exception>
#include <cstdlib>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include <base/util.h>
#include <base/logging.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h> 
#include <sandesh/sandesh_trace.h>
#include <sandesh/sandesh_message_builder.h>
#include "ruleparser/ruleglob.h"
#include "db_handler.h"
#include "OpServerProxy.h"
#include "collector_uve_types.h"
#include "viz_constants.h"
#include "ruleeng.h"
#include "stat_walker.h"

using std::string;
using std::vector;
using std::pair;
using std::make_pair;

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

    std::map<std::string, std::string> keymap;
    std::map<std::string, std::string>::iterator it;
    const char *table, *rowkey;

    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        table = node.attribute("key").value();
        if (strcmp(table, "")) {
            rowkey = node.child_value();
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
    uint64_t timestamp(header.get_Timestamp());
    for (it = keymap.begin(); it != keymap.end(); it++) {
        db->ObjectTableInsert(it->first, it->second, 
            timestamp, rmsg->unm);
    }
    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        handle_object_log(node, rmsg, db, header);
    }
}

void Ruleeng::remove_identifier(const pugi::xml_node &parent) {
    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        node.remove_attribute("identifier");
        remove_identifier(node);
    }
}

static DbHandler::Var ParseNode(const pugi::xml_node& node) {
    DbHandler::Var sample;

    if (node.empty()) {
        LOG(ERROR, __func__ << "Parsing Empty node");
        return sample;
    }
    string attype = node.attribute("type").value();
    if (attype == "string") {
        sample = string(node.child_value());
    } else if (attype == "double") {   
        sample = (double) strtod(node.child_value(), NULL);
    } else if ((attype == "u16") ||  (attype == "u32") || (attype == "u64")) {
        sample = (uint64_t) strtoul(node.child_value(), NULL, 10);
    } else {
        LOG(ERROR, __func__ << " Bad Stat Type " << attype << " for attr " << node.name());
    }
    return sample;
}

static bool ParseTags(const string& tstr, const string& node,
        vector<string> * singletag,
        vector<pair<string,string> > * doubletag) {
    size_t pos;
    size_t npos = 0;
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
        // Read the suffix here as well
        size_t spos = term.find(':');
        string pterm, sterm, sname, pname;

        if (spos == string::npos) {
            pterm = term;
            if (pterm.empty()) return false;
            if (pterm[0] != '.') {
                pname = pterm;
            } else {
                pname = node + pterm;
            }
            (*singletag).push_back(pname);
        } else {
            pterm = term.substr(0,spos);
            sterm = term.substr(spos+1,string::npos);
               
            if (pterm.empty()) return false;
            if (pterm[0] != '.') {
                pname = pterm;
            } else {
                pname = node + pterm;
            }

            if (sterm.empty()) return false;
            if (sterm[0] != '.') {
                sname = sterm;
            } else {
                sname = node + sterm;
            }

            (*doubletag).push_back(make_pair(pname,sname));

        }

    } while (npos != string::npos);

    return true;

}

bool Ruleeng::handle_uve_publish(const pugi::xml_node& parent,
    const VizMsg *rmsg, DbHandler *db, const SandeshHeader& header) {
    if (header.get_Type() != SandeshType::UVE) {
        return true;
    }

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
    const char *tempstr, *rowkey;
    std::string table;

    for (pugi::xml_node node = object.first_child(); node;
            node = node.next_sibling()) {
        tempstr = node.attribute("key").value();
        if (strcmp(tempstr, "")) {
            rowkey = node.child_value();
            if (!barekey.empty()) {
                barekey.append(":");
                barekey.append(rowkey);
            } else {
                table = std::string(tempstr);
                barekey.append(rowkey);
                
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

    bool deleted = false;
    for (pugi::xml_node node = object.first_child(); node;
           node = node.next_sibling()) {
        std::ostringstream ostr; 
        node.print(ostr, "", pugi::format_raw);
        std::string agg;
        std::string atyp;
        if (!strcmp(node.name(), "deleted")) {
            if (!strcmp(node.child_value(), "true")) {
                deleted = true;
                continue;
            }
            continue;
        }
        tempstr = node.attribute("key").value();
        if (strcmp(tempstr, "")) {
            continue;
        }
        tempstr = node.attribute("aggtype").value();
        if (strcmp(tempstr, "")) {
            agg = std::string(tempstr);
            if (!strcmp(tempstr,"stats")) {
                ostr.str("");
                ostr.clear();
                ostr << node.child_value();
            }
        } else {
            agg = std::string("None");
        }

        if (!node.attribute("tags").empty()) {

            // We only process tags for lists of structs
            string ltype;
            pugi::xml_node subs;
            if (strcmp(node.attribute("type").value(), "list") == 0) {
                subs = node.child("list");
                ltype = subs.attribute("type").value();
            }
            
            string tstr(node.attribute("tags").value());
            vector<string> singletag;
            vector<pair<string,string> > doubletag;
            bool pt = ParseTags(tstr, node.name(), &singletag, &doubletag);
  
            if ((ltype == "struct") && pt) {
                                    
                pugi::xml_node anode_p =
                    object.child(g_viz_constants.STAT_OBJECTID_FIELD.c_str());

                StatWalker::TagMap m1;
                StatWalker::TagVal h1,h2;
                h1.val = ParseNode(anode_p);
                m1.insert(make_pair(g_viz_constants.STAT_OBJECTID_FIELD, h1));
                h2.val = source;
                m1.insert(make_pair(g_viz_constants.STAT_SOURCE_FIELD, h2));

                for (size_t idx=0; idx < singletag.size(); idx++) {
                    // We only recognize top-level single-tags 
                    if (singletag[idx].find(".") == string::npos) {
                        pugi::xml_node anode_p =
                            object.child(singletag[idx].c_str());
                        StatWalker::TagVal th;
                        th.val = ParseNode(anode_p);
                        m1.insert(make_pair(singletag[idx], th));
                    }
                } 
                
                StatWalker sw(boost::bind(&DbHandler::StatTableInsert, db,
                    _1, _2, _3, _4, _5), ts, object.name(), m1);
 
                for (pugi::xml_node elem = subs.first_child(); elem;
                        elem = elem.next_sibling()) {

                    DbHandler::AttribMap attribs;

                    // Load all tags and non-tags
                    for (pugi::xml_node sattr = elem.first_child(); sattr;
                            sattr = sattr.next_sibling()) {
                        DbHandler::Var sample = ParseNode(sattr);
                        attribs.insert(make_pair(sattr.name(), sample));
                    }

                    StatWalker::TagMap tm;

                    for (size_t idx=0; idx < singletag.size(); idx++) {
                        size_t pos = singletag[idx].find(".");

                        // Only look for tags at the current level
                        if (pos != string::npos) {
                            string term = singletag[idx].substr(pos+1, string::npos);
                            // Match against attribs of this level
                            if (attribs.find(term) != attribs.end()) {
                                StatWalker::TagVal tv;
                                pugi::xml_node anode_s = elem.child(term.c_str());
                                tv.val = ParseNode(anode_s);
                                tm.insert(make_pair(term, tv));
                            } else {
                                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                                  ":" << node_type << ":" << module << ":" << instance_id << 
                                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                                  " Count not find attrib for tag " << term);
                            }
                        }
                    }

                    for (size_t idx=0; idx < doubletag.size(); idx++) {
                        size_t ppos = doubletag[idx].first.find(".");
                        pugi::xml_node anode_p;
                        if (ppos != string::npos) {
                            string pterm = doubletag[idx].first.substr(ppos+1, string::npos);
                            if (attribs.find(pterm) != attribs.end()) {
                                anode_p = elem.child(pterm.c_str());
                            } else {
                                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                                  ":" << node_type << ":" << module << ":" << instance_id << 
                                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                                  " Could not find attrib for ptag " << pterm);
                                continue;
                            }
                        } else {
                            string pterm = doubletag[idx].first;
                            anode_p = object.child(pterm.c_str());
                        }
                        DbHandler::Var pv = ParseNode(anode_p);
                        StatWalker::TagVal tv;
                        tv.prefix = make_pair(doubletag[idx].first, pv);

                        size_t spos = doubletag[idx].second.find(".");
                        pugi::xml_node anode_s;
                        if (spos != string::npos) {
                            string sterm = doubletag[idx].second.substr(spos+1, string::npos);
                            if (tm.find(sterm) != tm.end()) {
                                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                                  ":" << node_type << ":" << module << ":" << instance_id << 
                                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                                  " Duplicate suffix for " << sterm);
                                continue;
                            }
                            if (attribs.find(sterm) != attribs.end()) {
                                anode_s = elem.child(sterm.c_str());
                                tv.val= ParseNode(anode_s);
                                tm.insert(make_pair(sterm, tv));
                            } else {
                                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                                  ":" << node_type << ":" << module << ":" << instance_id << 
                                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                                  " Could not find attrib for stag " << sterm);
                            }
                        } else {
                            LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                              ":" << node_type << ":" << module << ":" << instance_id << 
                              " Name: " << object.name() <<  " Node: " << node.name()  <<
                              " Cannot record top-level stag " << doubletag[idx].second);
                        }
                    }

                    sw.Push(node.name(), tm, attribs);
                    sw.Pop();

                    // We don't want to show query info in the Analytics Node UVE
                    if (!strcmp(object.name(),"QueryPerfInfo")) continue;
                    if (elem == subs.first_child()) {
                        vector<string> sels = DbHandler::StatTableSelectStr(
                                object.name(), node.name(), attribs);
                        string qclause;
                        qclause.reserve(100);
                        qclause.append("[{\"rtype\":\"query\", \"aggtype\":\"StatTable.");
                        qclause.append(object.name());
                        qclause.append(".");
                        qclause.append(node.name());
                        qclause.append("\", \"select\":[");
                        int elems = 0;
                        for (vector<string>::const_iterator it = sels.begin();
                                it != sels.end(); it++) {
                            if (*it==g_viz_constants.STAT_OBJECTID_FIELD) continue;
                            if (*it==g_viz_constants.STAT_SOURCE_FIELD) continue;
                            if (elems) qclause.append(",");
                            qclause.append("\"");
                            qclause.append(*it);
                            qclause.append("\"");
                            elems++;
                        }
                        qclause.append("] }]");
                        ostr.str("");
                        ostr.clear();
                        ostr << qclause;
                    }
                }

                // We don't want to show query info in the Analytics Node UVE
                if (!strcmp(object.name(),"QueryPerfInfo")) continue;

            } else {
                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                  ":" << node_type << ":" << module << ":" << instance_id << 
                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                  " Bad Stat type " << ltype << " or tags " << tstr); 
                continue;
            }
        }
        
        if (!osp_->UVEUpdate(object.name(), node.name(),
                             source, node_type, module, instance_id,
                             key, ostr.str(), seq,
                             agg, node.attribute("hbin").value(), ts)) {
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

    if (deleted) {
        if (!osp_->UVEDelete(object.name(), source, node_type, module, 
                             instance_id, key, seq)) {
            LOG(ERROR, __func__ << " Cannot Delete " << key);
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key,
                seq, false, node_type, instance_id);
        } else {
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key,
                seq, true, node_type, instance_id);
        }
        LOG(DEBUG, __func__ << " Deleted " << key);
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
