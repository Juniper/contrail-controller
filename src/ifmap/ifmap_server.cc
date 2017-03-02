/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "ifmap/ifmap_server.h"

#include <boost/asio/io_service.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/time_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_graph_edge.h"
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_db_client.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_graph_walker.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/ifmap_uuid_mapper.h"
#include "schema/vnc_cfg_types.h"

#include "sandesh/sandesh.h"
#include "control-node/sandesh/control_node_types.h"

using boost::regex;
using boost::regex_search;
using std::make_pair;

class IFMapServer::IFMapVmSubscribe : public Task {
public:
    IFMapVmSubscribe(DB *db, DBGraph *graph, IFMapServer *server,
                     const std::string &vr_name, const std::string &vm_uuid,
                     bool subscribe, bool has_vms):
            Task(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"), 0),
            db_(db), ifmap_server_(server), vr_name_(vr_name),
            vm_uuid_(vm_uuid), subscribe_(subscribe), has_vms_(has_vms) {
    }

    bool Run() {

        IFMapServerTable *vm_table = static_cast<IFMapServerTable *>(
            db_->FindTable("__ifmap__.virtual_machine.0"));
        assert(vm_table != NULL);

        // We are processing a VM-sub/unsub. If the client is gone by the time
        // we get here, there is nothing to do.
        IFMapClient *client = ifmap_server_->FindClient(vr_name_);
        if (!client) {
            return true;
        }

        // Find the vm's node using its UUID. If the config has not added the
        // vm yet, treat this request as pending since we cant process it right
        // now. If the node is marked deleted, mark it as pending since the
        // node might get revived. In this case, the pending entry will get
        // cleaned up either via an unsub from the client or client-delete.
        IFMapNode *vm_node = ifmap_server_->GetVmNodeByUuid(vm_uuid_);
        if (vm_node && !vm_node->IsDeleted()) {
            IFMapServerTable *vr_table = static_cast<IFMapServerTable *>(
                db_->FindTable("__ifmap__.virtual_router.0"));
            assert(vr_table != NULL);

            std::string vm_name = vm_node->name();
            vr_table->IFMapVmSubscribe(vr_name_, vm_name, subscribe_, has_vms_);

            if (subscribe_) {
                ifmap_server_->ClientGraphDownload(client);
            }
        } else {
            ifmap_server_->ProcessVmRegAsPending(vm_uuid_, vr_name_,
                                                 subscribe_);
        }

        return true;
    }
    std::string Description() const { return "IFMapServer::IFMapVmSubscribe"; }

private:
    DB *db_;
    IFMapServer *ifmap_server_;
    std::string vr_name_;
    std::string vm_uuid_;
    bool subscribe_;
    bool has_vms_;
};

IFMapServer::IFMapServer(DB *db, DBGraph *graph,
                         boost::asio::io_service *io_service)
        : db_(db), graph_(graph),
          queue_(new IFMapUpdateQueue(this)),
          exporter_(new IFMapExporter(this)),
          sender_(new IFMapUpdateSender(this, queue())),
          vm_uuid_mapper_(new IFMapVmUuidMapper(db_, this)),
          work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"),
              0, boost::bind(&IFMapServer::ClientWorker, this, _1)),
          io_service_(io_service), config_manager_(NULL),
          ifmap_channel_manager_(NULL) {
}

IFMapServer::~IFMapServer() {
}

void IFMapServer::Initialize() {
    exporter_->Initialize(db_);
    vm_uuid_mapper_->Initialize();
}

void IFMapServer::Shutdown() {
    vm_uuid_mapper_->Shutdown();
    exporter_->Shutdown();
}

void IFMapServer::ClientRegister(IFMapClient *client) {
    size_t index = client_indexes_.find_first_clear();
    if (index == BitSet::npos) {
        index = client_indexes_.size();
    }
    client_indexes_.set(index);

    std::pair<ClientMap::iterator, bool> cm_ret;
    cm_ret = client_map_.insert(make_pair(client->identifier(), client));
    assert(cm_ret.second);

    std::pair<IndexMap::iterator, bool> im_ret;
    im_ret = index_map_.insert(make_pair(index, client));
    assert(im_ret.second);

    client->Initialize(exporter_.get(), index);
    queue_->Join(index);
    IFMAP_DEBUG(IFMapServerClientRegUnreg, "Register request for client ",
                client->identifier(), index);
}

void IFMapServer::SaveClientHistory(IFMapClient *client) {
    if (client_history_.size() >= kClientHistorySize) {
        // Remove the oldest entry.
        client_history_.pop_front();
    }
    ClientHistoryInfo info(client->identifier(), client->index(),
                           client->created_at(), UTCTimestampUsec());
    client_history_.push_back(info);
}

void IFMapServer::ClientUnregister(IFMapClient *client) {
    IFMAP_DEBUG(IFMapServerClientRegUnreg, "Un-register request for client ",
                client->identifier(), client->index());
    size_t index = client->index();
    sender_->CleanupClient(index);
    queue_->Leave(index);
    ImSz_t iret = index_map_.erase(index);
    assert(iret == 1);
    CmSz_t cret = client_map_.erase(client->identifier());
    assert(cret == 1);
    client_indexes_.reset(index);
}

