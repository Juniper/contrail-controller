/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __WORK_PROCESSOR_INL_H__
#define __WORK_PROCESSOR_INL_H__

#include <vector>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/type_traits.hpp>
#include <boost/mpl/assert.hpp>
#include <boost/tuple/tuple.hpp>
#include <limits>
#include <sstream>
#include <tbb/atomic.h> 
#include "base/task.h"

struct PipelineWorker : public Task {
    PipelineWorker(int tid, int tinst, boost::function<bool(void)> runner) :
        Task(tid,tinst), runner_(runner) {}

    bool Run() {
        if (!(runner_)()) return false;
        return true;
    }
private:
    const boost::function<bool(void)> runner_;
};

template<typename InputT, typename SubResultT, typename ExternalT>
class WorkProcessor : public ExternalProcIf<ExternalT> {
public:
    typedef boost::function<ExternalBase::Efn(
        uint32_t inst,
        const std::vector<ExternalT*> & exts, // Info for previous steps of this stage
        const InputT & inp, // Info from previous stage
        SubResultT & subRes // Access to final result of this instance
        )> ExecuteFn;

    WorkProcessor(uint32_t stage, ExecuteFn efn, FinFn finFn, const InputT & inp,
        uint32_t inst, int tid, int tinst) :
            stage_(stage),
            finFn_(finFn),
            efn_(efn),
            inp_(inp),
            finished_(false),
            running_(false),
            inst_(inst),
            tid_(tid),
            tinst_(tinst),
            w_(new PipelineWorker(tid_, tinst_,
                boost::bind(&WorkProcessor<InputT,SubResultT,ExternalT>::Runner,
                this))) {
        res_.reset(new SubResultT);
    }

    void Start() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(w_);
    }   

    boost::shared_ptr<SubResultT> Result() const {
        if (!finished_) return boost::shared_ptr<SubResultT>();
        return res_;
    }

    void Release() {
        assert(finished_);
        res_.reset();
    }
    
    std::string Key() const {
        std::stringstream keystr;
        keystr << "PROC-STAGE:" << stage_ << "-INST:" << inst_ <<
                "-STEP:" << externals_.size();
        return keystr.str();
    }

private:
    uint32_t stage_;
    FinFn finFn_;
    const ExecuteFn efn_;
    const InputT & inp_;
    boost::shared_ptr<SubResultT> res_;
    std::vector<ExternalT*> externals_;
    bool finished_;
    bool running_;
    const uint32_t inst_;
    const int tid_;
    const int tinst_;
    PipelineWorker * const w_;

    void WorkDone(bool ret_code) {
        for (typename std::vector<ExternalT*>::iterator it = externals_.begin();
                it!=externals_.end(); it++) {
            delete (*it);
        }
        finished_ = true;
        finFn_(ret_code);
    }

    bool Runner(void) {
        running_ = true;
        ExternalBase::Efn fn = (efn_)(inst_, externals_, inp_, *res_);
        running_ = false;
        if (fn.empty()) {
            WorkDone(true); 
        } else {
            if (fn == &ExternalBase::Incomplete) return false;

            if (!fn(this)) {
                WorkDone(false);
            }
        }
        return true;
    }

    void Response(std::auto_ptr<ExternalT> resp) {
        ExternalT * msg = resp.get();
        resp.release();
        externals_.push_back(msg);
        assert(!running_);
        PipelineWorker *w = new PipelineWorker(tid_, tinst_,
                boost::bind(&WorkProcessor<InputT,SubResultT,ExternalT>::Runner,
                this));
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(w);        
    }
};

template <typename InputT, typename ResultT>
struct WorkStageIf {
    virtual void Start(uint32_t stage, FinFn finFn, const boost::shared_ptr<InputT> & inp) = 0;
    virtual boost::shared_ptr<ResultT> Result() const = 0;
    virtual void Release() = 0;
    virtual ~WorkStageIf() {}
};
#endif