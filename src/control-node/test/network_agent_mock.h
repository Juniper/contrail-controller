/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__network_agent_mock__
#define __ctrlplane__network_agent_mock__

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <map>
#include <pugixml/pugixml.hpp>
#include <tbb/compat/condition_variable>
#include <tbb/mutex.h>

#include "base/queue_task.h"
#include "bgp/extended-community/load_balance.h"

namespace autogen {
struct ItemType;
struct EnetItemType;
struct McastItemType;
class VirtualRouter;
class VirtualMachine;
}

namespace pugi {
class xml_document;
class xml_node;
}

class EventManager;
class XmppChannelConfig;
class XmppClient;
class BgpXmppChannelManager;

namespace test {

enum TestErrorType {
    ROUTE_AF_ERROR,
    ROUTE_SAFI_ERROR,
    XML_TOKEN_ERROR,
};

struct RouteParams {
    RouteParams()
        : edge_replication_not_supported(false),
          assisted_replication_supported(false) {
    };
    bool edge_replication_not_supported;
    bool assisted_replication_supported;
    std::string replicator_address;
};

struct RouteAttributes {
public:
    static const int kDefaultLocalPref = 100;
    static const int kDefaultMed = 200;
    static const int kDefaultSequence = 0;
    RouteAttributes()
        : local_pref(kDefaultLocalPref),
          med(kDefaultMed),
          sequence(kDefaultSequence),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t seq, const std::vector<int> &sg)
        : local_pref(lpref),
          med(0),
          sequence(seq),
          sgids(sg),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t seq)
        : local_pref(lpref),
          med(0),
          sequence(seq),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t med, uint32_t seq)
        : local_pref(lpref),
          med(med),
          sequence(seq),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref)
        : local_pref(lpref),
          med(0),
          sequence(kDefaultSequence),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(const std::vector<int> &sg)
        : local_pref(kDefaultLocalPref),
          med(0),
          sequence(kDefaultSequence),
          sgids(sg),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(const std::vector<std::string> &community)
        : local_pref(kDefaultLocalPref),
          med(0),
          sequence(kDefaultSequence),
          sgids(std::vector<int>()),
          communities(community) {
    }
    RouteAttributes(const LoadBalance::LoadBalanceAttribute &lba)
        : local_pref(kDefaultLocalPref), sequence(kDefaultSequence),
          loadBalanceAttribute(lba) {
    }
    RouteAttributes(const RouteParams &params)
        : local_pref(kDefaultLocalPref),
          med(kDefaultMed),
          sequence(kDefaultSequence),
          params(params) {
    }
    void SetSg(const std::vector<int> &sg) {
        sgids = sg;
    }
    void SetCommunities(const std::vector<std::string> &community) {
        communities = community;
    }
    static int GetDefaultLocalPref() { return kDefaultLocalPref; }
    static int GetDefaultMed() { return kDefaultMed; }
    static int GetDefaultSequence() { return kDefaultSequence; }

    uint32_t local_pref;
    uint32_t med;
    uint32_t sequence;
    std::vector<int> sgids;
    std::vector<std::string> communities;
    LoadBalance::LoadBalanceAttribute loadBalanceAttribute;
    RouteParams params;
};

struct NextHop {
    NextHop() : label_(0) { }
    NextHop(std::string address) :
            address_(address), label_(0) {
        tunnel_encapsulations_.push_back("gre");
    }
    NextHop(std::string address, uint32_t label, std::string tun1 = "gre",
            const std::string virtual_network = "") :
                address_(address), label_(label),
                virtual_network_(virtual_network) {
        if (tun1 == "all") {
            tunnel_encapsulations_.push_back("gre");
            tunnel_encapsulations_.push_back("udp");
            tunnel_encapsulations_.push_back("vxlan");
        } else if (tun1 == "all_ipv6") {
            tunnel_encapsulations_.push_back("gre");
            tunnel_encapsulations_.push_back("udp");
        } else {
            tunnel_encapsulations_.push_back(tun1);
        }
    }

    bool operator==(NextHop other) {
        if (address_ != other.address_) return false;
        if (label_ != other.label_) return false;
        if (tunnel_encapsulations_.size() !=
                other.tunnel_encapsulations_.size()) {
            return false;
        }

        std::vector<std::string>::iterator i;
        for (i = tunnel_encapsulations_.begin();
                i != tunnel_encapsulations_.end(); i++) {
            if (std::find(other.tunnel_encapsulations_.begin(),
                          other.tunnel_encapsulations_.end(), *i) ==
                    other.tunnel_encapsulations_.end()) {
                return false;
            }
        }
        if (virtual_network_ != other.virtual_network_) return false;
        return true;
    }

