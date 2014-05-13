/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include "base/logging.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_graph_vertex.h"
#include "db/db_table_partition.h"
#include "base/bitset.h"

#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_object.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_uuid_mapper.h"

#include "bgp/bgp_sandesh.h"
#include <pugixml/pugixml.hpp>

using namespace boost::assign;
using namespace std;
using namespace pugi;

class IFMapNodeCopier {
public:
    IFMapNodeCopier(IFMapNodeShowInfo *dest, DBEntryBase *src,
                    IFMapServer *server) {
        // Get the name of the node
        IFMapNode *src_node = static_cast<IFMapNode *>(src);
        dest->node_name = src_node->ToString();

        IFMapNodeState *state = server->exporter()->NodeStateLookup(src_node);

        if (state) {
            // Get the interests and advertised from state
            dest->interests = state->interest().ToNumberedString();
            dest->advertised = state->advertised().ToNumberedString();
        } else {
            dest->dbentryflags.append("No state, ");
        }

        if (src_node->IsDeleted()) {
            dest->dbentryflags.append("Deleted, ");
        }
        if (src_node->is_onlist()) {
            dest->dbentryflags.append("OnList, ");
        }
        if (src_node->IsOnRemoveQ()) {
            dest->dbentryflags.append("OnRemoveQ");
        }

        dest->obj_info.reserve(src_node->list_.size());
        for (IFMapNode::ObjectList::const_iterator iter = 
             src_node->list_.begin(); iter != src_node->list_.end(); ++iter) {
            const IFMapObject *src_obj = iter.operator->();

            IFMapObjectShowInfo dest_obj;
            dest_obj.sequence_number = src_obj->sequence_number();
            dest_obj.origin = src_obj->origin().ToString();
            dest_obj.data = GetIFMapObjectData(src_obj);

            dest->obj_info.push_back(dest_obj);
        }
        if (src_node->IsVertexValid()) {
            DBGraph *graph = server->graph();
            int neighbor_count = 0;
            for (DBGraphVertex::adjacency_iterator iter =
                src_node->begin(graph); iter != src_node->end(graph); ++iter) {
                ++neighbor_count;
            }
            dest->neighbors.reserve(neighbor_count);
            for (DBGraphVertex::adjacency_iterator iter =
                src_node->begin(graph); iter != src_node->end(graph); ++iter) {
                IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
                dest->neighbors.push_back(adj->ToString());
            }
        }
        dest->last_modified = src_node->last_change_at_str();
    }

    string GetIFMapObjectData(const IFMapObject *src_obj) {
        pugi::xml_document xdoc;
        pugi::xml_node xnode = xdoc.append_child("iq");
        src_obj->EncodeUpdate(&xnode);

        ostringstream oss;
        xnode.print(oss);
        string objdata = oss.str();
        return objdata;
    }
};

// almost everything in this class is static since we dont really want to
// intantiate this class
class ShowIFMapTable {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapNodeShowInfo> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapNodeShowInfo>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
    static void TableToBuffer(IFMapTable *table, ShowData *show_data,
                              IFMapServer *server);
    static bool BufferAllTables(const RequestPipeline::PipeSpec ps,
                                RequestPipeline::InstData *data);
    static bool BufferSomeTables(const RequestPipeline::PipeSpec ps,
                                 RequestPipeline::InstData *data);
};

void ShowIFMapTable::TableToBuffer(IFMapTable *table, ShowData *show_data,
        IFMapServer *server) {
    for (int i = 0; i < IFMapTable::kPartitionCount; i++) {
        DBTablePartBase *partition = table->GetTablePartition(i);
        DBEntryBase *src = partition->GetFirst();
        while (src) {
            IFMapNodeShowInfo dest;
            IFMapNodeCopier copyNode(&dest, src, server);
            show_data->send_buffer.push_back(dest);
            src = partition->GetNext(src);
        }
    }
}

bool ShowIFMapTable::BufferSomeTables(const RequestPipeline::PipeSpec ps,
                                      RequestPipeline::InstData *data) {
    const IFMapTableShowReq *request = 
        static_cast<const IFMapTableShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());

    IFMapTable *table = IFMapTable::FindTable(bsc->ifmap_server->database(),
                                              request->get_table_name());
    if (table) {
        ShowData *show_data = static_cast<ShowData *>(data);
        show_data->send_buffer.reserve(table->Size());
        TableToBuffer(table, show_data, bsc->ifmap_server);
    } else {
        IFMAP_WARN(IFMapTblNotFound, "Cant show/find table ",
                   request->get_table_name());
    }

    return true;
}

