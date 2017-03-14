/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_MGMT_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_MGMT_H_
#include "mac_learning_key.h"

class MacLearningMgmtDBEntry;
class MacLearningMgmtManager;
class MacLearningMgmtDBTree;
class MacLearningMgmtNode {
public:
    MacLearningMgmtNode(MacLearningEntryPtr ptr);
    virtual ~MacLearningMgmtNode();

    void set_intf(MacLearningMgmtDBEntry *intf) {
        intf_.reset(intf);
    }

    void set_vrf(MacLearningMgmtDBEntry *vrf) {
        vrf_.reset(vrf);
    }

    void set_rt(MacLearningMgmtDBEntry *rt) {
        rt_.reset(rt);
    }

    void UpdateRef(MacLearningMgmtManager *mgr);

    MacLearningEntryPtr mac_learning_entry() {
        return mac_entry_;
    }

    void set_mac_learning_entry(MacLearningEntryPtr mac_entry) {
        mac_entry_ = mac_entry;
    }
private:
    MacLearningEntryPtr mac_entry_;
    DependencyRef<MacLearningMgmtNode, MacLearningMgmtDBEntry> intf_;
    DependencyRef<MacLearningMgmtNode, MacLearningMgmtDBEntry> vrf_;
    DependencyRef<MacLearningMgmtNode, MacLearningMgmtDBEntry> rt_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtNode);
};
typedef boost::shared_ptr<MacLearningMgmtNode> MacLearningMgmtNodePtr;

class MacLearningMgmtDBEntry {
public:
    typedef DependencyList<MacLearningMgmtNode,
                           MacLearningMgmtDBEntry> MacLearningEntryList;
    enum Type {
        INVALID,
        INTERFACE,
        VRF,
        BRIDGE,
        END
    };

     MacLearningMgmtDBEntry(Type type_, const DBEntry *entry);
     virtual ~MacLearningMgmtDBEntry() {}

     const DBEntry *db_entry() const {
         return db_entry_;
     }

     virtual bool UseDBEntry() const {
         return true;
     }
     void Change();
     void Delete(bool set_delete);
     virtual bool TryDelete();

     void set_tree(MacLearningMgmtDBTree *tree) {
         tree_ = tree;
     }

     void set_db_entry(const DBEntry *entry) {
         db_entry_ = entry;
     }

     virtual bool Compare(const MacLearningMgmtDBEntry *rhs) const {
         return false;
     }

     virtual bool IsLess(const MacLearningMgmtDBEntry *rhs) const {
         if (type_ != rhs->type_) {
             return type_ < rhs->type_;
         }

         if (UseDBEntry()) {
             if (db_entry_ != rhs->db_entry_) {
                 return db_entry_ < rhs->db_entry_;
             }
         }

         return Compare(rhs);
     }

     MacLearningMgmtDBTree* tree() const {
         return tree_;
     }

     void set_gen_id(uint32_t gen_id) {
         gen_id_ = gen_id;
     }

     uint32_t gen_id() const {
         return gen_id_;
     }

protected:
     Type type_;
     const DBEntry* db_entry_;
     bool deleted_;
     DEPENDENCY_LIST(MacLearningMgmtNode, MacLearningMgmtDBEntry, mac_entry_list_);
     MacLearningMgmtDBTree *tree_;
     uint32_t gen_id_;
     DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtDBEntry);
};

class MacLearningMgmtIntfEntry : public MacLearningMgmtDBEntry {
public:
     MacLearningMgmtIntfEntry(const Interface *intf);
     virtual ~MacLearningMgmtIntfEntry() {}
private:
    DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtIntfEntry);
};

class MacLearningMgmtVrfEntry : public MacLearningMgmtDBEntry {
public:
     MacLearningMgmtVrfEntry(const VrfEntry *vrf);
     virtual ~MacLearningMgmtVrfEntry() {}
     virtual bool TryDelete();

private:
     DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtVrfEntry);
};

class MacLearningMgmtRouteEntry : public MacLearningMgmtDBEntry {
public:
    MacLearningMgmtRouteEntry(const AgentRoute *rt);
    MacLearningMgmtRouteEntry(const std::string &vrf,
                              const MacAddress &mac);
   virtual ~MacLearningMgmtRouteEntry() {}
   virtual bool UseDBEntry() const {
       return false;
   }

   virtual bool TryDelete();
   virtual bool Compare(const MacLearningMgmtDBEntry *rhs) const {
       const MacLearningMgmtRouteEntry *rhs_rt =
           static_cast<const MacLearningMgmtRouteEntry *>(rhs);
       if (vrf_ != rhs_rt->vrf_) {
           return vrf_ < rhs_rt->vrf_;
       }
       return mac_ < rhs_rt->mac_;
   }

   const std::string& vrf() {
       return vrf_;
   }
private:
    std::string vrf_;
    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtRouteEntry);
};

