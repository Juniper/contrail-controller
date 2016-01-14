/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_entry_hpp
#define vnsw_agent_arp_entry_hpp

struct ArpKey {
    ArpKey(in_addr_t addr, const VrfEntry *ventry) : ip(addr), vrf(ventry) {};
    ArpKey(const ArpKey &key) : ip(key.ip), vrf(key.vrf) {};
    bool operator <(const ArpKey &rhs) const {
        if (vrf != rhs.vrf)
            return vrf < rhs.vrf;
        return (ip < rhs.ip);
    }

    in_addr_t ip;
    const VrfEntry *vrf;
};

// Represents each Arp entry maintained by the ARP module
class ArpEntry {
public:
    enum State {
        INITING = 0x01,
        RESOLVING = 0x02,
        ACTIVE = 0x04,
        RERESOLVING  = 0x08,
    };

    ArpEntry(boost::asio::io_service &io, ArpHandler *handler,
             ArpKey &key, const VrfEntry *vrf, State state,
             const Interface *itf);
    virtual ~ArpEntry();

    const ArpKey &key() const { return key_; }
    State state() const { return state_; }
    const MacAddress &mac_address() const { return mac_address_; }
    const Interface *interface() const { return interface_; }

    bool HandleArpRequest();
    void HandleArpReply(const MacAddress &);
    bool RetryExpiry();
    bool AgingExpiry();
    void SendGratuitousArp();
    bool DeleteArpRoute();
    bool IsResolved();
    void Resync(bool policy, const VnListType &vnlist,
                const SecurityGroupList &sg);

private:
    void StartTimer(uint32_t timeout, uint32_t mtype);
    void SendArpRequest();
    void AddArpRoute(bool resolved);
    void HandleDerivedArpRequest();
    bool IsDerived();

    boost::asio::io_service &io_;
    ArpKey key_;
    const VrfEntry *nh_vrf_;
    MacAddress mac_address_;
    State state_;
    int retry_count_;
    boost::intrusive_ptr<ArpHandler> handler_;
    Timer *arp_timer_;
    const Interface *interface_;
    DISALLOW_COPY_AND_ASSIGN(ArpEntry);
};

#endif // vnsw_agent_arp_entry_hpp
