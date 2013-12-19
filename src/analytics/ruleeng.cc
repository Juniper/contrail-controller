/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ruleeng.h"
#include <sstream>
#include <exception>
#include <cstdlib>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include "base/util.h"
#include "base/logging.h"
#include "sandesh/sandesh_constants.h"
#include "ruleparser/ruleglob.h"
#include "db_handler.h"
#include "OpServerProxy.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h> 
#include <sandesh/sandesh_trace.h> 
#include "collector_uve_types.h"
#include "viz_constants.h"

using std::string;

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

bool Ruleeng::rule_present(const boost::shared_ptr<VizMsg> vmsgp) {
    t_rulemsgtype msgtype(vmsgp->messagetype);

    if (vmsgp->hdr.__isset.Context) {
        msgtype.has_context_ = true;
        msgtype.context_ = vmsgp->hdr.Context;
    }

    return rulelist_->rule_present(msgtype);
}

/*
 * Check if any handling is needed for the message wrt to ObjectLog
 * Looks for the 'key' annotations for the table name and inserts
 * the object trace with the rowkey corresponding to the value of the
 * field
 */
void Ruleeng::handle_object_log(const pugi::xml_node& parent, const RuleMsg& rmsg,
        const boost::uuids::uuid& unm, DbHandler *db) {
    if (!(rmsg.hdr.get_Hints() & g_sandesh_constants.SANDESH_KEY_HINT)) {
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
    for (it = keymap.begin(); it != keymap.end(); it++) {
        db->ObjectTableInsert(it->first, it->second, rmsg, unm);
    }
    for (pugi::xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        handle_object_log(node, rmsg, unm, db);
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
    string attype = node.attribute("type").value();
    DbHandler::Var sample;
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

bool Ruleeng::handle_uve_publish(const RuleMsg& rmsg, DbHandler *db) {
    if (rmsg.hdr.get_Type() != SandeshType::UVE) {
        return true;
    }

    std::string type(rmsg.messagetype);
    std::string source(rmsg.hdr.get_Source());
    std::string module(rmsg.hdr.get_Module());
    int32_t seq = rmsg.hdr.get_SequenceNum();
    int64_t ts = rmsg.hdr.get_Timestamp();

    pugi::xml_node parent = rmsg.get_doc();
    RuleMsg::RuleMsgPredicate p1(type);
    pugi::xml_node object = parent.find_node(p1);
    if (!object) {
        LOG(ERROR, __func__ << " Message: " << type << " Source: " << source <<
            " object NOT PRESENT");
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
        LOG(ERROR, __func__ << " Message: " << type << " Source: " << source <<
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

            if (ltype == "struct") {
                                    
                string tstr(node.attribute("tags").value());

                for (pugi::xml_node elem = subs.first_child(); elem;
                        elem = elem.next_sibling()) {

                    DbHandler::TagMap tmap;
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

                        // TODO : check for timestamp character sequence
                        //        If it is found, use it as timestamp

                        // Separating this term into a prefix and suffix
                        // Read the suffix here as well
                        size_t spos = term.find(':');
                        string pterm, sterm, sname, pname;
                        DbHandler::Var sv;
                        if (spos == string::npos) {
                            pterm = term;
                        } else {
                            pterm = term.substr(0,spos);
                            sterm = term.substr(spos+1,string::npos);
                           
                            if (!sterm.empty()) {
                                pugi::xml_node anode_s;
                                string sname;
                                if (sterm[0] != '.') {
                                    anode_s = object.child(sterm.c_str());
                                    sname = sterm;
                                } else {
                                    string cattr = sterm.substr(1,string::npos);
                                    anode_s = elem.child(cattr.c_str());
                                    sname = string(node.name()) + sterm;
                                }
                                sv = ParseNode(anode_s);
                            }
                        }

                        if (pterm[0] != '.') {
                            pname = pterm;
                        } else {
                            pname = string(node.name()) + pterm;
                        }

                        // Look for an existing prefix entry
                        // Add the suffix to it if suffix was present
                        // If prefix doesn't exist, create it.
                        DbHandler::TagMap::iterator tr = tmap.find(pname);
                        if (tr == tmap.end()) {
                            
                            pugi::xml_node anode_p;
                            if (pterm[0] != '.') {
                                anode_p = object.child(pterm.c_str());
                            } else {
                                string cattr = pterm.substr(1,string::npos);
                                anode_p = elem.child(cattr.c_str());
                            }
                            DbHandler::Var pv = ParseNode(anode_p);

                            DbHandler::AttribMap amap;
                            if (!sterm.empty()) {
                                amap.insert(make_pair(sname, sv));
                            }
                            tmap.insert(make_pair(pname,
                                                  make_pair(pv, amap))); 
                        } else {
                            DbHandler::AttribMap& amap = tr->second.second;
                            if (!sterm.empty()) {
                                amap.insert(make_pair(sname, sv));
                            }                    
                        }

                    } while (npos != string::npos);

                    DbHandler::AttribMap attribs;
                    if (tmap.find(g_viz_constants.STAT_OBJECTID_FIELD) == tmap.end()) {
                        DbHandler::AttribMap amap;
                        pugi::xml_node anode_p = object.child(g_viz_constants.STAT_OBJECTID_FIELD.c_str());
                        DbHandler::Var pv = ParseNode(anode_p);

                        tmap.insert(make_pair(g_viz_constants.STAT_OBJECTID_FIELD, make_pair(pv, amap)));
                        attribs.insert(make_pair(g_viz_constants.STAT_OBJECTID_FIELD, pv));
                    }

                    // Load all tags and non-tags
                    for (pugi::xml_node sattr = elem.first_child(); sattr;
                            sattr = sattr.next_sibling()) {
                        string sattrname(".");
                        sattrname.append(sattr.name());
                        DbHandler::Var sample = ParseNode(sattr);

                        string tname = string(node.name()) + sattrname;
                        attribs.insert(make_pair(tname, sample));
                    }
                    db->StatTableInsert(ts, object.name(), node.name(), tmap, attribs);
                }

            } else {
                LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
                  " Name: " << object.name() <<  " Node: " << node.name()  <<
                  " Bad Stat type " << ltype); 
            }

        }
        
        if (!osp_->UVEUpdate(object.name(), node.name(),
                             source, module,
                             key, ostr.str(), seq,
                             agg, node.attribute("hbin").value(), ts)) {
            LOG(ERROR, __func__ << " Message: "  << type << " Source: " << source <<
              " Name: " << object.name() <<  " UVEUpdate Failed"); 
            PUBLISH_UVE_UPDATE_TRACE(UVETraceBuf, source, module, type, key , node.name(), false);
        } else {
            PUBLISH_UVE_UPDATE_TRACE(UVETraceBuf, source, module, type, key , node.name(), true);
        }
    }

    if (deleted) {
        if (!osp_->UVEDelete(object.name(), source, module, key, seq)) {
            LOG(ERROR, __func__ << " Cannot Delete " << key);
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key, seq, false);
        } else {
            PUBLISH_UVE_DELETE_TRACE(UVETraceBuf, source, module, type, key, seq, true);
        }
        LOG(DEBUG, __func__ << " Deleted " << key);
    }

    return true;
}

// handle flow message
bool Ruleeng::handle_flow_object(const RuleMsg& rmsg, DbHandler *db) {
    if (rmsg.hdr.get_Type() != SandeshType::FLOW) {
        return true;
    }

    if (!(db->FlowTableInsert(rmsg))) {
        return false;
    }
    return true;
}

bool Ruleeng::rule_execute(const boost::shared_ptr<VizMsg> vmsgp, bool uveproc, DbHandler *db) {
    if (db->DropMessage(vmsgp->hdr)) {
        return true;
    }
    RuleMsg rmsg(vmsgp);

    /*
     *  We would like to execute some actions globally here, before going
     *  through the ruleeng rules
     *  First set of actions is for Object Tracing
     */
    pugi::xml_node parent = rmsg.get_doc();

    remove_identifier(parent);

    handle_object_log(parent, rmsg, vmsgp->unm, db);

    if (uveproc) handle_uve_publish(rmsg, db);

    handle_flow_object(rmsg, db);

    rulelist_->rule_execute(rmsg);

    return true;
}


