/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/community.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>

#include "base/string_util.h"
#include "bgp/bgp_proto.h"
#include "bgp/extended-community/tag.h"
#include "bgp/extended-community/default_gateway.h"
#include "bgp/extended-community/es_import.h"
#include "bgp/extended-community/esi_label.h"
#include "bgp/extended-community/etree.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/router_mac.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/extended-community/sub_cluster.h"
#include "bgp/extended-community/tag.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "net/community_type.h"

using std::sort;
using std::string;
using std::unique;
using std::vector;

void CommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_community(this);
}

string CommunitySpec::ToString() const {
    string repr;
    char start[32];
    snprintf(start, sizeof(start), "Communities: %zu [", communities.size());
    repr += start;

    for (size_t i = 0; i < communities.size(); ++i) {
        char community[12];
        snprintf(community, sizeof(community), " %X", communities[i]);
        repr += community;
    }
    repr += " ]";

    return repr;
}

Community::Community(CommunityDB *comm_db, const CommunitySpec spec)
    : comm_db_(comm_db), communities_(spec.communities) {
    refcount_ = 0;
    sort(communities_.begin(), communities_.end());
    vector<uint32_t>::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

int Community::CompareTo(const Community &rhs) const {
    KEY_COMPARE(communities_, rhs.communities_);
    return 0;
}

size_t CommunitySpec::EncodeLength() const {
    return communities.size() * sizeof(uint32_t);
}

void Community::Append(uint32_t value) {
    if (ContainsValue(value))
        return;
    communities_.push_back(value);
    sort(communities_.begin(), communities_.end());
}

void Community::Append(const std::vector<uint32_t> &communities) {
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.push_back(community);
    }
    sort(communities_.begin(), communities_.end());
    vector<uint32_t>::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

void Community::Set(const std::vector<uint32_t> &communities) {
    communities_.clear();
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.push_back(community);
    }
}

void Community::Remove(const std::vector<uint32_t> &communities) {
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.erase(
               std::remove(communities_.begin(), communities_.end(), community),
               communities_.end());
    }
}
void Community::Remove() {
    comm_db_->Delete(this);
}

bool Community::ContainsValue(uint32_t value) const {
    BOOST_FOREACH(uint32_t community, communities_) {
        if (community == value)
            return true;
    }
    return false;
}

void Community::BuildStringList(vector<string> *list) const {
    BOOST_FOREACH(uint32_t community, communities_) {
        string name = CommunityType::CommunityToString(community);
        list->push_back(name);
    }
}

CommunityDB::CommunityDB(BgpServer *server) {
}