    std::string address_;
    int label_;
    std::vector<std::string> tunnel_encapsulations_;
    std::string virtual_network_;
};

typedef std::vector<NextHop> NextHops;

class XmppDocumentMock {
public:
    enum Oper {
        ADD,
        CHANGE,
        DELETE,
    };
    static const char *kControlNodeJID;
    static const char *kNetworkServiceJID;
    static const char *kConfigurationServiceJID;
    static const char *kPubSubNS;

    XmppDocumentMock(const std::string &hostname);
    pugi::xml_document *AddEorMarker();
    pugi::xml_document *RouteAddXmlDoc(const std::string &network,
        const std::string &prefix,
        const NextHops &nexthops = NextHops(),
        const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteDeleteXmlDoc(const std::string &network, 
        const std::string &prefix);

    pugi::xml_document *Inet6RouteAddXmlDoc(const std::string &network,
        const std::string &prefix, const NextHops &nexthops,
        const RouteAttributes &attributes);
    pugi::xml_document *Inet6RouteChangeXmlDoc(const std::string &network,
        const std::string &prefix, const NextHops &nexthops,
        const RouteAttributes &attributes);
    pugi::xml_document *Inet6RouteDeleteXmlDoc(const std::string &network,
        const std::string &prefix);
    pugi::xml_document *Inet6RouteAddBogusXmlDoc(const std::string &network,
        const std::string &prefix, NextHops nexthops, TestErrorType error_type);

    pugi::xml_document *RouteEnetAddXmlDoc(const std::string &network,
                                           const std::string &prefix,
                                           const NextHops &nexthops,
                                           const RouteAttributes &attributes);
    pugi::xml_document *RouteEnetDeleteXmlDoc(const std::string &network,
                                              const std::string &prefix,
                                              NextHops nexthops = NextHops());
    pugi::xml_document *RouteMcastAddXmlDoc(const std::string &network, 
                                            const std::string &sg,
                                            const std::string &nexthop,
                                            const std::string &label_range,
                                            const std::string &encap);
    pugi::xml_document *RouteMcastDeleteXmlDoc(const std::string &network, 
                                               const std::string &sg);
    pugi::xml_document *SubscribeXmlDoc(const std::string &network, int id,
                                        std::string type = kNetworkServiceJID);
    pugi::xml_document *UnsubscribeXmlDoc(const std::string &network, int id,
                                        std::string type = kNetworkServiceJID);

    const std::string &hostname() const { return hostname_; }
    const std::string &localaddr() const { return localaddr_; }

    void set_localaddr(const std::string &addr) { localaddr_ = addr; }

private:
    pugi::xml_node PubSubHeader(std::string type);
    pugi::xml_document *SubUnsubXmlDoc(
            const std::string &network, int id, bool sub, std::string type);
    pugi::xml_document *Inet6RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, Oper oper,
            const NextHops &nexthops = NextHops(),
            const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add,
            const NextHops &nexthop = NextHops(),
            const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteEnetAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add,
            const NextHops &nexthops = NextHops(),
            const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteMcastAddDeleteXmlDoc(const std::string &network,
            const std::string &sg, bool add,
            const std::string &nexthop = std::string(),
            const std::string &label_range = std::string(),
            const std::string &encap = std::string());

    std::string hostname_;
    int label_alloc_;
    std::string localaddr_;
    boost::scoped_ptr<pugi::xml_document> xdoc_;
};

class NetworkAgentMock {
private:
    class AgentPeer;

public:
    typedef autogen::ItemType RouteEntry;
    typedef std::map<std::string, RouteEntry *> RouteTable;

    typedef autogen::ItemType Inet6RouteEntry;
    typedef std::map<std::string, Inet6RouteEntry *> Inet6RouteTable;

    typedef autogen::EnetItemType EnetRouteEntry;
    typedef std::map<std::string, EnetRouteEntry *> EnetRouteTable;

    typedef autogen::McastItemType McastRouteEntry;
    typedef std::map<std::string, McastRouteEntry *> McastRouteTable;

    typedef autogen::VirtualRouter VRouterEntry;
    typedef std::map<std::string, VRouterEntry *> VRouterTable;

    typedef autogen::VirtualMachine VMEntry;
    typedef std::map<std::string, VMEntry *> VMTable;

    template <typename T>
    class Instance {
    public:
        typedef std::map<std::string, T *> TableMap;
        Instance();
        virtual ~Instance();
        void Update(long count);
        void Update(const std::string &node, T *entry);
        void Remove(const std::string &node);
        void Clear();
        int Count() const;
        const T *Lookup(const std::string &node) const;
    private:
        size_t count_;
        TableMap table_;
    };

    template <typename T>
    class InstanceMgr {
        public:
            typedef std::map<std::string, Instance<T> *> InstanceMap;

