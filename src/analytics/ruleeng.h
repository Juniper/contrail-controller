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

        Ruleeng(DbHandler *, OpServerProxy *);
        virtual ~Ruleeng();

        void Init();
        bool Buildrules(const std::string& rulesrc, const std::string& rulebuf);
        bool Parserules(char *, size_t len);
        bool Parserules(const char *, int len);

        void add_rulesrc(const std::string& rulesrc) {
            rulesrc_.push_back(rulesrc);
        }

        bool rule_present(const boost::shared_ptr<VizMsg> vmsgp);

        bool rule_execute(const boost::shared_ptr<VizMsg> vmsgp);

        void print(std::ostream& os) {
            rulelist_->print(os);
        }

        OpServerProxy * GetOSP() { return osp_; }
    private:
        DbHandler *db_handler_;
        OpServerProxy *osp_;
        t_rulelist *rulelist_;
        std::vector<std::string> rulesrc_;

        bool handle_uve_publish(const RuleMsg& rmsg);

        bool handle_flow_object(const RuleMsg& rmsg);

        void handle_object_log(const pugi::xml_node& parent, const RuleMsg& rmsg,
                const boost::uuids::uuid& unm);

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

private:
    Ruleeng *re_;
    std::string rulesrc_;
    std::string rulebuf_;
};



#endif
