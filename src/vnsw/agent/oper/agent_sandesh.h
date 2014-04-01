/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_sandesh_h_
#define vnsw_agent_sandesh_h_

#include <db/db_table_walker.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

class VrfEntry;
class DBEntryBase;

/////////////////////////////////////////////////////////////////////////////
// To handle Sandesh for Agent DB
/////////////////////////////////////////////////////////////////////////////
class AgentSandesh {
public:
    static const uint8_t entries_per_sandesh = 100;

    AgentSandesh(std::string context,
                 std::string name) : name_(name), resp_(NULL),
                   count_(0), context_(context),
                   walkid_(DBTableWalker::kInvalidWalkerId) {}
    virtual ~AgentSandesh() {}
    void DoSandesh();

protected:
    std::string name_; // name coming in the sandesh request
    SandeshResponse *resp_;
private:
    bool EntrySandesh(DBEntryBase *entry);
    void SandeshDone();
    void SetResp();
    virtual DBTable *AgentGetTable() = 0;
    virtual void Alloc() = 0;
    virtual bool UpdateResp(DBEntryBase *entry);

    uint32_t count_;
    std::string context_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(AgentSandesh);
};

class AgentVnSandesh : public AgentSandesh {
public:
    AgentVnSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentSgSandesh : public AgentSandesh {
public:
    AgentSgSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVmSandesh : public AgentSandesh {
public:
    AgentVmSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentIntfSandesh : public AgentSandesh {
public:
    AgentIntfSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentNhSandesh : public AgentSandesh {
public:
    AgentNhSandesh(std::string context) : AgentSandesh(context, "") {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentMplsSandesh : public AgentSandesh {
public:
    AgentMplsSandesh(std::string context) : AgentSandesh(context, "") {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVrfSandesh : public AgentSandesh {
public:
    AgentVrfSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentInet4UcRtSandesh : public AgentSandesh {
public:
    AgentInet4UcRtSandesh(VrfEntry *vrf, std::string context)
        : AgentSandesh(context, ""), vrf_(vrf) {
        dump_table_ = true;
    }
    AgentInet4UcRtSandesh(VrfEntry *vrf, std::string context,
                          Ip4Address addr, uint8_t plen)
        : AgentSandesh(context, ""), vrf_(vrf), addr_(addr), plen_(plen) {
        dump_table_ = false;
    }

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    Ip4Address addr_;
    uint8_t plen_;
    bool dump_table_;
};

class AgentInet4McRtSandesh : public AgentSandesh {
public:
    AgentInet4McRtSandesh(VrfEntry *vrf, std::string context, std::string name)
        : AgentSandesh(context, name), vrf_(vrf) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
};

class AgentLayer2RtSandesh : public AgentSandesh {
public:
    AgentLayer2RtSandesh(VrfEntry *vrf, std::string context, std::string name)
        : AgentSandesh(context, name), vrf_(vrf) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
};

class AgentAclSandesh : public AgentSandesh {
public:
    AgentAclSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);
};

class AgentMirrorSandesh : public AgentSandesh {
public:
    AgentMirrorSandesh(std::string context) : AgentSandesh(context, "") {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVrfAssignSandesh : public AgentSandesh {
public:
    AgentVrfAssignSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVxLanSandesh : public AgentSandesh {
public:
    AgentVxLanSandesh(std::string context) : AgentSandesh(context, "") {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

#endif // vnsw_agent_sandesh_h_