            InstanceMgr(NetworkAgentMock *parent, std::string type) {
                parent_ = parent;
                type_ = type;
            }

            bool HasSubscribed(const std::string &network);
            void Subscribe(const std::string &network, int id = -1,
                           bool wait_for_established = true,
                           bool send_subscribe = true);
            void Unsubscribe(const std::string &network, int id = -1,
                             bool wait_for_established = true,
                             bool send_unsubscribe = true);
            void Update(const std::string &network, long count);
            void Update(const std::string &network,
                        const std::string &node_name, T *rt_entry);
            void Remove(const std::string &network,
                        const std::string &node_name);
            int Count(const std::string &network) const;
            int Count() const;
            void Clear();
            const T *Lookup(const std::string &network,
                            const std::string &prefix,
                            const bool take_lock = true) const;

        private:
            NetworkAgentMock *parent_;
            std::string type_;
            InstanceMap instance_map_;
    };

    NetworkAgentMock(EventManager *evm, const std::string &hostname,
                     int server_port, std::string local_address = "127.0.0.1",
                     std::string server_address = "127.0.0.1",
                     bool xmpp_auth_enabled = false);
    ~NetworkAgentMock();

    bool skip_updates_processing() { return skip_updates_processing_; }
    void set_skip_updates_processing(bool set) {
        skip_updates_processing_ = set;
    }
    void SessionDown();
    void SessionUp();

    void SubscribeAll(const std::string &network, int id = -1,
                      bool wait_for_established = true) {
        route_mgr_->Subscribe(network, id, wait_for_established, true);
        inet6_route_mgr_->Subscribe(network, id, wait_for_established, false);
        enet_route_mgr_->Subscribe(network, id, wait_for_established, false);
        mcast_route_mgr_->Subscribe(network, id, wait_for_established, false);
    }
    void UnsubscribeAll(const std::string &network, int id = -1,
                        bool wait_for_established = true) {
        route_mgr_->Unsubscribe(network, id, wait_for_established, true);
        inet6_route_mgr_->Unsubscribe(network, id, wait_for_established, false);
        enet_route_mgr_->Unsubscribe(network, id, wait_for_established, false);
        mcast_route_mgr_->Unsubscribe(network, id, wait_for_established, false);
    }

    void Subscribe(const std::string &network, int id = -1,
                   bool wait_for_established = true) {
        route_mgr_->Subscribe(network, id, wait_for_established);
    }
    void Unsubscribe(const std::string &network, int id = -1,
                     bool wait_for_established = true) {
        route_mgr_->Unsubscribe(network, id, wait_for_established);
    }

    int RouteCount(const std::string &network) const;
    int RouteCount() const;
    bool HasSubscribed(const std::string &network) const;
    int RouteNextHopCount(const std::string &network,
                          const std::string &prefix);
    const RouteEntry *RouteLookup(const std::string &network,
                                  const std::string &prefix) const {
        return route_mgr_->Lookup(network, prefix);
    }

    void Inet6Subscribe(const std::string &network, int id = -1,
                        bool wait_for_established = true) {
        inet6_route_mgr_->Subscribe(network, id, wait_for_established);
    }
    void Inet6Unsubscribe(const std::string &network, int id = -1,
                          bool wait_for_established = true) {
        inet6_route_mgr_->Unsubscribe(network, id, wait_for_established);
    }
    int Inet6RouteCount(const std::string &network) const;
    int Inet6RouteCount() const;
    const RouteEntry *Inet6RouteLookup(const std::string &network,
                                       const std::string &prefix) const {
        return inet6_route_mgr_->Lookup(network, prefix);
    }

    void SendEorMarker();
    void AddRoute(const std::string &network, const std::string &prefix,
                  const std::string nexthop = "",
                  int local_pref = 0, int med = 0);
    void AddRoute(const std::string &network, const std::string &prefix,
                  const NextHops &nexthops, int local_pref = 0);
    void AddRoute(const std::string &network, const std::string &prefix,
                  const NextHops &nexthops, const RouteAttributes &attributes);
    void DeleteRoute(const std::string &network, const std::string &prefix);

    void AddInet6Route(const std::string &network, const std::string &prefix,
        const NextHops &nexthops = NextHops(),
        const RouteAttributes &attributes = RouteAttributes());
    void AddInet6Route(const std::string &network, const std::string &prefix,
        const std::string &nexthop_str, int local_pref = 100, int med = 0);
    void ChangeInet6Route(const std::string &network, const std::string &prefix,
        const NextHops &nexthops = NextHops(),
        const RouteAttributes &attributes = RouteAttributes());
    void DeleteInet6Route(const std::string &network,
        const std::string &prefix);
    void AddBogusInet6Route(const std::string &network,
        const std::string &prefix, const std::string &nexthop,
        TestErrorType error_type);