CommunityPtr CommunityDB::AppendAndLocate(const Community *src,
    uint32_t value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Append(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::AppendAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Append(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::SetAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Set(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::RemoveAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Remove(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::RemoveAndLocate(const Community *src,
                                          uint32_t value) {
    Community::CommunityList communities;
    communities.push_back(value);
    return RemoveAndLocate(src, communities);
}

string ExtCommunitySpec::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "ExtCommunity <code: %d, flags: %02x>:%d",
             code, flags, (uint32_t)communities.size());
    return string(repr);
}

size_t ExtCommunitySpec::EncodeLength() const {
    return communities.size() * sizeof(uint64_t);
}

int ExtCommunitySpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(communities,
        static_cast<const ExtCommunitySpec &>(rhs_attr).communities);
    return 0;
}

void ExtCommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_ext_community(this);
}

void ExtCommunitySpec::AddTunnelEncaps(vector<string> encaps) {
    for (vector<string>::size_type i = 0; i < encaps.size(); i++) {
        string encap_str = encaps[i];
        TunnelEncap tun_encap(encap_str);
        communities.push_back(tun_encap.GetExtCommunityValue());
    }
}

int ExtCommunity::CompareTo(const ExtCommunity &rhs) const {
    KEY_COMPARE(communities_.size(), rhs.communities_.size());

    ExtCommunityList::const_iterator i, j;
    for (i = communities_.begin(), j = rhs.communities_.begin();
         i < communities_.end(); ++i, ++j) {
        if (*i < *j) {
            return -1;
        }
        if (*i > *j) {
            return 1;
        }
    }
    return 0;
}

void ExtCommunity::Remove(const ExtCommunityList &list) {
    for (ExtCommunityList::const_iterator it = list.begin();
         it != list.end(); ++it) {
        communities_.erase(std::remove(communities_.begin(),
                    communities_.end(), *it), communities_.end());
    }
}
void ExtCommunity::Remove() {
    extcomm_db_->Delete(this);
}

void ExtCommunity::Set(const ExtCommunityList &list) {
    communities_.clear();
    for (ExtCommunityList::const_iterator it = list.begin();
         it != list.end(); ++it) {
        communities_.push_back(*it);
    }
}

void ExtCommunity::Append(const ExtCommunityList &list) {
    communities_.insert(communities_.end(), list.begin(), list.end());
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

void ExtCommunity::Append(const ExtCommunityValue &value) {
    communities_.push_back(value);
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

ExtCommunity::ExtCommunityValue ExtCommunity::FromHexString(
        const string &comm, boost::system::error_code *errorp) {
    ExtCommunityValue data;
    char *end;
    uint64_t value = strtoull(comm.c_str(), &end, 16);
    if (value == 0 || *end) {
        // e.g. 0 or 12x34ff (invalid hex)
        if (errorp != NULL) {
            *errorp = make_error_code(
                    boost::system::errc::invalid_argument);
            return data;
        }
    }
    if (comm[0] == '0' && (comm[1] == 'x' || comm[1] == 'X')) {
        if (comm.length() > 18 && errorp != NULL) {
            // e.g. 0xabcdef0123456789f is an invalid 8byte hex value
            *errorp = make_error_code(
                    boost::system::errc::invalid_argument);
            return data;
        }
    } else {
        if (comm.length() > 16 && errorp != NULL) {
            // e.g. abcdef0123456789f is an invalid 8byte hex value
            *errorp = make_error_code(
                    boost::system::errc::invalid_argument);
            return data;
        }
    }
    put_value(&data[0], 8, value);
    return data;
}

ExtCommunity::ExtCommunityList ExtCommunity::ExtCommunityFromString(
        const string &comm) {
    ExtCommunityList commList;
    ExtCommunityValue value;
    size_t pos = comm.find(':');
    string first(comm.substr(0, pos));
    boost::system::error_code error;
    if (first == "soo") {
        SiteOfOrigin soo = SiteOfOrigin::FromString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(soo.GetExtCommunity());
    } else if (first == "target") {
        RouteTarget rt = RouteTarget::FromString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(rt.GetExtCommunity());
    } else if (first == "source-as") {
        SourceAs sas = SourceAs::FromString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(sas.GetExtCommunity());
    } else if (first == "rt-import") {
        VrfRouteImport vit = VrfRouteImport::FromString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(vit.GetExtCommunity());
    } else if (first == "subcluster") {
        SubCluster sc = SubCluster::FromString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(sc.GetExtCommunity());
    } else {
        value = FromHexString(comm, &error);
        if (error) {
            return commList;
        }
        commList.push_back(value);
    }
    return commList;
}

string ExtCommunity::ToHexString(const ExtCommunityValue &comm) {
    char temp[50];
    int len = 0;
    for (size_t i = 0; i < comm.size(); i++) {
        len += snprintf(temp+len, sizeof(temp) - len, "%02x", (comm)[i]);
    }
    return(string(temp));
}

string ExtCommunity::ToString(const ExtCommunityValue &comm) {
    if (is_route_target(comm)) {
        RouteTarget rt(comm);
        return(rt.ToString());
    } else if (is_default_gateway(comm)) {
        DefaultGateway dgw(comm);
        return(dgw.ToString());
    } else if (is_es_import(comm)) {
        EsImport es_import(comm);
        return(es_import.ToString());
    } else if (is_esi_label(comm)) {
        EsiLabel esi_label(comm);
        return(esi_label.ToString());
    } else if (is_mac_mobility(comm)) {
        MacMobility mm(comm);
        return(mm.ToString());
    } else if (is_etree(comm)) {
        ETree etree(comm);
        return(etree.ToString());
    } else if (is_router_mac(comm)) {
        RouterMac router_mac(comm);
        return(router_mac.ToString());
    } else if (is_origin_vn(comm)) {
        OriginVn origin_vn(comm);
        return(origin_vn.ToString());
    } else if (is_security_group(comm)) {
        SecurityGroup sg(comm);
        return(sg.ToString());
    } else if (is_site_of_origin(comm)) {
        SiteOfOrigin soo(comm);
        return(soo.ToString());
    } else if (is_tunnel_encap(comm)) {
        TunnelEncap encap(comm);
        return(encap.ToString());
    } else if (is_load_balance(comm)) {
        LoadBalance load_balance(comm);
        return(load_balance.ToString());
    } else if (is_tag(comm)) {
        Tag tag(comm);
        return(tag.ToString());
    } else if (is_source_as(comm)) {
        SourceAs sas(comm);
        return(sas.ToString());
    } else if (is_vrf_route_import(comm)) {
        VrfRouteImport rt_import(comm);
        return(rt_import.ToString());
    } else if (is_sub_cluster(comm)) {
        SubCluster sc(comm);
        return(sc.ToString());
    }
    return ToHexString(comm);
}

bool ExtCommunity::ContainsRTarget(const ExtCommunityValue &val) const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
         it != communities_.end(); ++it) {
        if (ExtCommunity::is_route_target(*it) && *it == val)
            return true;
    }
    return false;
}

bool ExtCommunity::ContainsOriginVn(const ExtCommunityValue &val) const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
         it != communities_.end(); ++it) {
        if (ExtCommunity::is_origin_vn(*it) && *it == val)
            return true;
    }
    return false;
}

