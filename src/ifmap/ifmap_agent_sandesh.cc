/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/request_pipeline.h>

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_table_partition.h"
#include "base/bitset.h"

#include <ifmap/ifmap_update.h>
#include <ifmap/ifmap_table.h>
#include <ifmap/ifmap_agent_table.h>
#include <ifmap/ifmap_node.h>
#include <ifmap/ifmap_agent_types.h>
#include <ifmap/ifmap_agent_parser.h>
#include <pugixml/pugixml.hpp>

using namespace boost::assign;
using namespace std;

class ShowIFMapAgentTable {
public:
    static DB *db_;
    static IFMapAgentParser *parser_;
    struct ShowData : public RequestPipeline::InstData {
        vector<string> send_buffer;
    };

    RequestPipeline::InstData *AllocData(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    void MakeNode(const string &name_sub_string,
                  const string &link_type_sub_string,
                  const string &link_node_sub_string,
                  string &dst, DBEntryBase *src);

    bool BufferStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                     int stage, int instNum, RequestPipeline::InstData *data);
    bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                   int stage, int instNum, RequestPipeline::InstData *data);
    void TableToBuffer(const ShowIFMapAgentReq *request, IFMapTable *table,
                       ShowData *show_data);

    bool BufferAllTables(const RequestPipeline::PipeSpec ps,
                         RequestPipeline::InstData *data);
    bool BufferSomeTables(const RequestPipeline::PipeSpec ps,
                          RequestPipeline::InstData *data);
};

DB* ShowIFMapAgentTable::db_;
IFMapAgentParser* ShowIFMapAgentTable::parser_;

static inline void to_uuid(uint64_t ms_long, uint64_t ls_long,
                              boost::uuids::uuid &u) {
    for (int i = 0; i < 8; i++) {
        u.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        u.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
}


void xml_parse(pugi::xml_node &node, string &s, int n) {
    int child;
    pugi::xml_node chld;
    pugi::xml_node fchld;
    string t(n, '\t');
    static uint64_t uuid_ms;
    static uint64_t uuid_ls;
    static int ls_set = 0;
    static int ms_set = 0;

    switch(node.type()) {
        case pugi::node_element:
            child = 0;
            for(chld = node.first_child(); chld; chld = chld.next_sibling()) {
                child++;
                fchld = chld;
            }

            if (strlen(node.child_value()) == 0) {
                if (!child) {
                    break;
                }
                if ((child == 1) && (fchld.type() == pugi::node_pcdata) && (strlen(fchld.child_value()) == 0)) {
                    break;
                }
            }

            if (strcmp(node.name(), "uuid-mslong") == 0) {
                string value(node.child_value());
                boost::trim(value);
                uuid_ms = strtoull(value.c_str(), NULL, 10);
                ms_set = 1;
            }

            if (strcmp(node.name(), "uuid-lslong") == 0) {
                string value(node.child_value());
                boost::trim(value);
                uuid_ls = strtoull(value.c_str(), NULL, 10);
                ls_set = 1;
            }

            if (strcmp(node.name(), "config") && strcmp(node.name(), "node")) {

                if (strlen(node.child_value())) {
                    s = s + t + node.name() + ":" + node.child_value() + "\n";
                } else {
                    s = s + t + node.name() + "\n";
                }
            }

            if (ms_set && ls_set) {
                boost::uuids::uuid u;
                to_uuid(uuid_ms, uuid_ls, u);
                string tmp = UuidToString(u);
                s = s + t + "Uuid : " + tmp + "\n";
                ms_set = ls_set = 0;
            }

            for (pugi::xml_attribute_iterator ait = node.attributes_begin(); ait != node.attributes_end(); ++ait) {
                s = s + t + ait->name() + ":" + ait->value() + "\n";
            }

            for(pugi::xml_node chld = node.first_child(); chld; chld = chld.next_sibling()) {
                xml_parse(chld, s, n + 1);
            }
            break;

        case pugi::node_pcdata:
            s = s + node.child_value() + "\n";
            break;

        default:
            break;
    }
}
void ShowIFMapAgentTable::MakeNode(const string &name_sub_string,
                                   const string &link_node_sub_string,
                                   const string &link_type_sub_string,
                                   string &dst, DBEntryBase *src) {
    IFMapNode *node = static_cast<IFMapNode *>(src);
    pugi::xml_document doc;
    pugi::xml_node config;

    if (name_sub_string.empty() == false) {
        if (node->name().find(name_sub_string) == string::npos)
            return;
    }

    dst += "\n";
    if (!node->IsDeleted()) {
        config = doc.append_child("config");
    } else {
        config = doc.append_child("Delete Marked config");
    }

    node->EncodeNodeDetail(&config);
    xml_parse(config, dst, 1);

    if (node->IsDeleted()) {
        return;
    }

    // Display its adjacencies
    dst = dst + "Adjacencies:\n";
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator
         iter = node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());

        if (link_type_sub_string.empty() == false) {
            // Check if filter passes the table-name for the adjacency
            if (strstr(adj_node->table()->Typename(),
                       link_type_sub_string.c_str()) == NULL)
                continue;
        }

        if (link_node_sub_string.empty() == false) {
            // Check if filter passes the node-name for the adjacency
            if (adj_node->name().find(link_node_sub_string) == string::npos)
                continue;
        }

        dst = dst + "\t" + adj_node->table()->Typename() + "  " +
            adj_node->name() +"\n";
    }
}