bool IFMapServer::ProcessClientWork(bool add, IFMapClient *client) {
    if (add) {
        ClientRegister(client);
        ClientExporterSetup(client);
        ClientGraphDownload(client);
    } else {
        RemoveSelfAddedLinksAndObjects(client);
        CleanupUuidMapper(client);
        SaveClientHistory(client);
        int index = client->index();
        ClientUnregister(client);
        // Exporter cleanup must happen after ClientUnregister() which does
        // Q-Leave which needs the config trackers in the exporters.
        ClientExporterCleanup(index);
    }
    return true;
}

// To be used only by tests.
void IFMapServer::AddClient(IFMapClient *client) {
    // Let ClientWorker() do all the work in the context of the db-task
    QueueEntry entry;
    entry.op = ADD;
    entry.client = client;
    work_queue_.Enqueue(entry);
}

// To be used only by tests.
void IFMapServer::DeleteClient(IFMapClient *client) {
    // Let ClientWorker() do all the work in the context of the db-task
    QueueEntry entry;
    entry.op = DELETE;
    entry.client = client;
    work_queue_.Enqueue(entry);
}

// To be used only by tests.
void IFMapServer::SimulateDeleteClient(IFMapClient *client) {
    QueueEntry entry;
    entry.op = DELETE;
    entry.client = client;
    ClientWorker(entry);
}

bool IFMapServer::ClientWorker(QueueEntry work_entry) {
    bool add = (work_entry.op == ADD) ? true : false;
    IFMapClient *client = work_entry.client;

    bool done = ProcessClientWork(add, client);

    return done;
}

// Get the list of subscribed VMs. For each item in the list, if it exist in the
// list of pending vm registration requests, remove it.
void IFMapServer::CleanupUuidMapper(IFMapClient *client) {
    std::vector<std::string> vmlist = client->vm_list();
    for (size_t count = 0; count < vmlist.size(); ++count) {
        vm_uuid_mapper_->CleanupPendingVmRegEntry(vmlist.at(count));
    }
}

void IFMapServer::RemoveSelfAddedLinksAndObjects(IFMapClient *client) {
    IFMapServerTable *vr_table = static_cast<IFMapServerTable *>(
        db_->FindTable("__ifmap__.virtual_router.0"));
    assert(vr_table != NULL);

    IFMapNode *node = vr_table->FindNode(client->identifier());
    if ((node != NULL) && node->IsVertexValid()) {
        IFMapOrigin origin(IFMapOrigin::XMPP);
        for (DBGraphVertex::adjacency_iterator iter = node->begin(graph_), next;
            iter != node->end(graph_); iter = next) {
            IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
            next = ++iter;
            if (adj->table()->name() == "__ifmap__.virtual_machine.0") {
                vr_table->IFMapRemoveVrVmLink(node, adj);
                IFMapServerTable::RemoveObjectAndDeleteNode(adj, origin);
            }
        }
        IFMapServerTable::RemoveObjectAndDeleteNode(node, origin);
    }
}

void IFMapServer::ClientGraphDownload(IFMapClient *client) {
    IFMapTable *table = IFMapTable::FindTable(db_, "virtual-router");
    assert(table);

    IFMapNode *node = table->FindNode(client->identifier());
    if ((node != NULL) && node->IsVertexValid()) {
        for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph_);
            iter != node->edge_list_end(graph_); ++iter) {
            IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
            if (exporter_->FilterNeighbor(node, link)) {
                continue;
            }
            DBTable *link_table = static_cast<DBTable *>(
                db_->FindTable("__ifmap_metadata__.0"));
            link_table->Change(link);
        }
    }
}

void IFMapServer::ClientExporterSetup(IFMapClient *client) {
    exporter_->AddClientConfigTracker(client->index());
}

void IFMapServer::ClientExporterCleanup(int index) {
    exporter_->CleanupClientConfigTrackedEntries(index);
    exporter_->DeleteClientConfigTracker(index);

    BitSet rm_bs;
    rm_bs.set(index);
    exporter_->ResetLinkDeleteClients(rm_bs);
}

IFMapClient *IFMapServer::FindClient(const std::string &id) {
    ClientMap::iterator loc = client_map_.find(id);
    if (loc != client_map_.end()) {
        return loc->second;
    }
    return NULL;
}

IFMapClient *IFMapServer::GetClient(int index) {
    IndexMap::iterator loc = index_map_.find(index);
    if (loc != index_map_.end()) {
        return loc->second;
    }
    return NULL;
}

bool IFMapServer::ClientNameToIndex(const std::string &id, int *index) {
    IFMapClient *client = FindClient(id);
    if (client) {
        *index = client->index();
        return true;
    }
    return false;
}

