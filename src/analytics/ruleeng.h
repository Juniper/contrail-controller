/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __RULEENG_H__
#define __RULEENG_H__

#include "viz_message.h"
#include "ruleparser/t_ruleparser.h"
#include "base/task.h"
#include "gendb_if.h"

class DbHandler;
class OpServerProxy;

class Ruleeng {
    public:
        static int RuleBuilderID;
        static int RuleWorkerID;

        Ruleeng(DbHandlerPtr, OpServerProxy *);
        virtual ~Ruleeng();

        void Init();
        bool Buildrules(const std::string& rulesrc, const std::string& rulebuf);
        bool Parserules(char *, size_t len);
        bool Parserules(const char *, int len);

        void add_rulesrc(const std::string& rulesrc) {
            rulesrc_.push_back(rulesrc);
        }

        bool rule_present(const VizMsg *vmsgp);

        bool rule_execute(const VizMsg *vmsgp, bool uveproc, DbHandler *db,
            GenDb::GenDbIf::DbAddColumnCb db_cb);

        void print(std::ostream& os) {
            rulelist_->print(os);
        }

        OpServerProxy * GetOSP() { return osp_; }
    private:
        DbHandlerPtr db_handler_;
        OpServerProxy *osp_;
        t_rulelist *rulelist_;
        std::vector<std::string> rulesrc_;

        bool handle_uve_publish(const pugi::xml_node& parent,
            const VizMsg *rmsg, DbHandler *db, const SandeshHeader &header,
            GenDb::GenDbIf::DbAddColumnCb db_cb);

        bool handle_uve_statistics(const pugi::xml_node& parent,
            const VizMsg *rmsg, DbHandler *db, const SandeshHeader &header,
            GenDb::GenDbIf::DbAddColumnCb db_cb);

        bool handle_flow_object(const pugi::xml_node& parent, DbHandler *db,
            const SandeshHeader &header, GenDb::GenDbIf::DbAddColumnCb db_cb);

        bool handle_session_object(const pugi::xml_node& parent, DbHandler *db,
            const SandeshHeader &header, GenDb::GenDbIf::DbAddColumnCb db_cb);

        void handle_object_log(const pugi::xml_node& parent,
            const VizMsg *rmsg, DbHandler *db, const SandeshHeader &header,
            DbHandler::ObjectNamesVec *object_names,
            GenDb::GenDbIf::DbAddColumnCb db_cb);

        void remove_identifier(const pugi::xml_node& parent);
};

class Builder : public Task {
public:
    Builder(Ruleeng *re, const std::string& rulesrc, const std::string& rulebuf) : Task(Ruleeng::RuleBuilderID), re_(re), rulesrc_(rulesrc), rulebuf_(rulebuf) { }
    ~Builder() { }

    virtual bool Run() {
        re_->add_rulesrc(rulesrc_);
        re_->Parserules(rulebuf_.c_str(), rulebuf_.size());
        return true;
    }
    std::string Description() const { return "Builder"; }

private:
    Ruleeng *re_;
    std::string rulesrc_;
    std::string rulebuf_;
};



#endif