void ShowIFMapAgentTable::TableToBuffer(const ShowIFMapAgentReq *request,
                                        IFMapTable *table,
                                        ShowData *show_data) {
    for (int i = 0; i < IFMapTable::kPartitionCount; i++) {
        DBTablePartBase *partition = table->GetTablePartition(i);
        DBEntryBase *src = partition->GetFirst();
        while (src) {
            string dst;
            MakeNode(request->get_node_sub_string(),
                     request->get_link_node_sub_string(),
                     request->get_link_type_sub_string(),
                     dst, src);
            if (dst.empty() == false)
                show_data->send_buffer.push_back(dst);
            src = partition->GetNext(src);
        }
    }
}

bool ShowIFMapAgentTable::BufferSomeTables(const RequestPipeline::PipeSpec ps,
                                      RequestPipeline::InstData *data) {
    const ShowIFMapAgentReq *request =
        static_cast<const ShowIFMapAgentReq *>(ps.snhRequest_.get());

    IFMapTable *table = IFMapTable::FindTable(db_, request->get_table_name());
    if (table == NULL)  {
        LOG(DEBUG, "Invalid table name: " << request->get_table_name());
        return true;
    }

    if (table->name().find("__ifmap__.") != 0) {
        LOG(DEBUG, "Invalid table name: " << request->get_table_name());
        return true;
    }

    ShowData *show_data = static_cast<ShowData *>(data);
    TableToBuffer(request, table, show_data);
    return true;
}

bool ShowIFMapAgentTable::BufferAllTables(const RequestPipeline::PipeSpec ps,
                                     RequestPipeline::InstData *data) {

    const ShowIFMapAgentReq *request =
        static_cast<const ShowIFMapAgentReq *>(ps.snhRequest_.get());
    for (DB::iterator iter = db_->lower_bound("__ifmap__.");
         iter != db_->end(); ++iter) {
        if (iter->first.find("__ifmap__.") != 0) {
            break;
        }
        IFMapTable *table = static_cast<IFMapTable *>(iter->second);
        ShowData *show_data = static_cast<ShowData *>(data);
        TableToBuffer(request, table, show_data);
    }

    return true;
}

bool ShowIFMapAgentTable::BufferStage(const Sandesh *sr,
                                 const RequestPipeline::PipeSpec ps,
                                 int stage, int instNum,
                                 RequestPipeline::InstData *data) {

    const ShowIFMapAgentReq *request =
        static_cast<const ShowIFMapAgentReq *>(ps.snhRequest_.get());
    // If table name has not been passed, print all tables
    if (request->get_table_name().length()) {
        return BufferSomeTables(ps, data);
    } else {
        return BufferAllTables(ps, data);
    }
}

