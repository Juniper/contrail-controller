/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <oper/physical_device.h>

#include <ovsdb_client_tor.h>

using OVSDB::TorTable;

TorTable::TorTable(Agent *agent, TorDeleteCallback cb) : cb_(cb),
    table_(agent->physical_device_table()) {
    id_ = table_->Register(boost::bind(&TorTable::PhyiscalDeviceNotify,
                                       this, _1, _2));
}

TorTable::~TorTable() {
    table_->Unregister(id_);
}

void TorTable::PhyiscalDeviceNotify(DBTablePartBase *part, DBEntryBase *e) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(e);
    TorEntryState *state =
        static_cast<TorEntryState *>(e->GetState(table_, id_));
    Ip4Address old_ip;
    if (dev->IsDeleted()) {
        if (state != NULL) {
            HandleIpDelete(state);
            e->ClearState(table_, id_);
        }
        return;
    }
    if (state == NULL) {
        state = new TorEntryState();
        state->uuid = dev->uuid();
        e->SetState(table_, id_, state);
    } else {
        HandleIpDelete(state);
    }
    // update IP and return
    state->ip = dev->management_ip().to_v4();
    end_point_list_.insert(EndPointKey(state->ip, state->uuid));
}

bool TorTable::isTorAvailable(Ip4Address ip) {
    EndPointList::iterator it =
        end_point_list_.upper_bound(EndPointKey(ip, boost::uuids::nil_uuid()));
    if (it == end_point_list_.end() || it->first != ip) {
        return false;
    }
    return true;
}

void TorTable::HandleIpDelete(TorEntryState *state) {
    end_point_list_.erase(EndPointKey(state->ip, state->uuid));
    EndPointList::iterator it =
        end_point_list_.upper_bound(EndPointKey(state->ip,
                                                boost::uuids::nil_uuid()));
    if (it == end_point_list_.end() || it->first != state->ip) {
        if (cb_ != NULL) {
            (cb_)(state->ip);
        }
    }
}

