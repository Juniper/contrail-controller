/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_mac_address_h
#define ctrlplane_mac_address_h

#include <sys/types.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <boost/system/error_code.hpp>
#include <cstring>

class MacAddress {
public:
    MacAddress();
    explicit MacAddress(const uint8_t *data);
    explicit MacAddress(const struct ether_addr &a) : valid_(true) {
        addr_ = a;
     }
    explicit MacAddress(const struct ether_addr *a) : valid_(true) {
        addr_ = *a;
    }

    MacAddress(uint a, uint b, uint c, uint d, uint e, uint f);

    explicit MacAddress(const std::string &s);

    MacAddress &Broadcast() {
        addr_ = kBroadcastMac;
        valid_ = true;
        return *this;
    }

    static MacAddress FromString(const std::string &str);
    static MacAddress FromString(const std::string &str,
        boost::system::error_code *error);

    int CompareTo(const MacAddress &rhs) const;
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
        return sizeof(addr_);
    }

    u_int8_t &operator[](size_t i) {
        return ((u_int8_t *)&addr_)[i];
    }

    u_int8_t operator[](size_t i) const {
        return ((u_int8_t *)&addr_)[i];
    }

    MacAddress &operator=(const u_int8_t *c) {
        memcpy(&addr_, c, size());
        return *this;
    }

    MacAddress &operator=(const struct sockaddr *sa) {
        memcpy(&addr_, sa->sa_data, size());
        return *this;
    }

    MacAddress &operator=(const struct sockaddr &sa) {
        return operator=(&sa);
    }

    bool ToArray(u_int8_t *p, size_t s) const {
        if (s < size())
            return false;

        memcpy(p, &addr_, size());

        return true;
    }

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
        return (*this)[5];
    }

    bool IsValid() const {
        return valid_;
    }

    MacAddress &operator=(const struct ether_addr &e) {
        addr_ = e;
        valid_ = true;
        return *this;
    }

    void Zero() {
        addr_ = kZeroMac;
    }

    std::string ToString() const;
    const uint8_t *GetData() const { return (uint8_t *)&addr_; }

    static const ether_addr kZeroMac;
    static const ether_addr kBroadcastMac;
    static const MacAddress BroadcastMac() {
        MacAddress t;
        return t.Broadcast();
    }
private:
    struct ether_addr addr_;
    bool valid_;
};

#endif
