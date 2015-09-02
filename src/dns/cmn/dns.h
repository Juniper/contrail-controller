/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_CMN_H_
#define __DNS_CMN_H_

#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>

#include <io/event_manager.h>
#include <base/logging.h>
#include <net/address.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <base/task.h>
#include <base/task_trigger.h>
#include <io/event_manager.h>
#include <base/misc_utils.h>
#include "discovery_client.h"
#include <sandesh/sandesh_trace.h>

class EventManager;
class DnsManager;
class DnsConfigManager;
class XmppServer;
class DiscoveryCfgXmppChannel;
class DnsAgentXmppChannelManager;
class DiscoveryServiceClient;

class Dns {
public:
    static EventManager *GetEventManager() { return event_mgr_; }
    static void SetEventManager(EventManager *evm) { event_mgr_ = evm; }

    static DnsManager *GetDnsManager() { return dns_mgr_; }
    static void SetDnsManager(DnsManager *mgr) { dns_mgr_ = mgr; }

    static DnsConfigManager *GetDnsConfigManager() { return dns_config_mgr_; }
    static void SetDnsConfigManager(DnsConfigManager *cfg) { dns_config_mgr_ = cfg; }

    static XmppServer *GetXmppServer() { return xmpp_server_; }
    static void SetXmppServer(XmppServer *server) { xmpp_server_ = server; }

    static const uint32_t GetXmppServerPort() { return xmpp_srv_port_; }
    static void SetXmppServerPort(uint32_t port) { xmpp_srv_port_ = port; }

    static DnsAgentXmppChannelManager *GetAgentXmppChannelManager() {
        return agent_xmpp_channel_mgr_;
    }
    static void SetAgentXmppChannelManager(DnsAgentXmppChannelManager *mgr) {
        agent_xmpp_channel_mgr_ = mgr;
    }

    static const std::string &GetHostName() {return host_name_;}
    static void SetHostName(const std::string name) { host_name_ = name; };
    static const std::string &GetProgramName() {return prog_name_;}
    static void SetProgramName(const char *name) { prog_name_ = name; };
    static const std::string &GetCollector() {return collector_;}
    static void SetCollector(std::string name) { collector_ = name; };
    static const uint32_t GetHttpPort() { return http_port_; }
    static void SetHttpPort(uint32_t port) { http_port_ = port; };
    static const uint32_t GetDnsPort() { return dns_port_; }
    static void SetDnsPort(uint32_t port) { dns_port_ = port; };
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }

    static bool GetVersion(std::string &build_info_str);
    static void Init() {
        EventManager *evm;
        evm = new EventManager();
        assert(evm);
        SetEventManager(evm);

        SetTaskSchedulingPolicy();
    }
    
    static void ShutdownDiscoveryClient(DiscoveryServiceClient *);

    static void SetDiscoveryServiceClient(DiscoveryServiceClient *ds) {
        ds_client_ = ds;
    }
    static DiscoveryServiceClient *GetDnsDiscoveryServiceClient() {
        return ds_client_;
    }

private:
    static void SetTaskSchedulingPolicy();

    static EventManager *event_mgr_;
    static DnsManager *dns_mgr_;
    static DnsConfigManager *dns_config_mgr_;

    static XmppServer *xmpp_server_;
    static uint32_t xmpp_srv_port_;
    static std::string host_name_;
    static std::string prog_name_;
    static std::string self_ip_;
    static std::string collector_;
    static uint32_t http_port_;
    static uint32_t dns_port_;
    static DnsAgentXmppChannelManager *agent_xmpp_channel_mgr_;
    static DiscoveryServiceClient *ds_client_;
};

#endif // __DNS_CMN_H_
