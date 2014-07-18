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

struct NextHop {
    NextHop() : label_(0) { }
    NextHop(std::string address, uint32_t label, std::string tun1 = "gre") :
            address_(address), label_(label) {
        if (tun1 == "all") {
            tunnel_encapsulations_.push_back("gre");
            tunnel_encapsulations_.push_back("udp");
            tunnel_encapsulations_.push_back("vxlan");
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
        return true;
    }

    std::string address_;
    int label_;
    std::vector<std::string> tunnel_encapsulations_;
};

typedef std::vector<NextHop> NextHops;

class XmppDocumentMock {
public:
    static const char *kControlNodeJID;
    static const char *kNetworkServiceJID;
    static const char *kConfigurationServiceJID;
    static const char *kPubSubNS;

    XmppDocumentMock(const std::string &hostname);
    pugi::xml_document *RouteAddXmlDoc(const std::string &network, 
                                       const std::string &prefix,
                                       NextHops nexthops = NextHops(),
                                       int local_pref = 0);
    pugi::xml_document *RouteDeleteXmlDoc(const std::string &network, 
                                          const std::string &prefix,
                                          NextHops nexthops = NextHops());
    pugi::xml_document *RouteEnetAddXmlDoc(const std::string &network,
                                           const std::string &prefix,
                                           NextHops nexthops = NextHops());
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
    pugi::xml_document *RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add, const std::string nexthop,
            int local_pref = 0);
    pugi::xml_document *RouteAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, bool add, NextHops nexthop,
            int local_pref = 0);
    pugi::xml_document *RouteEnetAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, const std::string nexthop, bool add);
    pugi::xml_document *RouteEnetAddDeleteXmlDoc(const std::string &network,
            const std::string &prefix, NextHops nexthop, bool add);
    pugi::xml_document *RouteMcastAddDeleteXmlDoc(const std::string &network,
            const std::string &sg, const std::string &nexthop,
            const std::string &label_range, const std::string &encap, bool add);

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
                           bool wait_for_established = true);
            void Unsubscribe(const std::string &network, int id = -1,
                             bool wait_for_established = true);
            void Update(const std::string &network, long count);
            void Update(const std::string &network,
                        const std::string &node_name, T *rt_entry);
            void Remove(const std::string &network,
                        const std::string &node_name);
            int Count(const std::string &network) const;
            int Count() const;
            void Clear();
            const T *Lookup(const std::string &network,
                    const std::string &prefix) const;

        private:
            NetworkAgentMock *parent_;
            std::string type_;
            InstanceMap instance_map_;
    };

    NetworkAgentMock(EventManager *evm, const std::string &hostname,
                     int server_port, std::string local_address = "127.0.0.1",
                     std::string server_address = "127.0.0.1");
    ~NetworkAgentMock();

    bool skip_updates_processing() { return skip_updates_processing_; }
    void set_skip_updates_processing(bool set) {
        skip_updates_processing_ = set;
    }
    void SessionDown();
    void SessionUp();

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
    const RouteEntry *RouteLookup(const std::string &network,
                                  const std::string &prefix) const {
        return route_mgr_->Lookup(network, prefix);
    }

    void AddRoute(const std::string &network, const std::string &prefix,
                  const std::string nexthop = "", int local_pref = 0);
    void DeleteRoute(const std::string &network, const std::string &prefix,
                     const std::string nexthop = "");

    void AddRoute(const std::string &network, const std::string &prefix,
                  NextHops nexthops, int local_pref = 0);
    void DeleteRoute(const std::string &network, const std::string &prefix,
                  NextHops nexthops);

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
                      const std::string nexthop = "");
    void DeleteEnetRoute(const std::string &network, const std::string &prefix,
                         const std::string nexthop = "");
    void AddEnetRoute(const std::string &network, const std::string &prefix,
                      NextHops nexthops);
    void DeleteEnetRoute(const std::string &network, const std::string &prefix,
                         NextHops nexthops);

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
    void ClearInstances();

    const std::string &hostname() const { return impl_->hostname(); }
    const std::string &localaddr() const { return impl_->localaddr(); }
    const std::string ToString() const;
    void set_localaddr(const std::string &addr) { impl_->set_localaddr(addr); }
    XmppDocumentMock *GetXmlHandler() { return impl_.get(); }

    XmppClient *client() { return client_; }
    void Delete();
    tbb::mutex &get_mutex() { return mutex_; }
    bool down() { return down_; }

    const std::string local_address() const { return local_address_; }
    void DisableRead(bool disable_read);

    enum RequestType {
        IS_ESTABLISHED,
    };
    struct Request {
        RequestType type;
        bool result;
    };

    bool ProcessRequest(Request *request);

    boost::scoped_ptr<InstanceMgr<RouteEntry> > route_mgr_;
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
};

typedef boost::shared_ptr<NetworkAgentMock> NetworkAgentMockPtr;

}  // namespace test

#endif /* defined(__ctrlplane__network_agent_mock__) */