bool ExtCommunity::ContainsOriginVn(as_t asn, uint32_t vn_index) const {
    if (asn <= 0xffffffff) {
        OriginVn origin_vn(asn, vn_index);
        return ContainsOriginVn(origin_vn.GetExtCommunity());
    }
    OriginVn origin_vn4(asn, AS_TRANS);
    OriginVn origin_vn(AS_TRANS, vn_index);
    return (ContainsOriginVn(origin_vn.GetExtCommunity()) &&
                ContainsOriginVn(origin_vn4.GetExtCommunity()));
}

bool ExtCommunity::ContainsSourceAs(const ExtCommunityValue &val) const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
         it != communities_.end(); ++it) {
        if (ExtCommunity::is_source_as(*it) && *it == val)
            return true;
    }
    return false;
}

uint32_t ExtCommunity::GetSubClusterId() const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
            it != communities_.end(); ++it) {
        if (ExtCommunity::is_sub_cluster(*it)) {
            SubCluster sc(*it);
            return sc.GetId();
        }
    }
    return 0;
}

bool ExtCommunity::ContainsVrfRouteImport(const ExtCommunityValue &val) const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
         it != communities_.end(); ++it) {
        if (ExtCommunity::is_vrf_route_import(*it) && *it == val)
            return true;
    }
    return false;
}

