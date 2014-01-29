/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_IFMAP_CLIENT_H__
#define __DB_IFMAP_CLIENT_H__

#include <inttypes.h>
#include <map>
#include <string>
#include <vector>

class IFMapExporter;

// An XmppPeer that receives IFMap updates from the server component
class IFMapClient {
public:
    typedef std::map<std::string, std::string> VmMap;
    static const int kIndexInvalid = -1;

    IFMapClient();
    virtual ~IFMapClient();

    virtual const std::string &identifier() const = 0;
    virtual bool SendUpdate(const std::string &msg) = 0;
    virtual const std::string &name() const { return name_; }
    virtual void SetName(const std::string &name) { name_ = name; }

    int index() const { return index_; }
    uint64_t msgs_sent() const { return msgs_sent_; }
    uint64_t msgs_blocked() const { return msgs_blocked_; }
    bool send_is_blocked() const { return send_is_blocked_; }

    void incr_msgs_sent() { ++msgs_sent_; }
    void incr_msgs_blocked() { ++msgs_blocked_; }
    void set_send_is_blocked(bool is_blocked) { send_is_blocked_ = is_blocked; }

    void Initialize(IFMapExporter *exporter, int index);

    // Called when the switch register for a specific network.
    void OnRegister(const std::string &vnet);

    // Called when the switch unregisters from a virtual network.
    void OnUnregister(const std::string &vnet);

    void AddVm(const std::string &vm_uuid) {
        vm_map_.insert(make_pair(vm_uuid, vm_uuid));
    }
    void DeleteVm(const std::string &vm_uuid) {
        vm_map_.erase(vm_uuid);
    }
    bool HasAddedVm(const std::string &vm_uuid) {
        return ((vm_map_.count(vm_uuid) == 1) ? true : false);
    }
    bool HasVms() {
        return (vm_map_.size() != 0);
    }
    // return vm_map_ as a list of strings
    std::vector<std::string> vm_list() const; 

private:
    int index_;
    IFMapExporter *exporter_;
    uint64_t msgs_sent_;
    uint64_t msgs_blocked_;
    bool send_is_blocked_;
    VmMap vm_map_;
    std::string name_;
};

#endif