void IFMapServer::ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                                     bool subscribe, bool has_vms) {
    IFMapVmSubscribe *vm_sub = new IFMapVmSubscribe(db_, graph_, this,
                                                    vr_name, vm_uuid,
                                                    subscribe, has_vms);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(vm_sub);
}

void IFMapServer::ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                                     bool subscribe) {
    IFMapClient *client = FindClient(vr_name);
    assert(client);
    IFMapVmSubscribe *vm_sub =
        new IFMapVmSubscribe(db_, graph_, this, vr_name, vm_uuid, subscribe,
                             client->HasVms());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(vm_sub);
}

void IFMapServer::ProcessVmRegAsPending(std::string vm_uuid,
                                        std::string vr_name, bool subscribe) {
    IFMAP_DEBUG(IFMapServerPendingVmReg, vm_uuid, vr_name, subscribe);
    vm_uuid_mapper_->ProcessVmRegAsPending(vm_uuid, vr_name, subscribe);
}

IFMapNode *IFMapServer::GetVmNodeByUuid(const std::string &vm_uuid) {
    return vm_uuid_mapper_->GetVmNodeByUuid(vm_uuid);
}

void IFMapServer::FillClientMap(IFMapServerShowClientMap *out_map,
                                const std::string &search_string) {
    regex search_expr(search_string);
    out_map->set_table_count(client_map_.size());
    for (ClientMap::const_iterator iter = client_map_.begin();
         iter != client_map_.end(); ++iter) {
        IFMapClient *client = iter->second;
        if (!regex_search(client->identifier(), search_expr)) {
            continue;
        }
        IFMapServerClientMapShowEntry entry;
        entry.set_client_name(client->identifier());
        entry.set_interest_tracker_entries(exporter_->ClientConfigTrackerSize(
                    IFMapExporter::INTEREST, client->index()));
        entry.set_advertised_tracker_entries(exporter_->ClientConfigTrackerSize(
                    IFMapExporter::ADVERTISED, client->index()));
        out_map->clients.push_back(entry);
    }
    out_map->set_print_count(out_map->clients.size());
}

void IFMapServer::FillIndexMap(IFMapServerShowIndexMap *out_map,
                               const std::string &search_string) {
    regex search_expr(search_string);
    out_map->set_table_count(index_map_.size());
    for (IndexMap::const_iterator iter = index_map_.begin();
         iter != index_map_.end(); ++iter) {
        IFMapClient *client = iter->second;
        if (!regex_search(client->identifier(), search_expr)) {
            continue;
        }
        IFMapServerIndexMapShowEntry entry;
        entry.set_client_index(iter->first);
        entry.set_client_name(client->identifier());
        out_map->clients.push_back(entry);
    }
    out_map->set_print_count(out_map->clients.size());
}

const std::string IFMapServer::ClientHistoryInfo::client_created_at_str() const {
    return duration_usecs_to_string(UTCTimestampUsec() - client_created_at);
}

const std::string IFMapServer::ClientHistoryInfo::history_created_at_str() const {
    return duration_usecs_to_string(UTCTimestampUsec() - history_created_at);
}

void IFMapServer::FillClientHistory(IFMapServerClientHistoryList *out_list,
                                    const std::string &search_string) {
    regex search_expr(search_string);
    out_list->set_table_count(client_history_.size());
    for (ClientHistory::const_iterator iter = client_history_.begin();
         iter != client_history_.end(); ++iter) {
        ClientHistoryInfo info = *iter;
        if (!regex_search(info.client_name, search_expr)) {
            continue;
        }
        IFMapServerClientHistoryEntry entry;
        entry.set_client_name(info.client_name);
        entry.set_client_index(info.client_index);
        entry.set_creation_time_ago(info.client_created_at_str());
        entry.set_deletion_time_ago(info.history_created_at_str());
        out_list->clients.push_back(entry);
    }
    out_list->set_print_count(out_list->clients.size());
}

void IFMapServer::GetUIInfo(IFMapServerInfoUI *server_info) const {
    server_info->set_num_peer_clients(GetClientMapSize());
}

bool IFMapServer::CollectStats(BgpRouterState *state, bool first) const {
    CHECK_CONCURRENCY("bgp::ShowCommand");

    ConfigDBConnInfo db_conn_info;
    bool change = false;

    get_config_manager()->config_db_client()->GetConnectionInfo(db_conn_info);
    if (first || db_conn_info != state->get_db_conn_info())  {
        state->set_db_conn_info(db_conn_info);
        change = true;
    }

    ConfigAmqpConnInfo amqp_conn_info;
    get_config_manager()->config_amqp_client()->GetConnectionInfo(amqp_conn_info);
    if (first || amqp_conn_info != state->get_amqp_conn_info())  {
        state->set_amqp_conn_info(amqp_conn_info);
        change = true;
    }

    IFMapServerInfoUI server_info;
    GetUIInfo(&server_info);
    if (first || server_info != state->get_ifmap_server_info()) {
        state->set_ifmap_server_info(server_info);
        change = true;
    }

    return change;
}
