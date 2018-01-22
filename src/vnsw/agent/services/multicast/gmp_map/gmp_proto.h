/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_gmp_proto_h
#define vnsw_agent_gmp_proto_h

typedef struct gmp_intf_ gmp_intf;
typedef struct _mgm_global_data mgm_global_data;
class TaskMap;
class GmpIntf;
class GmpProto;

class GmpIntf {
public:
    GmpIntf(const GmpProto *);
    ~GmpIntf() {};

    void SetGif(gmp_intf *gif) { gif_ = gif; }
    gmp_intf *GetGif() { return gif_; }
    bool SetIpAddress(const IpAddress &ip_addr);

private:
    const GmpProto *gmp_proto_;
    IpAddress ip_addr_;
    gmp_intf *gif_;
};

struct GmpType {
    enum Type {
        INVALID,
        IGMP,
        MAX_TYPE,
    };
};

class GmpProto {
public:
    GmpProto(GmpType::Type type, Agent *agent, const std::string &task_name, 
                            int instance, boost::asio::io_service &io);
    ~GmpProto();
    GmpIntf *CreateIntf();

    bool DeleteIntf(GmpIntf *gif);
    bool Start();
    bool Stop();
    bool GmpProcessPkt(GmpIntf *gif, void *rcv_pkt, uint32_t packet_len,
                            IpAddress ip_saddr, IpAddress ip_daddr);
    void GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group, bool add);
    void ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group);
    void UpdateHostInSourceGroup(GmpIntf *gif, IpAddress host, IpAddress source,
                            IpAddress group);

private:
    GmpType::Type type_;
    Agent *agent_;
    const std::string &name_;
    int instance_;
    boost::asio::io_service &io_;

    TaskMap *task_map_;
    mgm_global_data *gd_;

    friend class GmpIntf;
};

class GmpProtoManager {
public:
    static GmpProto *CreateGmpProto(GmpType::Type type, Agent *agent,
                            const std::string &name, int instance,
                            boost::asio::io_service &io);
    static bool DeleteGmpProto(GmpProto *gmp_proto);

    friend class GmpProto;
};

#endif /* vnsw_agent_gmp_proto_h */
