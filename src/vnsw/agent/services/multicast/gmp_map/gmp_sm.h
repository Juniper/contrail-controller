/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_gmp_sm_h
#define vnsw_agent_gmp_sm_h

typedef struct gmp_intf_ gmp_intf;
typedef struct _mgm_global_data mgm_global_data;
class TaskMap;
class GmpIntf;
class GmpSm;

class GmpIntf {
public:
    GmpIntf(const GmpSm *);
    ~GmpIntf() {};

    void SetGif(gmp_intf *gif) { gif_ = gif; }
    gmp_intf *GetGif() { return gif_; }
    bool SetIpAddress(const IpAddress &ip_addr);

private:
    const GmpSm *gmp_sm_;
    IpAddress ip_addr_;
    gmp_intf *gif_;
};

class GmpSm {
public:
    GmpSm(Agent *agent, const std::string &name, PktHandler::PktModuleName mod,
                            boost::asio::io_service &io);
    ~GmpSm();
    GmpIntf *CreateIntf();

    bool DeleteIntf(GmpIntf *gif);
    bool Start();
    bool Stop();
    bool GmpProcessPkt(GmpIntf *gif, void *rcv_pkt, u_int32_t packet_len,
                            const u_int8_t *src_addr, const u_int8_t *dst_addr);

private:
    Agent *agent_;
    const std::string &name_;
    PktHandler::PktModuleName module_;
    boost::asio::io_service &io_;

    TaskMap *task_map_;
    mgm_global_data *gd_;

    friend class GmpIntf;
};

class GmpSmManager {
public:
    static GmpSm *CreateGmpSm(Agent *agent, const std::string &name,
            PktHandler::PktModuleName module, boost::asio::io_service &io);
    static bool DeleteGmpSm(GmpSm *gmp_sm);

    friend class GmpSm;
};

#endif /* vnsw_agent_gmp_sm_h */