class MacLearningMgmtKeyCmp {
public:
     bool operator()(const MacLearningMgmtDBEntry *lhs,
                     const MacLearningMgmtDBEntry *rhs) {
         return lhs->IsLess(rhs);
     }
};

class MacLearningMgmtDBTree {
public:
    typedef std::map<MacLearningMgmtDBEntry *, MacLearningMgmtDBEntry *,
                     MacLearningMgmtKeyCmp> Tree;
    typedef std::pair<MacLearningMgmtDBEntry *, MacLearningMgmtDBEntry *>
        MacLearningMgmtDBPair;

    MacLearningMgmtDBTree(MacLearningMgmtManager *mgr);
    virtual ~MacLearningMgmtDBTree() {}

    void Add(MacLearningMgmtDBEntry *entry);
    void Change(MacLearningMgmtDBEntry *entry);
    void Delete(MacLearningMgmtDBEntry *entry);
    void Erase(MacLearningMgmtDBEntry *entry);
    MacLearningMgmtDBEntry* Find(MacLearningMgmtDBEntry *entry);
    MacLearningMgmtDBEntry* LowerBound(MacLearningMgmtDBEntry* entry) {
        Tree::iterator it = tree_.lower_bound(entry);
        if (it != tree_.end()) {
            return it->second;
        }
        return NULL;
    }

    MacLearningMgmtManager* mac_learning_mac_manager() {
        return mac_learning_mac_manager_;
    }
    void TryDelete(MacLearningMgmtDBEntry *db_entry);
protected:
    Tree tree_;
    MacLearningMgmtManager *mac_learning_mac_manager_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtDBTree);
};

class MacLearningMgmtRequest {
public:
    enum Event {
        ADD_MAC,
        CHANGE_MAC,
        DELETE_MAC,
        ADD_DBENTRY,
        CHANGE_DBENTRY,
        DELETE_DBENTRY,
        DELETE_ALL_MAC,
        RELEASE_TOKEN,
    };

    MacLearningMgmtRequest(Event event, MacLearningEntryPtr ptr):
        event_(event), mac_learning_entry_(ptr) {
    }

    MacLearningMgmtRequest(Event event, const DBEntry *db_entry,
                           uint32_t gen_id):
        event_(event), db_entry_(db_entry), gen_id_(gen_id) {
    }

    MacLearningEntryPtr mac_learning_entry() {
        return mac_learning_entry_;
    }

    Event event() {
        return event_;
    }

    const DBEntry *db_entry() {
        return db_entry_;
    }

    uint32_t gen_id() const {
        return gen_id_;
    }

    void set_gen_id(uint32_t gen_id) {
        gen_id_ = gen_id;
    }

private:
    Event event_;
    MacLearningEntryPtr mac_learning_entry_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
};

typedef boost::shared_ptr<MacLearningMgmtRequest> MacLearningMgmtRequestPtr;

class MacLearningMgmtManager {
public:
    typedef WorkQueue<MacLearningMgmtRequestPtr> MacLearningMgmtQueue;
    typedef std::map<MacLearningKey, MacLearningMgmtNodePtr,
                     MacLearningKeyCmp> MacLearningNodeTree;
    typedef std::pair<MacLearningKey, MacLearningMgmtNodePtr>
        MacLearningNodePair;

    MacLearningMgmtManager(Agent *agent);
    virtual ~MacLearningMgmtManager() {}
    bool RequestHandler(MacLearningMgmtRequestPtr ptr);

    void AddMacLearningEntry(MacLearningMgmtRequestPtr ptr);
    void ReleaseToken(MacLearningMgmtRequestPtr ptr);
    void DeleteMacLearningEntry(MacLearningMgmtRequestPtr ptr);
    void AddDBEntry(MacLearningMgmtRequestPtr ptr);
    void DeleteDBEntry(MacLearningMgmtRequestPtr ptr);
    void Enqueue(MacLearningMgmtRequestPtr &ptr);
    void DeleteAllEntry(MacLearningMgmtRequestPtr ptr);

    MacLearningMgmtDBEntry* Locate(const DBEntry *e);
    MacLearningMgmtDBEntry* Find(const DBEntry *e);
    MacLearningMgmtDBEntry* Locate(const std::string &vrf,
                                   const MacAddress &mac);
    MacLearningMgmtDBTree* vrf_tree() {
        return &vrf_tree_;
    }

    MacLearningMgmtDBTree* rt_tree() {
        return &rt_tree_;
    }

    bool IsVrfRouteEmpty(const std::string &vrf_name);
    Agent *agent() const {
        return agent_;
    }
private:
    Agent *agent_;
    MacLearningNodeTree mac_learning_node_tree_;
    MacLearningMgmtDBTree intf_tree_;
    MacLearningMgmtDBTree vrf_tree_;
    MacLearningMgmtDBTree rt_tree_;
    MacLearningMgmtQueue request_queue_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningMgmtManager);
};
#endif
