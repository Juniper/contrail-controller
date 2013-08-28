/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * work_pipeline.h
 * 
 *  This file provides some templates that can be used to manage processing.
 *  
 *  The user creates a WorkPipeline, with multiple WorkStage's inside it.
 *
 *  The stages will execute in sequence. A stage has an execution phase, where
 *  multiple threads (instances) can do processing in parallel, and an optional
 *  merge phase where the results of the parallel execution can be consolidated.
 *  For each stage, the user provides the execution function (of type ExecuteFn) 
 *  (optionally) a merge function (of type MergeFn).
 *
 *  The execution function runs in parallel over a number of instances. Each 
 *  instance executes in steps. During a step, the ExecuteFn is expected to 
 *  provide the external command that needs to be run to get information (e.g. 
 *  cassandra or redis, or another WorkPipleline) . ExecuteFn will called again
 *  (as a next step) when the results of the external command are available. In
 *  that next step, ExecuteFn can consolidate the external result into the
 *  instances' subresult. The user can run multiple such steps within the
 *  instance.
 *
 *  After the subresults from all instances of that stage are available, MergeFn 
 *  will run which can consolidate the subresults into the final result for
 *  that stage. If a stage has a single instance, then MergeFn is not needed 
 *  for that stage.
 *  
 *  After all stages have been executed, the user's callback function is called.
 *  The user can then access the final result of the WorkPipeline. 
 *
 *  Here are some of the features:
 *
 *    - Buffers are shared and transferred wherever possible (instead of copy)
 *
 *    - A generic mechanism is provided to get information from any source. 
 *      Both Async and Sync processing is allowed for the external sources.
 *
 *    - Strong typechecking for Input Type, Subresult Type and Result type 
 *      across stages.
 *
 *    - TaskId and TaskInstance can be customized for ExecuteFn (for every 
 *      instance of every stage) and MergeFn (for every stage)
 *
 *
 *  This file has the interfaces provided by WorkPipeline and WorkStage.
 *  Most of the implementation is split across two other files -
 *  work_processor-inl.h and work_pipeline-inl.h       
 *
 */

#ifndef __WORK_PIPELINE_H__
#define __WORK_PIPELINE_H__

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

// When the WorkPipeline is done, it will call this function back
// to indicate that the result is ready.
// The bool indicates success or failure.
typedef boost::function<void(bool)> FinFn;


// This is the interface to the external command execution that
// can be requested by the user in ExecuteFn
//
// The user provides the function ExternalBase::Efn that the
// WorkStage can call with a single argument (ExternalBase *)
//
// The user should ensure that when the command is done, the
// argument is cast into  ExternalProcIf *, and the
// Response function is called with the result of the the
// external command 
struct ExternalBase {
    typedef boost::function<bool(ExternalBase *)> Efn;
    static bool Incomplete(void *) { assert(0); }
};
template<typename ExternalT>
struct ExternalProcIf : public ExternalBase {
    virtual std::string Key() const = 0;
    virtual void Response(std::auto_ptr<ExternalT>) = 0;
    virtual ~ExternalProcIf() {}
};

#include "work_processor-inl.h"

// When creating the WorkPipeline, the user needs to supply it 
// constructed WorkStage objects.
// The Constructor of this class is the only function
// that the client needs to call.
template<typename InputT, typename ResultT, typename ExternalT, typename SubResultT = ResultT>
class WorkStage : public WorkStageIf<InputT, ResultT> {
public:

    // A WorkStage executes multiple instances in parallel by calling ExecuteFn
    // Each execution run of ExecuteFn is called a "step". 
    // - If the client returns ExternalBase::Incomplete, the step is incomplete.
    //   After yielding, ExecuteFn will be called again to continue the step.
    // - If the client returns NULL, the step ends, and so does the instance.
    // - Otherwise, WorkStage will call the returned callback function
    //   and move to the next step when the client calls ExternalProcIf's
    //   Response function. The buffer passed in the Response function is added
    //   to the exts vector before calling ExecuteFn for the next step.
    // 
    // The client should fill in the final result in subRes before the 
    // instance ends.
    typedef boost::function<
        ExternalBase::Efn( // Client returns External fn to call and
                           // whether processing is incomplete.
        uint32_t inst,     // Instance num. (the instances execute in parallel)
        const std::vector<ExternalT*>
                            & exts, // Info for previous steps of this stage
        const InputT & inp, // Info from previous stage
        SubResultT & subRes // Access to final result of this instance
        )> ExecuteFn;

