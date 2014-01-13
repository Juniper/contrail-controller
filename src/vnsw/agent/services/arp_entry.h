/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_entry_hpp
#define vnsw_agent_arp_entry_hpp

struct ArpKey {
    in_addr_t ip;
    const VrfEntry  *vrf;

    ArpKey(in_addr_t addr, const VrfEntry *ventry) : ip(addr), vrf(ventry) {};
    ArpKey(const ArpKey &key) : ip(key.ip), vrf(key.vrf) {};
    bool operator <(const ArpKey &rhs) const { return (ip < rhs.ip); }
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
             in_addr_t ip, const VrfEntry *vrf,
             State state = ArpEntry::INITING);
    virtual ~ArpEntry();

    const ArpKey &key() const { return key_; }
    State state() const { return state_; }
    const unsigned char *mac_address() const { return mac_address_; }

    bool HandleArpRequest();
    void HandleArpReply(uint8_t *mac);
    void RetryExpiry();
    void AgingExpiry();
    void SendGraciousArp();
    void Delete();
    bool IsResolved();

private:
    void StartTimer(uint32_t timeout, ArpHandler::ArpMsgType mtype);
    void SendArpRequest();
    void UpdateNhDBEntry(DBRequest::DBOperation op, bool resolved = false);

    ArpKey key_;
    unsigned char mac_address_[ETH_ALEN];
    State state_;
    int retry_count_;
    boost::scoped_ptr<ArpHandler> handler_;
    Timer *arp_timer_;
    DISALLOW_COPY_AND_ASSIGN(ArpEntry);
};

#endif // vnsw_agent_arp_entry_hpp