void ExtCommunity::RemoveMFlags() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_multicast_flags(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveRTarget() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_route_target(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveSGID() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_security_group(*it) ||
                ExtCommunity::is_security_group4(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveTag() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_tag(*it) || ExtCommunity::is_tag4(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveSiteOfOrigin() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_site_of_origin(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveSourceAS() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_source_as(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}
void ExtCommunity::RemoveVrfRouteImport() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_vrf_route_import(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveOriginVn() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_origin_vn(*it))
            it = communities_.erase(it);
        else
            ++it;
    }
}

void ExtCommunity::RemoveTunnelEncapsulation() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_tunnel_encap(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveLoadBalance() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_load_balance(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveSubCluster() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_sub_cluster(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

vector<string> ExtCommunity::GetTunnelEncap() const {
    vector<string> encap_list;
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tunnel_encap(*iter))
            continue;
        TunnelEncap encap(*iter);
        if (encap.tunnel_encap() == TunnelEncapType::UNSPEC)
            continue;
        encap_list.push_back(encap.ToXmppString());
    }

    sort(encap_list.begin(), encap_list.end());
    vector<string>::iterator encap_iter =
        unique(encap_list.begin(), encap_list.end());
    encap_list.erase(encap_iter, encap_list.end());
    return encap_list;
}

vector<int> ExtCommunity::GetTagList(as2_t asn) const {
    vector<int> tag_list;
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tag(*iter))
            continue;
        Tag tag_comm(*iter);
        if (asn && tag_comm.as_number() != asn && !tag_comm.IsGlobal())
            continue;
        tag_list.push_back(tag_comm.tag());
    }

    sort(tag_list.begin(), tag_list.end());
    vector<int>::iterator tag_iter = unique(tag_list.begin(), tag_list.end());
    tag_list.erase(tag_iter, tag_list.end());
    return tag_list;
}

vector<int> ExtCommunity::GetTag4List(as_t asn) const {
    vector<int> tag_list;
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tag4(*iter))
            continue;
        Tag4ByteAs tag_comm(*iter);
        if (asn && tag_comm.as_number() != asn && !tag_comm.IsGlobal())
            continue;
        vector<int> matching_tag_list = GetTagList(tag_comm.tag());
        tag_list.insert(tag_list.end(), matching_tag_list.begin(),
                        matching_tag_list.end());
        tag_list.push_back(tag_comm.tag());
    }
    if ((asn <= 0xffff) && tag_list.size() == 0)
        tag_list = GetTagList(asn);

    sort(tag_list.begin(), tag_list.end());
    vector<int>::iterator tag_iter = unique(tag_list.begin(), tag_list.end());
    tag_list.erase(tag_iter, tag_list.end());
    return tag_list;
}

bool ExtCommunity::ContainsTunnelEncapVxlan() const {
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tunnel_encap(*iter))
            continue;
        TunnelEncap encap(*iter);
        if (encap.tunnel_encap() == TunnelEncapType::VXLAN)
            return true;
    }
    return false;
}

int ExtCommunity::GetOriginVnIndex() const {
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (ExtCommunity::is_origin_vn(*iter)) {
            OriginVn origin_vn(*iter);
            return origin_vn.vn_index();
        }
    }
    return -1;
}

ExtCommunity::ExtCommunity(ExtCommunityDB *extcomm_db,
        const ExtCommunitySpec spec) : extcomm_db_(extcomm_db) {
    refcount_ = 0;
    for (vector<uint64_t>::const_iterator it = spec.communities.begin();
         it < spec.communities.end(); ++it) {
        ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), *it);
        communities_.push_back(comm);
    }
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

ExtCommunityDB::ExtCommunityDB(BgpServer *server) {
}

ExtCommunityPtr ExtCommunityDB::AppendAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->Append(list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::AppendAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &value) {
    ExtCommunity::ExtCommunityList list;
    list.push_back(value);
    return AppendAndLocate(src, list);
}

ExtCommunityPtr ExtCommunityDB::RemoveAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->Remove(list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceMFlagsAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &export_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveMFlags();
    clone->Append(export_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceRTargetAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &export_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveRTarget();
    clone->Append(export_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSGIDListAndLocate(
    const ExtCommunity *src,
    const ExtCommunity::ExtCommunityList &sgid_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSGID();
    clone->Append(sgid_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceTagListAndLocate(
    const ExtCommunity *src,
    const ExtCommunity::ExtCommunityList &tag_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveTag();
    clone->Append(tag_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::RemoveSiteOfOriginAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSiteOfOrigin();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSiteOfOriginAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &soo) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSiteOfOrigin();
    clone->Append(soo);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::RemoveSourceASAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSourceAS();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSourceASAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &sas) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSourceAS();
    clone->Append(sas);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::RemoveVrfRouteImportAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveVrfRouteImport();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceVrfRouteImportAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &vit) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveVrfRouteImport();
    clone->Append(vit);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::RemoveOriginVnAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveOriginVn();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceOriginVnAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &origin_vn) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveOriginVn();
    clone->Append(origin_vn);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceTunnelEncapsulationAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &tunnel_encaps) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveTunnelEncapsulation();
    clone->Append(tunnel_encaps);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceLoadBalanceAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &lb) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveLoadBalance();
    clone->Append(lb);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSubClusterAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &sc) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSubCluster();
    clone->Append(sc);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::SetAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &value) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->Set(value);
    return Locate(clone);
}