    // When all the ExecuteFn instances end, the Execute phase of the stage
    // is over. At that time, WorkStage calls MergeFn. MergeFn is expected to 
    // aggregate the SubResults from the ExecuteFn instances into the final
    // Result for the stage.
    // If the SubResult type and Result type is the same, MergeFn is optional.
    // If no MergeFn is supplied, the SubResult of the 1st instance will be 
    // used as the final result of the stage. 
    typedef boost::function<
       bool(              // Client returns whether processing is incomplete.
       const std::vector<boost::shared_ptr<SubResultT> >
                           & subs, // Subresults from this stage's instances
       const boost::shared_ptr<InputT> & inp, // Info from previous stage
       ResultT & res               // Final result to be reported by this stage
       )> MergeFn;

    // The client needs to constuct WorkStages.
    //   tinfo is a vector of TaskId,TaskInstance pairs for ExecuteFn
    //   ExecuteFn and MergeFn were explained above
    //   tid and tinst are the TaskId and TaskInstance used for MergeFn
    WorkStage(std::vector<std::pair<int,int> > tinfo,
            ExecuteFn efn, MergeFn mfn = 0, int tid = 0, int tinst = -1); 

    void Start(uint32_t stage, FinFn finFn, const boost::shared_ptr<InputT> & inp);

    boost::shared_ptr<ResultT> Result() const;

    void Release();

private:
    uint32_t stage_;
    tbb::atomic<uint32_t> remainingInst_;
    FinFn finFn_;
    boost::shared_ptr<InputT> inp_;
    boost::shared_ptr<ResultT> res_;
    std::vector<boost::shared_ptr<WorkProcessor<InputT,SubResultT,ExternalT> > > workers_;
    std::vector<boost::shared_ptr<SubResultT> > subRes_;
    const MergeFn merger_;
    const ExecuteFn efn_;
    bool finished_;
    bool running_;   
    const int tid_;
    const int tinst_;
    const std::vector<std::pair<int,int> > tinfo_;    

    bool Runner(void);
    void WorkProcCb(uint32_t inst, bool ret_code);
    void StageProceed(boost::true_type) { res_ = subRes_[0]; }
    void StageProceed(boost::false_type) { assert(!merger_.empty()); }

};


template<typename T0, typename T1, typename T2 = T1,
    typename T3 = T2, typename T4 = T3,
    typename T5 = T4, typename T6 = T5>
class WorkPipeline {
public:
    typedef T6 ResT;
    typedef WorkPipeline<T0,T1,T2,T3,T4,T5,T6> SelfT;

    // The client should instantiate the WorkPipeline with instances
    // of WorkStage. The WorkPipeline accomodates a minimum of 1 and
    // maximum of 6 stages. Each stage has an Input and Output type.
    // The Output type of a stage must match the Input type of the 
    // next stage. This sequence of types must be used to instantiate
    // the WorkPipeline template. 
    WorkPipeline(
        WorkStageIf<T0,T1> * s0,
        WorkStageIf<T1,T2> * s1 = NULL,
        WorkStageIf<T2,T3> * s2 = NULL,
        WorkStageIf<T3,T4> * s3 = NULL,
        WorkStageIf<T4,T5> * s4 = NULL,
        WorkStageIf<T5,T6> * s5 = NULL);

    // The client should call this function to start the WorkPipeline
    // "finFn" is the callback function that WorkPipeline will call when
    // the last stage ends. "inp" is the Input to the 1st stage.
    // The WorkPipeline will retain a shared_ptr to the initial 
    // input for the lifetime of the WorkPipeline object. 
    void Start(FinFn finFn, const boost::shared_ptr<T0> & inp);

    // After "finFn" has been called by WorkPipeline, the client can
    // access the final result by calling this function
    // The WorkPipeline will retain a shared_ptr to the final result
    // for the lifetime of the WorkPipeline object.
    boost::shared_ptr<ResT> Result() const;

private:
    bool finished_;
    FinFn finFn_;
    boost::shared_ptr<T0> inp_;
    boost::shared_ptr<ResT> res_;
    boost::tuple<
        const boost::shared_ptr<WorkStageIf<T0,T1> >,
        const boost::shared_ptr<WorkStageIf<T1,T2> >,
        const boost::shared_ptr<WorkStageIf<T2,T3> >,
        const boost::shared_ptr<WorkStageIf<T3,T4> >,
        const boost::shared_ptr<WorkStageIf<T4,T5> >,
        const boost::shared_ptr<WorkStageIf<T5,T6> > > sg_;

    template<int kS, bool same> struct PipeProceed {};

    template<int kS> struct PipeProceed<kS, true> {
        static void Do(SelfT * wp) { wp->res_ = boost::get<kS>(wp->sg_)->Result(); }
    };

    template<int kS> struct PipeProceed<kS, false> {
        static void Do(SelfT * wp) { assert(boost::get<kS+1>(wp->sg_)); }
    };

    void WorkStageCb(uint32_t stage, bool ret_code);
    template<int kS,typename NextT> void NextStage();

};

#include "work_pipeline-inl.h"


#endif
