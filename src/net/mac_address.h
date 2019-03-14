/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_mac_address_h
#define ctrlplane_mac_address_h

#include <string>
#include <boost/system/error_code.hpp>

#include <net/ethernet.h>
#include <sys/socket.h>

class MacAddress {
public:
    MacAddress();
    explicit MacAddress(const uint8_t *data);

    MacAddress(const MacAddress &rhs) {
        addr_ = rhs.addr_;
    }

    explicit MacAddress(const struct ether_addr &a) {
        addr_ = a;
     }
    explicit MacAddress(const struct ether_addr *a) {
        addr_ = *a;
    }

    MacAddress(unsigned int a, unsigned int b, unsigned int c,
               unsigned int d, unsigned int e, unsigned int f);

    explicit MacAddress(const std::string &s,
                        boost::system::error_code *error = NULL);

    bool IsBroadcast() const;
    bool IsMulticast() const;
    bool IsZero() const;
    int CompareTo(const MacAddress &rhs, int len = 0) const;

    bool operator==(const MacAddress *rhs) const {
        return CompareTo(*rhs) == 0;
    }
    bool operator==(const MacAddress &rhs) const {
        return CompareTo(rhs) == 0;
    }
    bool operator<(const MacAddress &rhs) const {
        return CompareTo(rhs) < 0;
    }
    bool operator>(const MacAddress &rhs) const {
        return CompareTo(rhs) > 0;
    }
    bool operator!=(const MacAddress &rhs) const {
        return !operator==(rhs);
    }

    static size_t size() {
        return sizeof(struct ether_addr);
    }

    static size_t bit_len() {
        return (sizeof(struct ether_addr) * 8);
    }

    u_int8_t &operator[](size_t i) {
        return ((u_int8_t *)&addr_)[i];
    }

    u_int8_t operator[](size_t i) const {
        return ((u_int8_t *)&addr_)[i];
    }

    MacAddress &operator=(const u_int8_t *c);

    MacAddress &operator=(const struct sockaddr *sa);

    MacAddress &operator=(const struct sockaddr &sa) {
        return operator=(&sa);
    }

    MacAddress &operator=(const struct ether_addr &ea) {
        addr_ = ea;
        return *this;
    }

    MacAddress &operator=(const MacAddress &rhs) {
        addr_ = rhs.addr_;
        return *this;
    }

    bool ToArray(u_int8_t *p, size_t s) const;

    operator ether_addr() {
        return addr_;
    }

    operator ether_addr&() {
        return addr_;
    }

    operator sockaddr() const {
        struct sockaddr sa = { 0 };
        ToArray((u_int8_t *)sa.sa_data, sizeof(sa));
        return sa;
    }

    operator const ether_addr&() const {
        return addr_;
    }

    operator const u_int8_t *() const {
        return (const u_int8_t *)&addr_;
    }

    operator u_int8_t *() {
        return (u_int8_t *)&addr_;
    }

    operator const int8_t *() const {
        return (const int8_t *)&addr_;
    }

    operator int8_t *() {
        return (int8_t *)&addr_;
    }

    u_int8_t &last_octet() {
        return ((u_int8_t *)&addr_)[5];
    }

    void Zero() {
        addr_ = kZeroMac;
    }

    void Broadcast() {
        addr_ = kBroadcastMac;
    }

    void Multicast() {
        addr_ = kMulticastMac;
    }

    const uint8_t *GetData() const { return (uint8_t *)&addr_; }

    std::string ToString() const;
    static MacAddress FromString(const std::string &str,
        boost::system::error_code *error = NULL);

    static const MacAddress kZeroMac;
    static const MacAddress kBroadcastMac;
    static const MacAddress kMulticastMac;
    static const MacAddress &BroadcastMac() {
        return kBroadcastMac;
    }
    static const MacAddress &MulticastMac() {
        return kMulticastMac;
    }
    static const MacAddress &ZeroMac() {
        return kZeroMac;
    }

private:
    struct ether_addr addr_;
};

#endif