    void EnetSubscribe(const std::string &network, int id = -1,
                       bool wait_for_established = true) {
        enet_route_mgr_->Subscribe(network, id, wait_for_established);
    }
    void EnetUnsubscribe(const std::string &network, int id = -1,
                         bool wait_for_established = true) {
        enet_route_mgr_->Unsubscribe(network, id, wait_for_established);
    }

    int EnetRouteCount(const std::string &network) const;
    int EnetRouteCount() const;
    const EnetRouteEntry *EnetRouteLookup(const std::string &network,
                                          const std::string &prefix) const {
        return enet_route_mgr_->Lookup(network, prefix);
    }

    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const std::string nexthop = "",
                      const RouteParams *params = NULL);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const NextHop &nexthop,
                      const RouteParams *params = NULL);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const NextHops &nexthops,
                      const RouteParams *params = NULL);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const NextHop &nexthop,
                      const RouteAttributes &attributes);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const NextHops &nexthops,
                      const RouteAttributes &attributes);
    void DeleteEnetRoute(const std::string &network, const std::string &prefix);

    void McastSubscribe(const std::string &network, int id = -1,
                        bool wait_for_established = true) {
        mcast_route_mgr_->Subscribe(network, id, wait_for_established);
    }
    void McastUnsubscribe(const std::string &network, int id = -1,
                          bool wait_for_established = true) {
        mcast_route_mgr_->Unsubscribe(network, id, wait_for_established);
    }

    int McastRouteCount(const std::string &network) const;
    int McastRouteCount() const;
    const McastRouteEntry *McastRouteLookup(const std::string &network,
                                            const std::string &prefix) const {
        return mcast_route_mgr_->Lookup(network, prefix);
    }

    void AddMcastRoute(const std::string &network, const std::string &sg,
                       const std::string &nexthop,
                       const std::string &label_range,
                       const std::string &encap = "");
    void DeleteMcastRoute(const std::string &network, const std::string &sg);

    bool IsEstablished();
    bool IsSessionEstablished();
    bool IsReady();
    bool IsChannelReady();
    void ClearInstances();

    const std::string &hostname() const { return impl_->hostname(); }
    const std::string &localaddr() const { return impl_->localaddr(); }
    const std::string ToString() const;
    void set_localaddr(const std::string &addr) { impl_->set_localaddr(addr); }
    XmppDocumentMock *GetXmlHandler() { return impl_.get(); }
    void set_id (int id) { id_ = id; }
    const int id() const { return id_; }

    XmppClient *client() { return client_; }
    void Delete();
    tbb::mutex &get_mutex() { return mutex_; }
    bool down() { return down_; }

    const std::string local_address() const { return local_address_; }
    void DisableRead(bool disable_read);

    enum RequestType {
        IS_ESTABLISHED,
        IS_CHANNEL_READY,
    };
    struct Request {
        RequestType type;
        bool result;
    };

    bool ProcessRequest(Request *request);

    size_t get_sm_connect_attempts();
    size_t get_sm_keepalive_count();
    size_t get_connect_error();
    size_t get_session_close();
    uint32_t flap_count();

    boost::scoped_ptr<InstanceMgr<RouteEntry> > route_mgr_;
    boost::scoped_ptr<InstanceMgr<Inet6RouteEntry> > inet6_route_mgr_;
    boost::scoped_ptr<InstanceMgr<EnetRouteEntry> > enet_route_mgr_;
    boost::scoped_ptr<InstanceMgr<McastRouteEntry> > mcast_route_mgr_;
    boost::scoped_ptr<InstanceMgr<VRouterEntry> > vrouter_mgr_;
    boost::scoped_ptr<InstanceMgr<VMEntry> > vm_mgr_;

private:
    static void Initialize();
    AgentPeer *GetAgent();
    XmppChannelConfig *CreateXmppConfig();
    bool ConnectionDestroyed() const;

    XmppClient *client_;
    std::auto_ptr<AgentPeer> peer_;
    boost::scoped_ptr<XmppDocumentMock> impl_;

    WorkQueue<Request *> work_queue_;
    std::string server_address_;
    std::string local_address_;
    int server_port_;
    bool skip_updates_processing_;
    bool down_;
    tbb::mutex mutex_;
    tbb::mutex work_mutex_;

    tbb::interface5::condition_variable cond_var_;

    bool xmpp_auth_enabled_;
    int id_;
};

typedef boost::shared_ptr<NetworkAgentMock> NetworkAgentMockPtr;

}  // namespace test

#endif /* defined(__ctrlplane__network_agent_mock__) */
