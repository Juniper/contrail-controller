/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_server__
#define __ctrlplane__ifmap_server__

#include <map>
#include <deque>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/bitset.h"
#include "base/timer.h"
#include "net/address.h"
#include "base/queue_task.h"
#include "ifmap/client/ifmap_manager.h"

class BgpRouterState;
class DB;
class DBGraph;
class DBGraphEdge;
class DBGraphVertex;
class IFMapChannelManager;
class IFMapClient;
class IFMapExporter;
class IFMapNode;
class IFMapUpdateQueue;
class IFMapUpdateSender;
class IFMapVmUuidMapper;
class IFMapServerShowClientMap;
class IFMapServerShowIndexMap;
class IFMapServerClientHistoryList;
class IFMapTableListEntry;
class IFMapNodeTableListShowEntry;
class IFMapServerInfoUI;

class IFMapServer {
public:
    struct ClientHistoryInfo {
        ClientHistoryInfo(const std::string &name, int id, uint64_t ctime,
                          uint64_t htime)
            : client_name(name), client_index(id), client_created_at(ctime),
              history_created_at(htime) {
        }
        const std::string client_created_at_str() const;
        const std::string history_created_at_str() const;

        std::string client_name;
        int client_index;
        uint64_t client_created_at;
        uint64_t history_created_at;
    };

    static const int kClientHistorySize = 5000;
    typedef std::map<std::string, IFMapClient *> ClientMap;
    typedef std::map<int, IFMapClient *> IndexMap;
    typedef std::deque<ClientHistoryInfo> ClientHistory;
    typedef ClientMap::size_type CmSz_t;
    typedef IndexMap::size_type ImSz_t;
    IFMapServer(DB *db, DBGraph *graph, boost::asio::io_service *io_service);
    virtual ~IFMapServer();

    // Must be called after the __ifmap__ tables are registered with the
    // database.
    void Initialize();
    void Shutdown();

    void ClientRegister(IFMapClient *client);
    void ClientUnregister(IFMapClient *client);
    bool ProcessClientWork(bool add, IFMapClient *client);

    IFMapClient *FindClient(const std::string &id);
    IFMapClient *GetClient(int index);

    void AddClient(IFMapClient *client);
    void DeleteClient(IFMapClient *client);
    void ClientExporterSetup(IFMapClient *client);

    DB *database() { return db_; }
    DBGraph *graph() { return graph_; }
    IFMapUpdateQueue *queue() { return queue_.get(); }
    IFMapUpdateSender *sender() { return sender_.get(); }
    IFMapExporter *exporter() { return exporter_.get(); }
    IFMapVmUuidMapper *vm_uuid_mapper() { return vm_uuid_mapper_.get(); }
    boost::asio::io_service *io_service() { return io_service_; }
    void set_ifmap_manager(IFMapManager *manager) {
        ifmap_manager_ = manager;
    }
    IFMapManager *get_ifmap_manager() { return ifmap_manager_; }
    IFMapManager *get_ifmap_manager() const { return ifmap_manager_; }
    virtual uint64_t get_ifmap_channel_sequence_number() {
        return ifmap_manager_->GetChannelSequenceNumber();
    }
    void set_ifmap_channel_manager(IFMapChannelManager *manager) {
        ifmap_channel_manager_ = manager;
    }
    IFMapChannelManager *get_ifmap_channel_manager() {
        return ifmap_channel_manager_;
    }

    void ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                            bool subscribe, bool has_vms);
    void ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                            bool subscribe);

    class IFMapStaleEntriesCleaner;
    class IFMapVmSubscribe;

    void ProcessVmRegAsPending(std::string vm_uuid, std::string vr_name,
                               bool subscribe);
    IFMapNode *GetVmNodeByUuid(const std::string &vm_uuid);

    void FillClientMap(IFMapServerShowClientMap *out_map,
                       const std::string &search_string);
    void FillIndexMap(IFMapServerShowIndexMap *out_map,
                      const std::string &search_string);
    void FillClientHistory(IFMapServerClientHistoryList *out_list,
                           const std::string &search_string);
    const CmSz_t GetClientMapSize() const { return client_map_.size(); }
    const CmSz_t GetIndexMapSize() const { return index_map_.size(); }
    void GetUIInfo(IFMapServerInfoUI *server_info) const;
    bool ClientNameToIndex(const std::string &id, int *index);
    bool ProcessStaleEntriesTimeout();
    bool CollectStats(BgpRouterState *state, bool first) const;

private:
    friend class IFMapServerTest;
    friend class IFMapRestartTest;
    friend class ShowIFMapXmppClientInfo;
    friend class XmppIfmapTest;
    friend class IFMapExporterTest;
    friend class IFMapVmUuidMapperTest;

    enum QueueOp {
        ADD = 1,
        DELETE = 2
    };
    struct QueueEntry {
        QueueOp op;
        IFMapClient *client;
    };
    bool ClientWorker(QueueEntry work_entry);
    void ClientGraphDownload(IFMapClient *client);
    void RemoveSelfAddedLinksAndObjects(IFMapClient *client);
    void CleanupUuidMapper(IFMapClient *client);
    void ClientExporterCleanup(int index);
    const ClientMap &GetClientMap() const { return client_map_; }
    void SimulateDeleteClient(IFMapClient *client);
    void SaveClientHistory(IFMapClient *client);

    DB *db_;
    DBGraph *graph_;
    boost::scoped_ptr<IFMapUpdateQueue> queue_;
    boost::scoped_ptr<IFMapExporter> exporter_;
    boost::scoped_ptr<IFMapUpdateSender> sender_;
    boost::scoped_ptr<IFMapVmUuidMapper> vm_uuid_mapper_;
    BitSet client_indexes_;
    ClientMap client_map_;
    IndexMap index_map_;
    WorkQueue<QueueEntry> work_queue_;
    boost::asio::io_service *io_service_;
    IFMapManager *ifmap_manager_;
    IFMapChannelManager *ifmap_channel_manager_;
    ClientHistory client_history_;
};

#endif /* defined(__ctrlplane__ifmap_server__) */
