/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/common/flow_types.h>

bool SessionIpPortProtocol::operator < (const SessionIpPortProtocol &rhs) const {
    if (port < rhs.port) {
        return true;
    } else if (port == rhs.port && protocol < rhs.protocol) {
        return true;
    } else if (port == rhs.port && protocol == rhs.protocol && ip < rhs.ip) {
        return true;
    } else {
        return false;
    }
}

bool SessionIpPort::operator < (const SessionIpPort &rhs) const {
    if (port < rhs.port) {
        return true;
    } else if (port == rhs.port && ip < rhs.ip) {
        return true;
    } else {
        return false;
    }
}

/*
 * print everything in SessionEndpoint structure other than the map
 */
std::string ExtractSEPInfo(const SessionEndpoint &sep) {
    std::stringstream Xbuf;
    //construct per session messages
    Xbuf << "vmi" << " = " << sep.vmi;
    Xbuf << " " << "vn" << " = " << sep.vn;
    if (sep.__isset.deployment) {
        Xbuf << " " << "deployment" << " = " << sep.deployment;
    }
    if (sep.__isset.tier) {
        Xbuf << " " << "tier" << " = " << sep.tier;
    }
    if (sep.__isset.application) {
        Xbuf << " " << "application" << " = " << sep.application;
    }
    if (sep.__isset.site) {
        Xbuf << " " << "site" << " = " << sep.site;
    }
    if (sep.__isset.labels) {
        Xbuf << " " << "labels" << "= [ ";
        {
            Xbuf << " " << "[";
            std::set<std::string> ::const_iterator label_iter;
            for (label_iter = sep.labels.begin();
                 label_iter != sep.labels.end(); ++label_iter) {
                Xbuf << " " << "label val" << " = " << (*label_iter);
                Xbuf << ", ";
            }
            Xbuf << " " << "]";
        }
    }
    Xbuf << " ]";
    if (sep.__isset.remote_deployment) {
        Xbuf << " " << "remote_deployment" << " = " << sep.remote_deployment;
    }
    if (sep.__isset.remote_tier) {
        Xbuf << " " << "remote_tier" << " = " << sep.remote_tier;
    }
    if (sep.__isset.remote_application) {
        Xbuf << " " << "remote_application" << " = " << sep.remote_application;
    }
    if (sep.__isset.remote_site) {
        Xbuf << " " << "remote_site" << " = " << sep.remote_site;
    }
    if (sep.__isset.remote_labels) {
        Xbuf << " " << "remote_labels" << "= [ ";
        {
            Xbuf << " " << "[";
            std::set<std::string> ::const_iterator label_iter;
            for (label_iter = sep.remote_labels.begin();
                 label_iter != sep.remote_labels.end(); ++label_iter) {
                Xbuf << " " << "(label val)" << " = " << (*label_iter);
                Xbuf << ", ";
            }
            Xbuf << " " << "]";
        }
            Xbuf << " ]";
    }
    if (sep.__isset.security_policy_rule) {
        Xbuf << " " << "security_policy_rule" << " = " <<
            sep.security_policy_rule;
    }
    Xbuf << " " << "remote_vn" << " = " << sep.remote_vn;
    Xbuf << " " << "is_client_session" << " = " <<
        integerToString(sep.is_client_session);
    Xbuf << " " << "is_si" << " = " << integerToString(sep.is_si);
    if (sep.__isset.remote_prefix) {
        Xbuf << " " << "remote_prefix" << " = " << sep.remote_prefix;
    }
    Xbuf << " " << "vrouter_ip" << " = " << sep.vrouter_ip;
    return Xbuf.str();
}

/*
 * Print everything in SessionAggInfo struct otherthan the map
 */
std::string PrintSessionAggreateInfo(
     const SessionAggInfo &sess_agg) {
    std::stringstream Xbuf;
    if (sess_agg.__isset.sampled_forward_bytes) {
        Xbuf << "sampled_forward_bytes" << " = " << integerToString(sess_agg.sampled_forward_bytes);
    }
    if (sess_agg.__isset.sampled_forward_pkts) {
        Xbuf << " " << "sampled_forward_pkts" << " = " << integerToString(sess_agg.sampled_forward_pkts);
    }
    if (sess_agg.__isset.sampled_reverse_bytes) {
        Xbuf << " " << "sampled_reverse_bytes" << " = " << integerToString(sess_agg.sampled_reverse_bytes);
    }
    if (sess_agg.__isset.sampled_reverse_pkts) {
        Xbuf << " " << "sampled_reverse_pkts" << " = " << integerToString(sess_agg.sampled_reverse_pkts);
    }
    if (sess_agg.__isset.logged_forward_bytes) {
        Xbuf << " " << "logged_forward_bytes" << " = " << integerToString(sess_agg.logged_forward_bytes);
    }
    if (sess_agg.__isset.logged_forward_pkts) {
        Xbuf << " " << "logged_forward_pkts" << " = " << integerToString(sess_agg.logged_forward_pkts);
    }
    if (sess_agg.__isset.logged_reverse_bytes) {
        Xbuf << " " << "logged_reverse_bytes" << " = " << integerToString(sess_agg.logged_reverse_bytes);
    }
    return Xbuf.str();
}


void SessionEndpointObject::LogUnrolled(std::string category,
    SandeshLevel::type level,
    const std::vector<SessionEndpoint> & session_data) {
    if (!IsLevelCategoryLoggingAllowed(SandeshType::SESSION, level, category)) {
        return;
    }
    log4cplus::LogLevel Xlog4level(SandeshLevelTolog4Level(level));
    log4cplus::Logger Xlogger = Sandesh::logger();
    if (!Xlogger.isEnabledFor(Xlog4level)) {
        return;
    }
    log4cplus::tostringstream Xbuf;
    std::vector<SessionEndpoint> ::const_iterator sep_iter;
    for (sep_iter = session_data.begin(); sep_iter != session_data.end();
         ++sep_iter) {
        std::string sep_info = ExtractSEPInfo(*sep_iter);
        std::map<SessionIpPortProtocol, SessionAggInfo>::const_iterator
            local_ep_iter;
        for (local_ep_iter = sep_iter->sess_agg_info.begin();
             local_ep_iter != sep_iter->sess_agg_info.end(); ++local_ep_iter++) {
            std::map<SessionIpPort, SessionInfo>::const_iterator sessions_iter;
            std::string local_ep_info = local_ep_iter->first.log();
            std::string sess_agg_data = PrintSessionAggreateInfo(local_ep_iter->second);
            // for each of the individual session
            for (sessions_iter = local_ep_iter->second.sessionMap.begin();
                 sessions_iter != local_ep_iter->second.sessionMap.end();
                 ++sessions_iter) {
                Xbuf.clear();
                Xbuf.str(std::string());
                Xbuf << category << " [" << LevelToString(level) << "]: SessionData: ";
                Xbuf << "[ ";
                //log4cplus::tostringstream Xbuf;
                Xbuf << sep_info << " ";
                // print sess_agg_info keys [ip, port, protocol of local]
                Xbuf << local_ep_info << " ";
                // print sess_agg_info values [aggregate logged and sampled data
                // for one session endpoint]
                Xbuf << sess_agg_data << " ";
                // print SessionInfo keys [ip, port of the remote end]
                Xbuf << sessions_iter->first.log() << " " ;
                // print SessionInfo values [aggregate data for individual session]
                Xbuf << sessions_iter->second.log() << " ";
                Xbuf << " ]";
                Xlogger.forcedLog(Xlog4level, Xbuf.str());
            }
        }
    }
}

