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

#define GMP_TX_BUFF_LEN            512

struct GmpPacket {
    GmpPacket(uint8_t *pkt, uint32_t pkt_len, IpAddress dst_addr)
        : pkt_(pkt), pkt_len_(pkt_len), dst_addr_(dst_addr) {}

    uint8_t *pkt_;
    uint32_t pkt_len_;
    IpAddress dst_addr_;
};

class GmpIntf {
public:
    GmpIntf(const GmpProto *);
    ~GmpIntf() {};

    void SetGif(gmp_intf *gif) { gif_ = gif; }
    gmp_intf *GetGif() { return gif_; }
    const IpAddress get_ip_address() { return ip_addr_; }
    bool set_ip_address(const IpAddress &ip_addr);
    const string &get_vrf_name() { return vrf_name_; }
    bool set_vrf_name(const std::string &vrf_name);
    bool set_gmp_querying(bool querying);

private:
    const GmpProto *gmp_proto_;
    string vrf_name_;
    IpAddress ip_addr_;
    gmp_intf *gif_;
    bool querying_;
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
    static const int kGmpTriggerRestartTimer = 100; /* milli-seconds */
    typedef boost::function<bool(GmpIntf *, IpAddress, IpAddress)> PolicyCb;
    typedef boost::function<bool(GmpIntf *, GmpPacket *)> Callback;
    struct GmpStats {
        GmpStats() { Reset(); }
        void Reset() {
            gmp_sgh_add_count_ = 0;
            gmp_sgh_del_count_ = 0;
        }

        uint32_t gmp_sgh_add_count_;
        uint32_t gmp_sgh_del_count_;
    };

    GmpProto(GmpType::Type type, Agent *agent, const std::string &task_name,
                            int instance, boost::asio::io_service &io);
    ~GmpProto();
    GmpIntf *CreateIntf();

    bool DeleteIntf(GmpIntf *gif);
    void RegisterPolicyCallback(PolicyCb cb) { policy_cb_ = cb; }
    void Register(Callback cb) { cb_ = cb; }
    bool Start();
    bool Stop();
    bool GmpProcessPkt(GmpIntf *gif, void *rcv_pkt, uint32_t packet_len,
                            IpAddress ip_saddr, IpAddress ip_daddr);
    uint8_t *GmpBufferGet();
    void GmpBufferFree(uint8_t *pkt);
    bool GmpNotificationHandler();
    bool GmpNotificationTimer();
    void GmpNotificationReady();
    void GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group, bool add);
    void ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group);
    void UpdateHostInSourceGroup(GmpIntf *gif, bool join, IpAddress host,
                            IpAddress source, IpAddress group);
    bool MulticastPolicyCheck(GmpIntf *gif, IpAddress source, IpAddress group);
    bool SendPacket(GmpIntf *gif, uint8_t *pkt, uint32_t pkt_len,
                            IpAddress dest);
    const GmpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    GmpType::Type type_;
    Agent *agent_;
    const std::string &name_;
    int instance_;
    boost::asio::io_service &io_;

    TaskMap *task_map_;
    Timer *gmp_trigger_timer_;
    TaskTrigger *gmp_notif_trigger_;
    mgm_global_data *gd_;
    PolicyCb policy_cb_;
    Callback cb_;

    GmpStats stats_;

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
