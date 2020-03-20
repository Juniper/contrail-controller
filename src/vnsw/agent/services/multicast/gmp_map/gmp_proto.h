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

class GmpSourceGroup {
public:
    GmpSourceGroup() {
        source_ = IpAddress();
        group_ = IpAddress();
        flags_ = 0;
        mode_ = GMP_MODE_INVALID;
        refcount_ = 0;
    }
    ~GmpSourceGroup() { }

    enum IgmpVersion {
        IGMP_VERSION_V1  = 1 << 0,
        IGMP_VERSION_V2  = 1 << 1,
        IGMP_VERSION_V3  = 1 << 2,
    };

    enum GmpMode {
        GMP_MODE_INVALID,
        GMP_MODE_INCLUDE,
        GMP_MODE_EXCLUDE,
    };

    IpAddress source_;
    IpAddress group_;
    uint8_t flags_;
    GmpMode mode_;
    uint32_t refcount_;
};

class VnGmpIntfState;
class VnGmpDBState : public DBState {
public:
    typedef std::set<GmpSourceGroup *> VnGmpIntfSGList;
    typedef VnGmpIntfSGList::iterator VnGmpSGIntfListIter;

    class VnGmpIntfState {
    public:

        VnGmpIntfState() {
            gmp_intf_ = NULL;
            gmp_intf_sg_list_.clear();
            igmp_enabled_vmi_count_ = 0;

        }
        ~VnGmpIntfState() { }

        GmpIntf *gmp_intf_;
        VnGmpIntfSGList gmp_intf_sg_list_;
        uint32_t igmp_enabled_vmi_count_;
    };

    typedef std::map<IpAddress, VnGmpIntfState *> VnGmpIntfMap;
    typedef std::set<GmpSourceGroup *> VnGmpSGList;
    typedef VnGmpSGList::iterator VnGmpSGListIter;

    VnGmpDBState() : DBState() { }
    ~VnGmpDBState() { }

    VnGmpIntfMap gmp_intf_map_;
    VnGmpSGList gmp_sg_list_;
};

class VmiGmpDBState : public DBState {
public:
    VmiGmpDBState() : DBState(), vrf_name_(), vmi_v4_addr_()  { }
    ~VmiGmpDBState() {}

    std::string vrf_name_;
    IpAddress vmi_v4_addr_;
    bool igmp_enabled_;
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
    typedef boost::function<bool(const VrfEntry *, IpAddress, GmpPacket *)> Callback;
    struct GmpStats {
        GmpStats() { Reset(); }
        void Reset() {
            gmp_g_add_count_ = 0;
            gmp_g_del_count_ = 0;

            gmp_sg_add_count_ = 0;
            gmp_sg_del_count_ = 0;
        }

        uint32_t gmp_g_add_count_;
        uint32_t gmp_g_del_count_;

        uint32_t gmp_sg_add_count_;
        uint32_t gmp_sg_del_count_;
    };

    GmpProto(GmpType::Type type, Agent *agent, const std::string &task_name,
                            int instance, boost::asio::io_service &io);
    ~GmpProto();

    GmpIntf *CreateIntf();
    bool DeleteIntf(GmpIntf *gif);

    void Register(Callback cb) { cb_ = cb; }

    bool Start();
    bool Stop();

    void GmpIntfSGClear(VnGmpDBState *state,
                            VnGmpDBState::VnGmpIntfState *gmp_intf_state);

    DBTableBase::ListenerId vn_listener_id();
    DBTableBase::ListenerId itf_listener_id();

    bool GmpProcessPkt(const VmInterface *vm_itf, void *rcv_pkt,
                            uint32_t packet_len, IpAddress ip_saddr,
                            IpAddress ip_daddr);
    uint8_t *GmpBufferGet();
    void GmpBufferFree(uint8_t *pkt);
    bool GmpNotificationHandler();
    bool GmpNotificationTimer();
    void GmpNotificationReady();
    void GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group,
                            int group_action);
    void ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group);
    void UpdateHostInSourceGroup(GmpIntf *gif, bool join, IpAddress host,
                            IpAddress source, IpAddress group);
    void TriggerMvpnNotification(const VmInterface *vm_intf, bool join,
                                    IpAddress source, IpAddress group);
    void TriggerEvpnNotification(const VmInterface *vm_intf, bool join,
                                    IpAddress source, IpAddress group);
    bool MulticastPolicyCheck(GmpIntf *gif, IpAddress source, IpAddress group);
    bool SendPacket(GmpIntf *gif, uint8_t *pkt, uint32_t pkt_len,
                            IpAddress dest);
    const GmpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    void GmpVnNotify(DBTablePartBase *part, DBEntryBase *entry);
    void GmpVrfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void GmpItfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void TryCreateDeleteIntf(VmiGmpDBState *vmi_state, VmInterface *vm_itf);

    GmpType::Type type_;
    Agent *agent_;
    const std::string &name_;
    int instance_;
    boost::asio::io_service &io_;

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId itf_listener_id_;

    TaskMap *task_map_;
    Timer *gmp_trigger_timer_;
    TaskTrigger *gmp_notif_trigger_;
    mgm_global_data *gd_;
    Callback cb_;

    GmpStats stats_;
    int itf_attach_count_;

    typedef std::map<IpAddress, boost::uuids::uuid> VmIpInterfaceMap;
    VmIpInterfaceMap vm_ip_to_vmi_;

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