bool ShowIFMapTable::BufferAllTables(const RequestPipeline::PipeSpec ps,
                                     RequestPipeline::InstData *data) {
    const IFMapTableShowReq *request = 
        static_cast<const IFMapTableShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());

    DB *db = bsc->ifmap_server->database();

    // Get the sum total of entries in all the tables
    int num_entries = 0;
    for (DB::iterator iter = db->lower_bound("__ifmap__.");
         iter != db->end(); ++iter) {
        if (iter->first.find("__ifmap__.") != 0) {
            break;
        }
        IFMapTable *table = static_cast<IFMapTable *>(iter->second);
        num_entries += table->Size();
    }
    ShowData *show_data = static_cast<ShowData *>(data);
    show_data->send_buffer.reserve(num_entries);
    for (DB::iterator iter = db->lower_bound("__ifmap__.");
         iter != db->end(); ++iter) {
        if (iter->first.find("__ifmap__.") != 0) {
            break;
        }
        IFMapTable *table = static_cast<IFMapTable *>(iter->second);
        TableToBuffer(table, show_data, bsc->ifmap_server);
    }

    return true;
}

bool ShowIFMapTable::BufferStage(const Sandesh *sr,
                                 const RequestPipeline::PipeSpec ps,
                                 int stage, int instNum,
                                 RequestPipeline::InstData *data) {

    const IFMapTableShowReq *request = 
        static_cast<const IFMapTableShowReq *>(ps.snhRequest_.get());
    // If table name has not been passed, print all tables
    if (request->get_table_name().length()) {
        return BufferSomeTables(ps, data);
    } else {
        return BufferAllTables(ps, data);
    }
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapTable::SendStage(const Sandesh *sr,
                               const RequestPipeline::PipeSpec ps,
                               int stage, int instNum,
                               RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapTable::ShowData &show_data = 
        static_cast<const ShowIFMapTable::ShowData &> (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapNodeShowInfo> dest_buffer;
    vector<IFMapNodeShowInfo>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapTableShowReq *request = 
        static_cast<const IFMapTableShowReq *>(ps.snhRequest_.get());
    IFMapTableShowResp *response = new IFMapTableShowResp();
    response->set_ifmap_db(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapTableShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapTable::AllocBuffer;
    s0.cbFn_ = ShowIFMapTable::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapTable::AllocTracker;
    s1.cbFn_ = ShowIFMapTable::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

// Code to display link-table entries

class ShowIFMapLinkTable {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapLinkShowInfo> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapLinkShowInfo>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static void CopyNode(IFMapLinkShowInfo *dest, DBEntryBase *src,
                         IFMapServer *server);
    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

void ShowIFMapLinkTable::CopyNode(IFMapLinkShowInfo *dest, DBEntryBase *src,
                                  IFMapServer *server) {
    IFMapLink *src_link = static_cast<IFMapLink *>(src);

    IFMapLinkState *state = server->exporter()->LinkStateLookup(src_link);

    dest->metadata = src_link->metadata();
    if (src_link->left()) {
        dest->left = src_link->left()->ToString();
    }
    if (src_link->right()) {
        dest->right = src_link->right()->ToString();
    }

    // Get the interests and advertised from state
    dest->interests = state->interest().ToNumberedString();
    dest->advertised = state->advertised().ToNumberedString();

    if (src_link->IsDeleted()) {
        dest->dbentryflags.append("Deleted, ");
    }
    if (src_link->is_onlist()) {
        dest->dbentryflags.append("OnList");
    }
    if (src_link->IsOnRemoveQ()) {
        dest->dbentryflags.append("OnRemoveQ");
    }
    dest->last_modified = src_link->last_change_at_str();

    for (std::vector<IFMapLink::LinkOriginInfo>::const_iterator iter = 
         src_link->origin_info_.begin(); iter != src_link->origin_info_.end();
         ++iter) {
        const IFMapLink::LinkOriginInfo *origin_info = iter.operator->();
        IFMapLinkOriginShowInfo dest_origin;
        dest_origin.sequence_number = origin_info->sequence_number;
        dest_origin.origin = origin_info->origin.ToString();
        dest->origins.push_back(dest_origin);
    }
}

bool ShowIFMapLinkTable::BufferStage(const Sandesh *sr,
                                     const RequestPipeline::PipeSpec ps,
                                     int stage, int instNum,
                                     RequestPipeline::InstData *data) {
    const IFMapLinkTableShowReq *request = 
        static_cast<const IFMapLinkTableShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());

    IFMapLinkTable *table =  static_cast<IFMapLinkTable *>(
        bsc->ifmap_server->database()->FindTable("__ifmap_metadata__.0"));
    if (table) {
        ShowData *show_data = static_cast<ShowData *>(data);
        show_data->send_buffer.reserve(table->Size());

        DBTablePartBase *partition = table->GetTablePartition(0);
        DBEntryBase *src = partition->GetFirst();
        while (src) {
            IFMapLinkShowInfo dest;
            CopyNode(&dest, src, bsc->ifmap_server);
            show_data->send_buffer.push_back(dest);
            src = partition->GetNext(src);
        }
    } else {
        IFMAP_WARN(IFMapTblNotFound, "Cant show/find ", "link table");
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapLinkTable::SendStage(const Sandesh *sr,
                                   const RequestPipeline::PipeSpec ps,
                                   int stage, int instNum,
                                   RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapLinkTable::ShowData &show_data = 
        static_cast<const ShowIFMapLinkTable::ShowData &>
                                                       (prev_stage_data->at(0));

    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapLinkShowInfo> dest_buffer;
    vector<IFMapLinkShowInfo>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapLinkTableShowReq *request = 
        static_cast<const IFMapLinkTableShowReq *>(ps.snhRequest_.get());
    IFMapLinkTableShowResp *response = new IFMapLinkTableShowResp();
    response->set_ifmap_db(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapLinkTableShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapLinkTable::AllocBuffer;
    s0.cbFn_ = ShowIFMapLinkTable::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapLinkTable::AllocTracker;
    s1.cbFn_ = ShowIFMapLinkTable::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

static bool IFMapNodeShowReqHandleRequest(const Sandesh *sr,
                                          const RequestPipeline::PipeSpec ps,
                                          int stage, int instNum,
                                          RequestPipeline::InstData *data) {
    const IFMapNodeShowReq *request =
        static_cast<const IFMapNodeShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());

    IFMapNodeShowResp *response = new IFMapNodeShowResp();

    string fq_node_name = request->get_fq_node_name();
    // EG: "virtual-network:my:virtual:network" i.e. type:name
    size_t type_length = fq_node_name.find(":");
    if (type_length != string::npos) {
        string node_type = fq_node_name.substr(0, type_length);
        // +1 to go to the next character after ':'
        string node_name = fq_node_name.substr(type_length + 1);

        DB *db = bsc->ifmap_server->database();
        IFMapTable *table = IFMapTable::FindTable(db, node_type);
        if (table) {
            IFMapNode *src = table->FindNode(node_name);
            if (src) {
                IFMapNodeShowInfo dest;
                IFMapNodeCopier copyNode(&dest, src, bsc->ifmap_server);
                response->set_node_info(dest);
            } else {
                IFMAP_WARN(IFMapIdentifierNotFound, "Cant find identifier",
                           node_name);
            }
        } else {
            IFMAP_WARN(IFMapTblNotFound, "Cant show/find table with node-type",
                       node_type);
        }
    }

    response->set_context(request->context());
    response->set_more(false);
    response->Response();

    // Return 'true' so that we are not called again
    return true;
}

void IFMapNodeShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.cbFn_ = IFMapNodeShowReqHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0);
    RequestPipeline rp(ps);
}

class ShowIFMapPerClientNodes {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapPerClientNodesShowInfo> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there
        // is no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapPerClientNodesShowInfo>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool CopyNode(IFMapPerClientNodesShowInfo *dest, DBEntryBase *src,
                         IFMapServer *server, int client_index);
    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

bool ShowIFMapPerClientNodes::CopyNode(IFMapPerClientNodesShowInfo *dest,
                                       DBEntryBase *src, IFMapServer *server,
                                       int client_index) {
    IFMapNode *src_node = static_cast<IFMapNode *>(src);

    IFMapNodeState *state = server->exporter()->NodeStateLookup(src_node);

    if (state->interest().test(client_index)) {
        dest->node_name = src_node->ToString();
        if (state->advertised().test(client_index)) {
            dest->sent = "Yes";
        } else {
            dest->sent = "No";
        }
        return true;
    } else {
        return false;
    }
}

bool ShowIFMapPerClientNodes::BufferStage(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage,
        int instNum, RequestPipeline::InstData *data) {
    const IFMapPerClientNodesShowReq *request =
        static_cast<const IFMapPerClientNodesShowReq *>(ps.snhRequest_.get());
    int client_index = request->get_client_index();
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(request->client_context());

    ShowData *show_data = static_cast<ShowData *>(data);
    DB *db = bsc->ifmap_server->database();
    for (DB::iterator iter = db->lower_bound("__ifmap__.");
         iter != db->end(); ++iter) {
        if (iter->first.find("__ifmap__.") != 0) {
            break;
        }
        IFMapTable *table = static_cast<IFMapTable *>(iter->second);

        for (int i = 0; i < IFMapTable::kPartitionCount; i++) {
            DBTablePartBase *partition = table->GetTablePartition(i);
            DBEntryBase *src = partition->GetFirst();
            while (src) {
                IFMapPerClientNodesShowInfo dest;
                bool send = CopyNode(&dest, src, bsc->ifmap_server,
                                     client_index);
                if (send) {
                    show_data->send_buffer.push_back(dest);
                }
                src = partition->GetNext(src);
            }
        }
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapPerClientNodes::SendStage(const Sandesh *sr,
                                        const RequestPipeline::PipeSpec ps,
                                        int stage, int instNum,
                                        RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapPerClientNodes::ShowData &show_data = 
        static_cast<const ShowIFMapPerClientNodes::ShowData &> 
        (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapPerClientNodesShowInfo> dest_buffer;
    vector<IFMapPerClientNodesShowInfo>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapPerClientNodesShowReq *request = 
        static_cast<const IFMapPerClientNodesShowReq *>(ps.snhRequest_.get());
    IFMapPerClientNodesShowResp *response = new IFMapPerClientNodesShowResp();
    response->set_node_db(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapPerClientNodesShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapPerClientNodes::AllocBuffer;
    s0.cbFn_ = ShowIFMapPerClientNodes::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapPerClientNodes::AllocTracker;
    s1.cbFn_ = ShowIFMapPerClientNodes::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

class ShowIFMapPerClientLinkTable {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapPerClientLinksShowInfo> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there
        // is no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapPerClientLinksShowInfo>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool CopyNode(IFMapPerClientLinksShowInfo *dest, DBEntryBase *src,
                         IFMapServer *server, int client_index);
    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

bool ShowIFMapPerClientLinkTable::CopyNode(IFMapPerClientLinksShowInfo *dest,
        DBEntryBase *src, IFMapServer *server, int client_index) {
    IFMapLink *src_link = static_cast<IFMapLink *>(src);

    IFMapLinkState *state = server->exporter()->LinkStateLookup(src_link);

    if (state->interest().test(client_index)) {
        dest->metadata = src_link->metadata();
        dest->left = src_link->left()->ToString();
        dest->right = src_link->right()->ToString();
        if (state->advertised().test(client_index)) {
            dest->sent = "Yes";
        } else {
            dest->sent = "No";
        }
        return true;
    } else {
        return false;
    }
}

bool ShowIFMapPerClientLinkTable::BufferStage(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {
    const IFMapPerClientLinksShowReq *request = 
        static_cast<const IFMapPerClientLinksShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    int client_index = request->get_client_index();

    IFMapLinkTable *table =  static_cast<IFMapLinkTable *>(
        bsc->ifmap_server->database()->FindTable("__ifmap_metadata__.0"));
    if (table) {
        ShowData *show_data = static_cast<ShowData *>(data);
        show_data->send_buffer.reserve(table->Size());

        DBTablePartBase *partition = table->GetTablePartition(0);
        DBEntryBase *src = partition->GetFirst();
        while (src) {
            IFMapPerClientLinksShowInfo dest;
            bool send = CopyNode(&dest, src, bsc->ifmap_server, client_index);
            if (send) {
                show_data->send_buffer.push_back(dest);
            }
            src = partition->GetNext(src);
        }
    } else {
        IFMAP_WARN(IFMapTblNotFound, "Cant show/find ", "link table");
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapPerClientLinkTable::SendStage(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapPerClientLinkTable::ShowData &show_data = 
        static_cast<const ShowIFMapPerClientLinkTable::ShowData &>
            (prev_stage_data->at(0));

    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapPerClientLinksShowInfo> dest_buffer;
    vector<IFMapPerClientLinksShowInfo>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapPerClientLinksShowReq *request = 
        static_cast<const IFMapPerClientLinksShowReq *>(ps.snhRequest_.get());
    IFMapPerClientLinksShowResp *response = new IFMapPerClientLinksShowResp();
    response->set_link_db(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapPerClientLinksShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapPerClientLinkTable::AllocBuffer;
    s0.cbFn_ = ShowIFMapPerClientLinkTable::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapPerClientLinkTable::AllocTracker;
    s1.cbFn_ = ShowIFMapPerClientLinkTable::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

class ShowIFMapUuidToNodeMapping {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapUuidToNodeMappingEntry> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapUuidToNodeMappingEntry>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

bool ShowIFMapUuidToNodeMapping::BufferStage(const Sandesh *sr,
                                             const RequestPipeline::PipeSpec ps,
                                             int stage, int instNum,
                                             RequestPipeline::InstData *data) {
    const IFMapUuidToNodeMappingReq *request = 
        static_cast<const IFMapUuidToNodeMappingReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    ShowData *show_data = static_cast<ShowData *>(data);

    IFMapVmUuidMapper *mapper = bsc->ifmap_server->vm_uuid_mapper();
    IFMapUuidMapper &uuid_mapper = mapper->uuid_mapper_;

    show_data->send_buffer.reserve(uuid_mapper.Size());
    for (IFMapUuidMapper::UuidNodeMap::const_iterator iter = 
                                        uuid_mapper.uuid_node_map_.begin();
         iter != uuid_mapper.uuid_node_map_.end(); ++iter) {
        IFMapUuidToNodeMappingEntry dest;
        dest.set_uuid(iter->first);
        IFMapNode *node = static_cast<IFMapNode *>(iter->second);
        dest.set_node_name(node->ToString());
        show_data->send_buffer.push_back(dest);
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapUuidToNodeMapping::SendStage(const Sandesh *sr,
                                           const RequestPipeline::PipeSpec ps,
                                           int stage, int instNum,
                                           RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapUuidToNodeMapping::ShowData &show_data = 
        static_cast<const ShowIFMapUuidToNodeMapping::ShowData &> 
        (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapUuidToNodeMappingEntry> dest_buffer;
    vector<IFMapUuidToNodeMappingEntry>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapUuidToNodeMappingReq *request = 
        static_cast<const IFMapUuidToNodeMappingReq *>(ps.snhRequest_.get());
    IFMapUuidToNodeMappingResp *response = new IFMapUuidToNodeMappingResp();
    response->set_map_count(dest_buffer.size());
    response->set_uuid_to_node_map(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapUuidToNodeMappingReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapUuidToNodeMapping::AllocBuffer;
    s0.cbFn_ = ShowIFMapUuidToNodeMapping::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapUuidToNodeMapping::AllocTracker;
    s1.cbFn_ = ShowIFMapUuidToNodeMapping::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

class ShowIFMapNodeToUuidMapping {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapNodeToUuidMappingEntry> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapNodeToUuidMappingEntry>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

bool ShowIFMapNodeToUuidMapping::BufferStage(const Sandesh *sr,
                                             const RequestPipeline::PipeSpec ps,
                                             int stage, int instNum,
                                             RequestPipeline::InstData *data) {
    const IFMapNodeToUuidMappingReq *request = 
        static_cast<const IFMapNodeToUuidMappingReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    ShowData *show_data = static_cast<ShowData *>(data);

    IFMapVmUuidMapper *mapper = bsc->ifmap_server->vm_uuid_mapper();

    show_data->send_buffer.reserve(mapper->NodeUuidMapCount());
    for (IFMapVmUuidMapper::NodeUuidMap::const_iterator iter = 
                                        mapper->node_uuid_map_.begin();
         iter != mapper->node_uuid_map_.end(); ++iter) {
        IFMapNodeToUuidMappingEntry dest;
        IFMapNode *node = static_cast<IFMapNode *>(iter->first);
        dest.set_node_name(node->ToString());
        dest.set_uuid(iter->second);
        show_data->send_buffer.push_back(dest);
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapNodeToUuidMapping::SendStage(const Sandesh *sr,
                                           const RequestPipeline::PipeSpec ps,
                                           int stage, int instNum,
                                           RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapNodeToUuidMapping::ShowData &show_data = 
        static_cast<const ShowIFMapNodeToUuidMapping::ShowData &> 
        (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapNodeToUuidMappingEntry> dest_buffer;
    vector<IFMapNodeToUuidMappingEntry>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapNodeToUuidMappingReq *request = 
        static_cast<const IFMapNodeToUuidMappingReq *>(ps.snhRequest_.get());
    IFMapNodeToUuidMappingResp *response = new IFMapNodeToUuidMappingResp();
    response->set_map_count(dest_buffer.size());
    response->set_node_to_uuid_map(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapNodeToUuidMappingReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapNodeToUuidMapping::AllocBuffer;
    s0.cbFn_ = ShowIFMapNodeToUuidMapping::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapNodeToUuidMapping::AllocTracker;
    s1.cbFn_ = ShowIFMapNodeToUuidMapping::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

class ShowIFMapPendingVmReg {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapPendingVmRegEntry> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapPendingVmRegEntry>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

bool ShowIFMapPendingVmReg::BufferStage(const Sandesh *sr,
                                        const RequestPipeline::PipeSpec ps,
                                        int stage, int instNum,
                                        RequestPipeline::InstData *data) {
    const IFMapPendingVmRegReq *request = 
        static_cast<const IFMapPendingVmRegReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    ShowData *show_data = static_cast<ShowData *>(data);

    IFMapVmUuidMapper *mapper = bsc->ifmap_server->vm_uuid_mapper();

    show_data->send_buffer.reserve(mapper->PendingVmRegCount());
    for (IFMapVmUuidMapper::PendingVmRegMap::const_iterator iter = 
                                        mapper->pending_vmreg_map_.begin();
         iter != mapper->pending_vmreg_map_.end(); ++iter) {
        IFMapPendingVmRegEntry dest;
        dest.set_vm_uuid(iter->first);
        dest.set_vr_name(iter->second);
        show_data->send_buffer.push_back(dest);
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapPendingVmReg::SendStage(const Sandesh *sr,
                                      const RequestPipeline::PipeSpec ps,
                                      int stage, int instNum,
                                      RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapPendingVmReg::ShowData &show_data = 
        static_cast<const ShowIFMapPendingVmReg::ShowData &> 
        (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapPendingVmRegEntry> dest_buffer;
    vector<IFMapPendingVmRegEntry>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapPendingVmRegReq *request = 
        static_cast<const IFMapPendingVmRegReq *>(ps.snhRequest_.get());
    IFMapPendingVmRegResp *response = new IFMapPendingVmRegResp();
    response->set_map_count(dest_buffer.size());
    response->set_vm_reg_map(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapPendingVmRegReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapPendingVmReg::AllocBuffer;
    s0.cbFn_ = ShowIFMapPendingVmReg::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapPendingVmReg::AllocTracker;
    s1.cbFn_ = ShowIFMapPendingVmReg::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}

static bool IFMapServerClientShowReqHandleRequest(const Sandesh *sr,
                const RequestPipeline::PipeSpec ps, int stage, int instNum,
                RequestPipeline::InstData *data) {
    const IFMapServerClientShowReq *request =
        static_cast<const IFMapServerClientShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(request->client_context());

    IFMapServerClientShowResp *response = new IFMapServerClientShowResp();

    IFMapServerShowClientMap name_list;
    bsc->ifmap_server->FillClientMap(&name_list);
    IFMapServerShowIndexMap index_list;
    bsc->ifmap_server->FillIndexMap(&index_list);

    response->set_name_list(name_list);
    response->set_index_list(index_list);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();

    // Return 'true' so that we are not called again
    return true;
}

void IFMapServerClientShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.cbFn_ = IFMapServerClientShowReqHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0);
    RequestPipeline rp(ps);
}

static bool IFMapNodeTableListShowReqHandleRequest(const Sandesh *sr,
                const RequestPipeline::PipeSpec ps, int stage, int instNum,
                RequestPipeline::InstData *data) {
    const IFMapNodeTableListShowReq *request =
        static_cast<const IFMapNodeTableListShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(request->client_context());

    vector<IFMapNodeTableListShowEntry> dest_buffer;
    IFMapTable::FillNodeTableList(bsc->ifmap_server->database(),
                                  &dest_buffer);

    IFMapNodeTableListShowResp *response = new IFMapNodeTableListShowResp();
    response->set_table_list(dest_buffer);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();

    // Return 'true' so that we are not called again
    return true;
}

void IFMapNodeTableListShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.cbFn_ = IFMapNodeTableListShowReqHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0);
    RequestPipeline rp(ps);
}

