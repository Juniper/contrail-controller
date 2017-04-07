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
    static const int kDefaultSticky = false;
    static const int kDefaultETreeLeaf = false;

    struct MobilityInfo {
        MobilityInfo() : seqno (kDefaultSequence), sticky(kDefaultSticky) {
        }

        MobilityInfo(uint32_t seq, bool sbit) : seqno (seq), sticky(sbit) {
        }
        uint32_t seqno;
        bool sticky;
    };

    RouteAttributes()
        : local_pref(kDefaultLocalPref),
          med(kDefaultMed),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t seq, const std::vector<int> &sg)
        : local_pref(lpref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(seq, kDefaultSticky),
          sgids(sg),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t seq)
        : local_pref(lpref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(seq, kDefaultSticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t med, uint32_t seq)
        : local_pref(lpref),
          med(med),
          etree_leaf(kDefaultETreeLeaf),
          mobility(seq, kDefaultSticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t med, uint32_t seq, bool sticky)
        : local_pref(lpref),
          med(kDefaultMed),
          etree_leaf(kDefaultETreeLeaf),
          mobility(seq, sticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, uint32_t med, uint32_t seq, bool sticky, bool leaf)
        : local_pref(lpref),
          med(kDefaultMed),
          etree_leaf(leaf),
          mobility(seq, sticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref)
        : local_pref(lpref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          sgids(std::vector<int>()),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(uint32_t lpref, const std::vector<int> &sg)
        : local_pref(lpref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          sgids(sg),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(const std::vector<int> &sg)
        : local_pref(kDefaultLocalPref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          sgids(sg),
          communities(std::vector<std::string>()) {
    }
    RouteAttributes(const std::vector<std::string> &community)
        : local_pref(kDefaultLocalPref),
          med(0),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          sgids(std::vector<int>()),
          communities(community) {
    }
    RouteAttributes(const LoadBalance::LoadBalanceAttribute &lba)
        : local_pref(kDefaultLocalPref), med(0), etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
          loadBalanceAttribute(lba) {
    }

    RouteAttributes(const RouteParams &params)
        : local_pref(kDefaultLocalPref),
          med(kDefaultMed),
          etree_leaf(kDefaultETreeLeaf),
          mobility(kDefaultSequence, kDefaultSticky),
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
    bool etree_leaf;
    MobilityInfo mobility;
    std::vector<int> sgids;
    std::vector<std::string> communities;
    LoadBalance::LoadBalanceAttribute loadBalanceAttribute;
    RouteParams params;
};

struct NextHop {
    NextHop() : no_label_(false), label_(0), l3_label_(0) { }
    NextHop(std::string address) :
            address_(address), no_label_(false), label_(0), l3_label_(0) {
        tunnel_encapsulations_.push_back("gre");
    }
    NextHop(bool no_label, std::string address) :
            address_(address), no_label_(no_label), label_(0), l3_label_(0) {
    }
    NextHop(std::string address, uint32_t label, std::string tunnel,
            const std::string virtual_network = "") :
            address_(address), no_label_(false), label_(label), l3_label_(0),
            virtual_network_(virtual_network) {
        if (tunnel.empty()) {
            tunnel_encapsulations_.push_back("gre");
        } else if (tunnel == "all") {
            tunnel_encapsulations_.push_back("gre");
            tunnel_encapsulations_.push_back("udp");
        } else if (tunnel == "all_ipv6") {
            tunnel_encapsulations_.push_back("gre");
            tunnel_encapsulations_.push_back("udp");
        } else {
            tunnel_encapsulations_.push_back(tunnel);
        }
    }
    NextHop(std::string address, std::string mac, uint32_t label,
        uint32_t l3_label, const std::string virtual_network = "") :
                address_(address), mac_(mac), no_label_(false), label_(label),
                l3_label_(l3_label), virtual_network_(virtual_network) {
        tunnel_encapsulations_.push_back("vxlan");
    }

    bool operator==(NextHop other) {
        if (address_ != other.address_) return false;
        if (mac_ != other.mac_) return false;
        if (no_label_ != other.no_label_) return false;
        if (label_ != other.label_) return false;
        if (l3_label_ != other.l3_label_) return false;
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
    std::string mac_;
    bool no_label_;
    int label_;
    int l3_label_;
    std::vector<std::string> tunnel_encapsulations_;
    std::string virtual_network_;
};

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
        const NextHop &nh = NextHop(),
        const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteDeleteXmlDoc(const std::string &network, 
        const std::string &prefix);

    pugi::xml_document *Inet6RouteAddXmlDoc(const std::string &network,
        const std::string &prefix, const NextHop &nh,
        const RouteAttributes &attributes);
    pugi::xml_document *Inet6RouteChangeXmlDoc(const std::string &network,
        const std::string &prefix, const NextHop &nh,
        const RouteAttributes &attributes);
    pugi::xml_document *Inet6RouteDeleteXmlDoc(const std::string &network,
        const std::string &prefix);
    pugi::xml_document *Inet6RouteAddBogusXmlDoc(const std::string &network,
        const std::string &prefix, NextHop nh, TestErrorType error_type);

    pugi::xml_document *RouteEnetAddXmlDoc(const std::string &network,
                                           const std::string &prefix,
                                           const NextHop &nh,
                                           const RouteAttributes &attributes);
    pugi::xml_document *RouteEnetDeleteXmlDoc(const std::string &network,
                                              const std::string &prefix);
    pugi::xml_document *RouteMcastAddXmlDoc(const std::string &network, 
                                            const std::string &sg,
                                            const std::string &nexthop,
                                            const std::string &label_range,
                                            const std::string &encap);
    pugi::xml_document *RouteMcastDeleteXmlDoc(const std::string &network, 
                                               const std::string &sg);
    pugi::xml_document *SubscribeXmlDoc(const std::string &network, int id,
                                        bool no_ribout = false,
                                        std::string type = kNetworkServiceJID);
    pugi::xml_document *UnsubscribeXmlDoc(const std::string &network, int id,
                                        std::string type = kNetworkServiceJID);

    const std::string &hostname() const { return hostname_; }
    const std::string &localaddr() const { return localaddr_; }

    void set_localaddr(const std::string &addr) { localaddr_ = addr; }

private:
    pugi::xml_node PubSubHeader(std::string type);
    pugi::xml_document *SubUnsubXmlDoc(
            const std::string &network, int id, bool no_ribout, bool sub,
            std::string type);
    pugi::xml_document *Inet6RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, Oper oper, const NextHop &nh = NextHop(),
            const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add,
            const NextHop &nh = NextHop(),
            const RouteAttributes &attributes = RouteAttributes());
    pugi::xml_document *RouteEnetAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add,
            const NextHop &nh = NextHop(),
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
        typedef std::set<std::string> OriginatedSet;
        Instance();
        virtual ~Instance();
        void Update(long count);
        void AddOriginated(const std::string &prefix);
        void DeleteOriginated(const std::string &prefix);
        void Update(const std::string &node, T *entry);
        void Remove(const std::string &node);
        void Clear();
        int Count() const;
        const T *Lookup(const std::string &node) const;
        const TableMap &table() const { return table_; }
        const OriginatedSet &originated() const { return originated_; }
    private:
        size_t count_;
        TableMap table_;
        OriginatedSet originated_;
    };

    template <typename T>
    class InstanceMgr {
        public:
            typedef std::map<std::string, Instance<T> *> InstanceMap;

            InstanceMgr(NetworkAgentMock *parent, std::string type) {
                parent_ = parent;
                type_ = type;
                ipv6_ = false;
            }

            void set_ipv6 (bool ipv6) { ipv6_ = ipv6; }
            bool ipv6 () const { return ipv6_; }
            bool HasSubscribed(const std::string &network);
            void Subscribe(const std::string &network, int id = -1,
                           bool no_ribout = false,
                           bool wait_for_established = true,
                           bool send_subscribe = true);
            void Unsubscribe(const std::string &network, int id = -1,
                             bool wait_for_established = true,
                             bool send_unsubscribe = true,
                             bool withdraw_routes = true);
            void Update(const std::string &network, long count);
            void AddOriginated(const std::string &network,
                               const std::string &prefix);
            void DeleteOriginated(const std::string &network,
                                  const std::string &prefix);
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
            bool ipv6_;
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
                      bool no_ribout = false,
                      bool wait_for_established = true) {
        route_mgr_->Subscribe(network, id, no_ribout,
                              wait_for_established, true);
        inet6_route_mgr_->Subscribe(network, id, no_ribout,
                                    wait_for_established, false);
        enet_route_mgr_->Subscribe(network, id, no_ribout,
                                   wait_for_established, false);
        mcast_route_mgr_->Subscribe(network, id, no_ribout,
                                    wait_for_established, false);
    }
    void UnsubscribeAll(const std::string &network, int id = -1,
                        bool wait_for_established = true,
                        bool withdraw_routes = true,
                        bool send_unsubscribe = true) {
        inet6_route_mgr_->Unsubscribe(network, id, wait_for_established,
                                      false, withdraw_routes);
        enet_route_mgr_->Unsubscribe(network, id, wait_for_established,
                                     false, withdraw_routes);
        mcast_route_mgr_->Unsubscribe(network, id, wait_for_established,
                                      false, withdraw_routes);
        route_mgr_->Unsubscribe(network, id, wait_for_established,
                                send_unsubscribe, withdraw_routes);
    }

    void Subscribe(const std::string &network, int id = -1,
                   bool wait_for_established = true, bool no_ribout = false) {
        route_mgr_->Subscribe(network, id, no_ribout, wait_for_established);
    }
    void Unsubscribe(const std::string &network, int id = -1,
                     bool wait_for_established = true,
                     bool withdraw_routes = true,
                     bool send_unsubscribe = true) {
        route_mgr_->Unsubscribe(network, id, wait_for_established,
                                send_unsubscribe, withdraw_routes);
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
                        bool wait_for_established = true,
                        bool no_ribout = false) {
        inet6_route_mgr_->Subscribe(network, id, no_ribout, wait_for_established);
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
    void AddRoute(const string &network_name, const string &prefix,
                  const string &nexthop, const RouteAttributes &attributes);
    void AddRoute(const std::string &network, const std::string &prefix,
                  const NextHop &nexthop,
                  const RouteAttributes &attributes = RouteAttributes());
    void DeleteRoute(const std::string &network, const std::string &prefix);

    void AddInet6Route(const std::string &network, const std::string &prefix,
        const NextHop &nh = NextHop(),
        const RouteAttributes &attributes = RouteAttributes());
    void AddInet6Route(const std::string &network, const std::string &prefix,
        const std::string &nexthop_str, int local_pref = 100, int med = 0);
    void AddInet6Route(const std::string &network, const std::string &prefix,
        const std::string &nexthop, const RouteAttributes &attributes);
    void ChangeInet6Route(const std::string &network, const std::string &prefix,
        const NextHop &nh = NextHop(),
        const RouteAttributes &attributes = RouteAttributes());
    void DeleteInet6Route(const std::string &network,
        const std::string &prefix);
    void AddBogusInet6Route(const std::string &network,
        const std::string &prefix, const std::string &nexthop,
        TestErrorType error_type);

    void EnetSubscribe(const std::string &network, int id = -1,
                       bool wait_for_established = true) {
        enet_route_mgr_->Subscribe(network, id, false, wait_for_established);
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
                      const NextHop &nh, const RouteParams *params = NULL);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const NextHop &nh, const RouteAttributes &attributes);
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      const std::string &nexthop,
                      const RouteAttributes &attributes);
    void DeleteEnetRoute(const std::string &network, const std::string &prefix);

    void McastSubscribe(const std::string &network, int id = -1,
                        bool wait_for_established = true) {
        mcast_route_mgr_->Subscribe(network, id, false, wait_for_established);
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