bool ShowIFMapAgentTable::SendStage(const Sandesh *sr,
                               const RequestPipeline::PipeSpec ps,
                               int stage, int instNum,
                               RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *stage_data = ps.GetStageData(0);
    const ShowIFMapAgentTable::ShowData &show_data =
        static_cast<const ShowIFMapAgentTable::ShowData &> (stage_data->at(0));
    const ShowIFMapAgentReq *request =
        static_cast<const ShowIFMapAgentReq *>(ps.snhRequest_.get());
    ShowIFMapAgentResp *response = new ShowIFMapAgentResp();
    response->set_table_data(show_data.send_buffer);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();
    return true;
}

void ShowIFMapAgentReq::HandleRequest() const {
    ShowIFMapAgentTable show_table;

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = boost::bind(&ShowIFMapAgentTable::AllocData, &show_table, _1);
    s0.cbFn_ = boost::bind(&ShowIFMapAgentTable::BufferStage, &show_table,
                           _1, _2, _3, _4, _5);
    s0.instances_.push_back(0);

    // Agent ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("agent_ifmap::ShowCommand");
    s1.allocFn_ = boost::bind(&ShowIFMapAgentTable::AllocData, &show_table, _1);
    s1.cbFn_ = boost::bind(&ShowIFMapAgentTable::SendStage, &show_table,
                           _1, _2, _3, _4, _5);
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

void ShowIFMapAgentDefLinkReq::HandleRequest() const {
    ShowIFMapAgentDefLinkResp *resp;
    resp = new ShowIFMapAgentDefLinkResp();

    //Get link table
    IFMapAgentLinkTable *link_table = static_cast<IFMapAgentLinkTable *>(
                     ShowIFMapAgentTable::db_->FindTable(IFMAP_AGENT_LINK_DB_NAME));

    IFMapAgentLinkTable::LinkDefMap::const_iterator dlist_it;
    std::list<IFMapAgentLinkTable::DeferredNode> *ent;
    std::list<IFMapAgentLinkTable::DeferredNode>::iterator it;

    //Get linktables's deferred list
    const IFMapAgentLinkTable::LinkDefMap &def_list = link_table->GetLinkDefMap();

    //Get Sandesh response's output list
    std::vector<IFMapAgentDefLink> &list =
        const_cast<std::vector<IFMapAgentDefLink>&>(resp->get_def_list());

    //Iterate left node list
    for(dlist_it = def_list.begin(); dlist_it != def_list.end(); dlist_it++) {
        const IFMapTable::RequestKey &temp = dlist_it->first;
        ent = dlist_it->second;

        //Iterate the right nodes corresponding to above left node
        for(it = ent->begin(); it != ent->end(); it++) {
            IFMapAgentDefLink data;
            data.set_seq_num((*it).node_key.id_seq_num);
            data.set_left_node(temp.id_type + ":" +
                    temp.id_name);
            data.set_metadata((*it).link_metadata);
            data.set_right_node((*it).node_key.id_type + ":" +
                    (*it).node_key.id_name);
            list.push_back(data);
        }
    }
    resp->set_context(context());
    resp->Response();
    return;
}

void ShowIFMapAgentStatsReq::HandleRequest() const {

    IFMapAgentParser *parser = ShowIFMapAgentTable::parser_;

    if (!parser)
        return;

    ShowIFMapAgentStatsResp *resp;
    resp = new ShowIFMapAgentStatsResp();

    resp->set_node_updates_processed(parser->node_updates());
    resp->set_node_deletes_processed(parser->node_deletes());
    resp->set_link_updates_processed(parser->link_updates());
    resp->set_link_deletes_processed(parser->link_deletes());
    resp->set_node_update_parse_errors(parser->node_update_parse_errors());
    resp->set_link_update_parse_errors(parser->link_update_parse_errors());
    resp->set_node_delete_parse_errors(parser->node_delete_parse_errors());
    resp->set_link_delete_parse_errors(parser->link_delete_parse_errors());

    resp->set_context(context());
    resp->Response();
    return;
}

void IFMapAgentSandeshInit(DB *db, IFMapAgentParser *parser) {
    ShowIFMapAgentTable::db_ = db;
    ShowIFMapAgentTable::parser_ = parser;
}
