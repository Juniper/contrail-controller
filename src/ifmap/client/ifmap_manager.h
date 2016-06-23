/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_manager_h
#define ctrlplane_ifmap_manager_h

#include "ifmap_channel.h"

#include <boost/function.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/scoped_ptr.hpp>

#include "ifmap/ifmap_config_options.h"

class DiscoveryServiceClient;
class IFMapStateMachine;
class IFMapServer;
class IFMapDSPeerInfo;
class IFMapPeerServerInfoUI;
class PeerIFMapServerFinder;

// This class is the window to the ifmap server for the rest of the system
class IFMapManager {
public:
    typedef boost::function<bool(const char *data, size_t length,
                                 uint64_t sequence_number)> PollReadCb;

    IFMapManager();
    IFMapManager(IFMapServer *ifmap_server,
                 const IFMapConfigOptions& config_options, PollReadCb readcb,
                 boost::asio::io_service *io_service);
    virtual ~IFMapManager();

    void InitializeDiscovery(DiscoveryServiceClient *ds_client,
                             const std::string& url);
    virtual void Start(const std::string &host, const std::string &port);

    boost::asio::io_service *io_service() { return io_service_; }
    IFMapChannel *channel() { return channel_.get(); }
    IFMapStateMachine *state_machine() { return state_machine_.get(); }
    PollReadCb pollreadcb() { return pollreadcb_; }
    void SetChannel(IFMapChannel *channel);
    IFMapServer *ifmap_server() { return ifmap_server_; }
    std::string get_host_port() {
        return channel_->get_host() + ":" + channel_->get_port();
    }
    std::string get_url();
    uint64_t GetChannelSequenceNumber();
    bool GetEndOfRibComputed() const;
    void GetPeerServerInfo(IFMapPeerServerInfoUI &server_info);
    void RetrieveStaticHostPort(const std::string& url);
    void GetAllDSPeerInfo(IFMapDSPeerInfo *ds_peer_info);
    bool get_init_done();
    virtual void ResetConnection(const std::string &host,
                                 const std::string &port);
    bool PeerDown();

private:

    PollReadCb pollreadcb_;
    boost::asio::io_service *io_service_;
    boost::scoped_ptr<IFMapChannel> channel_;
    boost::scoped_ptr<IFMapStateMachine> state_machine_;
    boost::scoped_ptr<PeerIFMapServerFinder> peer_finder_;
    IFMapServer *ifmap_server_;
};

#endif // ctrlplane_ifmap_manager_h
