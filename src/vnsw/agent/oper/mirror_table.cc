/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>

#include <cmn/agent_cmn.h>
#include "oper/route_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

using namespace std;
using namespace boost::asio;

MirrorTable *MirrorTable::mirror_table_;

MirrorTable::~MirrorTable() { 
    boost::system::error_code err;
    if (udp_sock_.get()) {
        udp_sock_->close(err);
    }
}

bool MirrorEntry::IsLess(const DBEntry &rhs) const {
    const MirrorEntry &a = static_cast<const MirrorEntry &>(rhs);
    return (analyzer_name_ < a.GetAnalyzerName());
}

DBEntryBase::KeyPtr MirrorEntry::GetDBRequestKey() const {
    MirrorEntryKey *key = new MirrorEntryKey(analyzer_name_);
    return DBEntryBase::KeyPtr(key);
}

void MirrorEntry::SetKey(const DBRequestKey *k) {
    const MirrorEntryKey *key = static_cast<const MirrorEntryKey *>(k);
    analyzer_name_ = key->analyzer_name_;
}

std::auto_ptr<DBEntry> MirrorTable::AllocEntry(const DBRequestKey *k) const {
    const MirrorEntryKey *key = static_cast<const MirrorEntryKey *>(k);
    MirrorEntry *mirror_entry = new MirrorEntry(key->analyzer_name_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(mirror_entry));
}

DBEntry *MirrorTable::Add(const DBRequest *req) {
    const MirrorEntryKey *key = static_cast<const MirrorEntryKey *>(req->key.get());
    MirrorEntry *mirror_entry = new MirrorEntry(key->analyzer_name_);
    //Get Mirror NH
    OnChange(mirror_entry, req);
    LOG(DEBUG, "Mirror Add");
    return mirror_entry;
}

bool MirrorTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false; 
    MirrorEntry *mirror_entry = static_cast<MirrorEntry *>(entry);
    MirrorEntryData *data = static_cast<MirrorEntryData *>(req->data.get());

    MirrorNHKey nh_key(data->vrf_name_, data->sip_, data->sport_, 
                       data->dip_, data->dport_);
    NextHop *nh = static_cast<NextHop *>
                  (Agent::GetInstance()->nexthop_table()->FindActiveEntry(&nh_key));
    assert(nh);

    if (mirror_entry->nh_ != nh) {
        mirror_entry->nh_ = nh;
        mirror_entry->sip_ = data->sip_;
        mirror_entry->sport_ = data->sport_;
        mirror_entry->dip_ = data->dip_;
        mirror_entry->dport_ = data->dport_;
        mirror_entry->vrf_ = Agent::GetInstance()->vrf_table()->FindVrfFromName(data->vrf_name_);
        ret = true;
    }
    return ret;
}

void MirrorTable::AddMirrorEntry(const std::string &analyzer_name,
                                 const std::string &vrf_name,
                                 const Ip4Address &sip, uint16_t sport, 
                                 const Ip4Address &dip, uint16_t dport) {

    DBRequest req;
    // First enqueue request to add Mirror NH
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MirrorNHKey *nh_key = new MirrorNHKey(vrf_name, sip, sport, dip, dport);
    req.key.reset(nh_key);
    req.data.reset(NULL);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    MirrorEntryKey *key = new MirrorEntryKey(analyzer_name);
    MirrorEntryData *data = new MirrorEntryData(vrf_name, sip, 
                                                sport, dip, dport);
    req.key.reset(key);
    req.data.reset(data);
    mirror_table_->Enqueue(&req);
}

void MirrorTable::DelMirrorEntry(const std::string &analyzer_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    MirrorEntryKey *key = new MirrorEntryKey(analyzer_name);
    req.key.reset(key);
    req.data.reset(NULL);
    mirror_table_->Enqueue(&req);
}

void MirrorTable::OnZeroRefcount(AgentDBEntry *e) {
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>(e);
    DelMirrorEntry(mirr_entry->GetAnalyzerName());
}

DBTableBase *MirrorTable::CreateTable(DB *db, const std::string &name) {
    mirror_table_ = new MirrorTable(db, name);
    mirror_table_->Init();
    return mirror_table_;
};

void MirrorTable::ReadHandler(const boost::system::error_code &ec,
                              size_t bytes_transferred) {

    if (ec) {
        LOG(ERROR, "Error reading from Mirroor sock. Error : " << 
            boost::system::system_error(ec).what());
        return;
    }

    udp_sock_->async_receive(boost::asio::buffer(rx_buff_, sizeof(rx_buff_)), 
                           boost::bind(&MirrorTable::ReadHandler, this, 
                                       placeholders::error,
                                       placeholders::bytes_transferred));
}

void MirrorTable::MirrorSockInit(void) {
    EventManager *event_mgr;

    event_mgr = Agent::GetInstance()->event_manager();
    boost::asio::io_service &io = *event_mgr->io_service();
    ip::udp::endpoint ep(ip::udp::v4(), 0);

    udp_sock_.reset(new ip::udp::socket(io));

    boost::system::error_code ec;
    udp_sock_->open(ip::udp::v4(), ec);
    assert(ec.value() == 0);

    udp_sock_->bind(ep, ec);
    assert(ec.value() == 0);

    ip::udp::endpoint sock_ep = udp_sock_->local_endpoint(ec);
    assert(ec.value() == 0);
    Agent::GetInstance()->set_mirror_port(sock_ep.port());

    udp_sock_->async_receive(boost::asio::buffer(rx_buff_, sizeof(rx_buff_)), 
                             boost::bind(&MirrorTable::ReadHandler, this, 
                                         placeholders::error,
                                         placeholders::bytes_transferred));
}

void MirrorEntry::set_mirror_entrySandeshData(MirrorEntrySandeshData &data) const {
    data.set_analyzer_name(GetAnalyzerName());
    data.set_sip(GetSip()->to_string());
    data.set_dip(GetDip()->to_string());
    data.set_vrf(GetVrf() ? GetVrf()->GetName() : "");
    data.set_sport(GetSPort());
    data.set_dport(GetDPort());
    data.set_ref_count(GetRefCount());
    nh_->SetNHSandeshData(data.nh);
}

bool MirrorEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    MirrorEntryResp *resp = static_cast<MirrorEntryResp *>(sresp);

    MirrorEntrySandeshData data;
    set_mirror_entrySandeshData(data);
    std::vector<MirrorEntrySandeshData> &list =  
        const_cast<std::vector<MirrorEntrySandeshData>&>
        (resp->get_mirror_entry_list());
    list.push_back(data);

    return true;
}

void MirrorEntryReq::HandleRequest() const {
    AgentMirrorSandesh *sand = new AgentMirrorSandesh(context());
    sand->DoSandesh();
}
