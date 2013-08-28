/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __WORK_PIPELINE_INL_H__
#define __WORK_PIPELINE_INL_H__


template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
WorkStage<InputT, ResultT, ExternalT, SubResultT>::WorkStage(
    std::vector<std::pair<int,int> > tinfo,
    ExecuteFn efn, MergeFn mfn, int tid, int tinst) :
        stage_(std::numeric_limits<uint32_t>::max()),
        merger_(mfn),
        efn_(efn),
        finished_(false),
        running_(false),
        tid_(tid),
        tinst_(tinst),
        tinfo_(tinfo) {}


template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
void
WorkStage<InputT, ResultT, ExternalT, SubResultT>::Start(uint32_t stage, FinFn finFn,
        const boost::shared_ptr<InputT> & inp) {
    assert(!running_);
    assert(!finished_);
    stage_ = stage;
    inp_ = inp;
    finFn_ = finFn;
    remainingInst_ = tinfo_.size();
    for (uint32_t tk = 0 ; tk < tinfo_.size(); tk++) {
        workers_.push_back(boost::shared_ptr<WorkProcessor<InputT,SubResultT,ExternalT> >(
                new WorkProcessor<InputT,SubResultT,ExternalT>(stage_, efn_,
                    boost::bind(&WorkStage<InputT,ResultT,ExternalT,SubResultT>::WorkProcCb,
                                this, tk, _1), 
                    *inp_, tk, tinfo_[tk].first, tinfo_[tk].second)));
    }
    for (uint32_t tk = 0 ; tk < tinfo_.size(); tk++) {
        workers_[tk]->Start();
    }

}

template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
boost::shared_ptr<ResultT>
WorkStage<InputT, ResultT, ExternalT, SubResultT>::Result() const {
    if (!finished_) return boost::shared_ptr<ResultT>();
    return res_;
}

template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
void
WorkStage<InputT, ResultT, ExternalT, SubResultT>::Release() {
    assert(finished_);
    assert(subRes_.size() == tinfo_.size());
    inp_.reset();
    for (uint32_t i = 0; i < subRes_.size(); i++) {
        subRes_[i].reset();
    }
    res_.reset();
}


template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
bool
WorkStage<InputT, ResultT, ExternalT, SubResultT>::Runner(void) {
    running_ = true;
    if (!(merger_)(subRes_, inp_, *res_)) {
       return false; 
    }
    running_ = false;
    finished_ = true;
    finFn_(true);
    return true;
}


template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT>
void
WorkStage<InputT, ResultT, ExternalT, SubResultT>::WorkProcCb(uint32_t inst, bool ret_code) {
    uint32_t prev = remainingInst_.fetch_and_decrement();
    if (prev == 1) {
        assert(workers_.size() == tinfo_.size());
        for (uint32_t i = 0; i < workers_.size(); i++) {
            subRes_.push_back(workers_[i]->Result());
            workers_[i]->Release();
        }
        StageProceed(boost::is_same<ResultT,SubResultT>());
        if (merger_.empty()) {
            finished_ = true;
            finFn_(true);
        } else {
            res_.reset(new ResultT);
            PipelineWorker *w = new PipelineWorker(tid_, tinst_,
                    boost::bind(&WorkStage<InputT,ResultT,ExternalT,SubResultT>::Runner,
                    this));
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            scheduler->Enqueue(w);                  
        }
    }
}


template<typename T0, typename T1, typename T2, typename T3, 
    typename T4, typename T5, typename T6>
WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::WorkPipeline(
    WorkStageIf<T0,T1> * s0,
    WorkStageIf<T1,T2> * s1,
    WorkStageIf<T2,T3> * s2,
    WorkStageIf<T3,T4> * s3,
    WorkStageIf<T4,T5> * s4,
    WorkStageIf<T5,T6> * s5) :
        finished_(false),
        sg_(
            boost::shared_ptr<WorkStageIf<T0,T1> >(s0),
            boost::shared_ptr<WorkStageIf<T1,T2> >(s1),
            boost::shared_ptr<WorkStageIf<T2,T3> >(s2),
            boost::shared_ptr<WorkStageIf<T3,T4> >(s3),
            boost::shared_ptr<WorkStageIf<T4,T5> >(s4),
            boost::shared_ptr<WorkStageIf<T5,T6> >(s5)) {}

template<typename T0, typename T1, typename T2, typename T3, 
    typename T4, typename T5, typename T6>
void
WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::Start(FinFn finFn, const boost::shared_ptr<T0> & inp) {
    inp_ = inp;
    finFn_ = finFn;
    boost::get<0>(sg_)->Start(0, boost::bind(&SelfT::WorkStageCb,
        this, 0, _1),inp_);
}

template<typename T0, typename T1, typename T2, typename T3, 
    typename T4, typename T5, typename T6>
boost::shared_ptr<typename WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::ResT>
WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::Result() const {
    if (!finished_) return boost::shared_ptr<ResT>();
    return res_;
}

template<typename T0, typename T1, typename T2, typename T3, 
    typename T4, typename T5, typename T6>
void
WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::WorkStageCb(uint32_t stage, bool ret_code) {
    switch(stage) {
    case 0: {
            NextStage<0,T1>();
        }
        break;
    case 1: {
            NextStage<1,T2>();
        }
        break;
    case 2: {
            NextStage<2,T3>();
        }
        break;
    case 3: {
            NextStage<3,T4>();
        }
        break;
    case 4: {
            NextStage<4,T5>();
        }
        break;            
    case 5: {
            res_ = boost::get<5>(sg_)->Result();
            boost::get<5>(sg_)->Release();
            finished_ = true;
            finFn_(true);
        }
    break;
    }
}

template<typename T0, typename T1, typename T2, typename T3, 
    typename T4, typename T5, typename T6>
template<int kS,typename NextT>
void
WorkPipeline<T0,T1,T2,T3,T4,T5,T6>::NextStage() {
    boost::shared_ptr<NextT> res = boost::get<kS>(sg_)->Result();
    PipeProceed<kS,boost::is_same<NextT,ResT>::value>::Do(this);
    boost::get<kS>(sg_)->Release();
    if (boost::get<kS+1>(sg_)) {
        res_.reset();
        boost::get<kS+1>(sg_)->Start(kS+1, boost::bind(&SelfT::WorkStageCb,
            this, kS+1, _1),res);
    } else {
        finished_ = true;
        finFn_(true);            
    }
}

#endif