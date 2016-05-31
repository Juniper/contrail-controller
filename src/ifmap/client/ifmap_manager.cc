/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_manager.h"
#include "ifmap_state_machine.h"
#include "ifmap/ifmap_server.h"
#include "peer_server_finder.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "ifmap/ifmap_server_show_types.h"

#include <boost/function.hpp>
#include <boost/asio/io_service.hpp>

IFMapManager::IFMapManager()
    : pollreadcb_(NULL), io_service_(NULL), channel_(NULL),
      state_machine_(NULL), peer_finder_(NULL), ifmap_server_(NULL) {
}

IFMapManager::IFMapManager(IFMapServer *ifmap_server,
        const IFMapConfigOptions& config_options, PollReadCb readcb,
        boost::asio::io_service *io_service)
    : pollreadcb_(readcb), io_service_(io_service),
      channel_(new IFMapChannel(this, config_options)),
      state_machine_(new IFMapStateMachine(this, config_options)),
      ifmap_server_(ifmap_server) {
    ifmap_server->set_ifmap_manager(this);
}

IFMapManager::~IFMapManager() {
}

void IFMapManager::InitializeDiscovery(DiscoveryServiceClient *ds_client,
                                       const std::string& url) {
    peer_finder_.reset(new PeerIFMapServerFinder(this, ds_client, url));
}

void IFMapManager::Start(const std::string &host, const std::string &port) {
    channel_->SetHostPort(host, port);
    channel_->set_sm(state_machine_.get());

    state_machine_->set_channel(channel_.get());
    state_machine_->Start();
}

void IFMapManager::SetChannel(IFMapChannel *channel) {
    channel_.reset(channel);
}

uint64_t IFMapManager::GetChannelSequenceNumber() {
    return channel_->get_sequence_number();
}

bool IFMapManager::GetEndOfRibComputed() const {
    return channel_->end_of_rib_computed();
}

void IFMapManager::GetPeerServerInfo(IFMapPeerServerInfoUI *server_info) {
    server_info->set_url(get_host_port());
    server_info->set_connection_status(channel_->get_connection_status());
    server_info->set_connection_status_change_at(
        channel_->get_connection_status_change_at());
}

void IFMapManager::GetAllDSPeerInfo(IFMapDSPeerInfo *ds_peer_info) {
    peer_finder_->GetAllDSPeerInfo(ds_peer_info);
}

void IFMapManager::ResetConnection(const std::string &host,
                                   const std::string &port) {
    state_machine_->ResetConnectionReqEnqueue(host, port);
}

bool IFMapManager::PeerDown() {
    return peer_finder_->PeerDown();
}

std::string IFMapManager::get_url() {
    return peer_finder_->get_url();
}

bool IFMapManager::get_init_done() {
    return peer_finder_->get_init_done();
}
