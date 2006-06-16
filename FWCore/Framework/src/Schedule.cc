#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/TriggerNamesService.h"
#include "FWCore/Framework/interface/TriggerReport.h"
#include "FWCore/Framework/src/ProducerWorker.h"
#include "FWCore/Framework/src/WorkerInPath.h"
#include "DataFormats/Common/interface/ModuleDescription.h"
#include "FWCore/Framework/interface/Schedule.h"
#include "FWCore/Framework/src/TriggerResultInserter.h"
#include "FWCore/ServiceRegistry/interface/ActivityRegistry.h"
#include "FWCore/ServiceRegistry/interface/ServiceRegistry.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/MessageLogger/interface/JobReport.h"

#include "FWCore/Framework/interface/UnscheduledHandler.h"

// needed for type tests
#include "FWCore/Framework/src/OutputWorker.h"
#include "FWCore/Framework/src/FilterWorker.h"

#include "boost/shared_ptr.hpp"

#include <string>
#include <memory>
#include <vector>
#include <iomanip>
#include <list>
#include <algorithm>

using namespace std;

namespace edm
{
  namespace
  {
    // Function template to transform each element in the input range to
    // a value placed into the output range. The supplied function
    // should take a const_reference to the 'input', and write to a
    // reference to the 'output'.
    template <class InputIterator, class ForwardIterator, class Func>
    void
    transform_into(InputIterator begin, InputIterator end,
		   ForwardIterator out, Func func)
    {
      for ( ; begin != end; ++begin, ++out ) func(*begin, *out);
    }
    
    // Function template that takes a sequence 'from', a sequence
    // 'to', and a callable object 'func'. It and applies
    // transform_into to fill the 'to' sequence with the values
    // calcuated by the callable object, taking care to fill the
    // outupt only if all calls succeed.
    template <class FROM, class TO, class FUNC>
    void
    fill_summary(FROM const& from, TO& to, FUNC func)
    {
      TO temp(from.size());
      transform_into(from.begin(), from.end(), temp.begin(), func);
      to.swap(temp);
    }

    // -----------------------------

    // Here we make the trigger results inserter directly.  This should
    // probably be a utility in the WorkerRegistry or elsewhere.

    Schedule::WorkerPtr makeInserter(const ParameterSet& trig_pset,
				     const string& proc_name,
				     ProductRegistry& preg,
				     ActionTable& actions,
				     Schedule::TrigResPtr trptr)
    {
#if 1
      WorkerParams work_args(trig_pset,preg,actions,proc_name);
      ModuleDescription md;
      md.pid = trig_pset.id();
      md.moduleName_ = "TriggerResultInserter";
      md.moduleLabel_ = "TriggerResults";
      md.processName_ = proc_name;
      md.versionNumber_ = 0; // not set properly!!!
      md.pass = 0; // not set properly!!!

      auto_ptr<EDProducer> producer(new TriggerResultInserter(trig_pset,trptr));

      Schedule::WorkerPtr ptr(new ProducerWorker(producer,md,work_args));
#else
      Schedule::WorkerPtr ptr;
#endif
      return ptr;
    }

    // -----------------------------

    class CallPrePost
    {
    public:
      CallPrePost(ActivityRegistry* a,
		  EventPrincipal* ep,
		  const EventSetup* es):
	a_(a),ep_(ep),es_(es)
      { a_->preProcessEventSignal_(ep_->id(),ep_->time()); }
      ~CallPrePost() {
	ModuleDescription dummy;
	Event evt(*ep_,dummy);
	const Event& eref(evt);
	a_->postProcessEventSignal_(eref,*es_);
      }

    private:
      ActivityRegistry* a_;
      EventPrincipal* ep_;
      const EventSetup* es_;
    };


  }

  class UnscheduledCallProducer : public UnscheduledHandler
  {
  public:
    UnscheduledCallProducer() {}
    void addWorker(Worker* aWorker) {
      assert(0 != aWorker);
      labelToWorkers_[aWorker->description().moduleLabel_]=aWorker;
    }
  private:
    virtual bool tryToFillImpl(const Provenance& prov,
			       EventPrincipal& event,
			       const EventSetup& eventSetup) {
      map<string, Worker*>::const_iterator itFound =
        labelToWorkers_.find(prov.product.module.moduleLabel_);
      if(itFound != labelToWorkers_.end()) {
	itFound->second->doWork(event,eventSetup);
	return true;
      }
      return false;
    }
    map<string, Worker*> labelToWorkers_;
  };

  // -----------------------------

  typedef vector<string> vstring;

  // -----------------------------

  Schedule::~Schedule() { }

  Schedule::Schedule(ParameterSet const& proc_pset,
		     edm::service::TriggerNamesService& tns,
		     WorkerRegistry& wreg,
		     ProductRegistry& preg,
		     ActionTable& actions,
		     ActivityRegistryPtr areg):
    pset_(proc_pset),
    worker_reg_(&wreg),
    prod_reg_(&preg),
    act_table_(&actions),
    proc_name_(tns.getProcessName()),
    trig_pset_(tns.getTrigPSet()),
    act_reg_(areg),
    state_(Ready),
    trig_name_list_(tns.getTrigPaths()),
    path_name_list_(tns.getPaths()),
    end_path_name_list_(tns.getEndPaths()),
    trig_name_set_(trig_name_list_.begin(),trig_name_list_.end()),

    results_        (new HLTGlobalStatus(trig_name_list_.size())),
    nontrig_results_(new HLTGlobalStatus(path_name_list_.size())),
    endpath_results_(), // delay!
    results_inserter_(),
    trig_paths_(),
    end_paths_(),
    wantSummary_(tns.wantSummary()),
    makeTriggerResults_(tns.makeTriggerResults()),
    total_events_(),
    total_passed_(),
    stopwatch_(new RunStopwatch::StopwatchPointer::element_type),
    unscheduled_(new UnscheduledCallProducer),
    demandGroups_(),
    endpathsAreActive_(true)
  {
    ParameterSet defopts;
    ParameterSet opts =
      pset_.getUntrackedParameter<ParameterSet>("options", defopts);

    bool hasFilter = false;

    vstring::iterator ib(path_name_list_.begin()),ie(path_name_list_.end());
    int trig_bitpos=0, non_bitpos=0;

    for(;ib!=ie;++ib) {
	if(trig_name_set_.find(*ib)!=trig_name_set_.end()) {
	  hasFilter += fillTrigPath(trig_bitpos,*ib, results_);
	  ++trig_bitpos;
	} else {
	    fillTrigPath(non_bitpos,*ib, nontrig_results_);
	    ++non_bitpos;
	}
    }

    // the results inserter stands alone
    if(hasFilter || makeTriggerResults_) {
	results_inserter_=makeInserter(trig_pset_,proc_name_,
				       preg,actions,results_);
	all_workers_.insert(results_inserter_.get());
    }

    // check whether an endpath for wrongly placed modules is needed
    if(tmp_wrongly_placed_.empty()) {
	TrigResPtr epptr(new HLTGlobalStatus(end_path_name_list_.size()));
	endpath_results_ = epptr;
    } else {
	TrigResPtr epptr(new HLTGlobalStatus(end_path_name_list_.size()+1));
	endpath_results_ = epptr;
    }

    // fill normal endpaths
    vstring::iterator eib(end_path_name_list_.begin()),eie(end_path_name_list_.end());
    for(int bitpos=0;eib!=eie;++eib,++bitpos) fillEndPath(bitpos,*eib);

    // handle additional endpath containing wrongly placed modules
    handleWronglyPlacedModules();

    //See if all modules were used
    set<string> usedWorkerLabels;
    for(AllWorkers::iterator itWorker=all_workers_.begin();
        itWorker != all_workers_.end();
        ++itWorker) {
	usedWorkerLabels.insert((*itWorker)->description().moduleLabel_);
    }
    vector<string> modulesInConfig(proc_pset.getParameter<vector<string> >("@all_modules"));
    set<string> modulesInConfigSet(modulesInConfig.begin(),modulesInConfig.end());
    vector<string> unusedLabels;
    set_difference(modulesInConfigSet.begin(),modulesInConfigSet.end(),
                        usedWorkerLabels.begin(),usedWorkerLabels.end(),
                        back_inserter(unusedLabels));
    //does the configuration say we should allow on demand?
    bool allowUnscheduled = opts.getUntrackedParameter<bool>("allowUnscheduled", false);
    set<string> unscheduledLabels;
    if(!unusedLabels.empty()) {
	//Need to
	// 1) create worker
	// 2) if they are ProducerWorkers, add them to our list
	// 3) hand list to our delayed reader
	vector<string>  shouldBeUsedLabels;
	
	for(vector<string>::iterator itLabel = unusedLabels.begin();
            itLabel != unusedLabels.end();
            ++itLabel) {
          if (allowUnscheduled) {
	      const unsigned long version=1, pass=1;
	      unscheduledLabels.insert(*itLabel);
	      //Need to hold onto the parameters long enough to make the call to getWorker
	      ParameterSet workersParams(proc_pset.getParameter<ParameterSet>(*itLabel));
	      WorkerParams params(workersParams,
				  *prod_reg_, *act_table_,
				  proc_name_, version, pass);
	      Worker* newWorker(wreg.getWorker(params));
	      if (dynamic_cast<ProducerWorker*>(newWorker)) {
		  unscheduled_->addWorker(newWorker);
		  //add to list so it gets reset each new event
		  all_workers_.insert(newWorker);
	      } else {
		  //not a producer so should be marked as not used
		  shouldBeUsedLabels.push_back(*itLabel);
	      }
	  } else {
	      //everthing is marked are unused so no 'on demand' allowed
	      shouldBeUsedLabels.push_back(*itLabel);
	  }
	}
	if(!shouldBeUsedLabels.empty()) {
	    ostringstream unusedStream;
	    unusedStream << "'"<< shouldBeUsedLabels.front() <<"'";
	    for(vector<string>::iterator itLabel = shouldBeUsedLabels.begin()+1;
		itLabel != shouldBeUsedLabels.end();
		++itLabel) {
	      unusedStream <<",'" << *itLabel<<"'";
	    }
	    LogWarning("path")
	      << "The following module labels are not assigned to any path:\n"
	      <<unusedStream.str()
	      <<"\n";
	}
    }
    prod_reg_->setProductIDs();
    //Now that these have been set, we can create the list of Groups we need for the 'on demand'
    const ProductRegistry::ProductList& prodsList = prod_reg_->productList();
    for(ProductRegistry::ProductList::const_iterator itProdInfo = prodsList.begin();
        itProdInfo != prodsList.end();
        ++itProdInfo)
      {
	if(unscheduledLabels.end() != unscheduledLabels.find(itProdInfo->second.module.moduleLabel_)) {
          auto_ptr<Provenance> prov(new Provenance(itProdInfo->second));
          boost::shared_ptr<Group> theGroup(new Group(prov));
          demandGroups_.push_back(theGroup);
	}
      }
  }

  void Schedule::handleWronglyPlacedModules()
  {
    // the wrongly placed workers (always output modules)
    // are already accounted for, but are not yet in paths.
    // Here we do that path assignment.

    if(!tmp_wrongly_placed_.empty()) {
      const string newname("WronglyPlaced");
      const unsigned int pos(end_path_name_list_.size());
      Path p(pos,newname,tmp_wrongly_placed_,
	     endpath_results_,pset_,*act_table_,act_reg_);
      end_paths_.push_back(p);
      end_path_name_list_.push_back(newname);
    }
  }


  void Schedule::fillWorkers(const string& name, PathWorkers& out)
  {
    vstring modnames = pset_.getParameter<vstring>(name);
    vstring::iterator it(modnames.begin()),ie(modnames.end());
    PathWorkers tmpworkers;

    for(;it!=ie;++it) {
      bool invert = (*it)[0]=='!';
      string realname = invert?string(it->begin()+1,it->end()):*it;
      WorkerInPath::State state =
	invert ? WorkerInPath::Veto : WorkerInPath::Normal;

      ParameterSet modpset;
      try {
        modpset= pset_.getParameter<ParameterSet>(realname);
      } catch( cms::Exception& ) {
        string pathType("endpath");
        if(find( end_path_name_list_.begin(),end_path_name_list_.end(), name) == end_path_name_list_.end()) {
          pathType = string("path");
        }
        throw edm::Exception(edm::errors::Configuration)<<"The unknown module label \""<<realname<<"\" appears in "<<pathType<<" \""<<name
        <<"\"\n please check spelling or remove that label from the path.";
      }
      unsigned long version=1, pass=1;
      WorkerParams params(modpset, *prod_reg_, *act_table_,
			  proc_name_, version, pass);
      WorkerInPath w(worker_reg_->getWorker(params),state);
      tmpworkers.push_back(w);
    }

    out.swap(tmpworkers);
  }

  struct ToWorker
  {
    Worker* operator()(WorkerInPath& w) const { return w.getWorker(); }
  };

  bool Schedule::fillTrigPath(int bitpos,const string& name, TrigResPtr trptr)
  {
    PathWorkers tmpworkers;
    PathWorkers goodworkers;
    Workers holder;
    fillWorkers(name,tmpworkers);
    bool hasFilter = false;

    // check for any OutputModules
    for(PathWorkers::iterator wi(tmpworkers.begin()),
	  we(tmpworkers.end());wi!=we;++wi) {
      Worker* tworker = wi->getWorker();
      if(dynamic_cast<OutputWorker*>(tworker)!=0) {
	LogWarning("path")
	  << "OutputModule "
	  << tworker->description().moduleLabel_
	  << " appears in path " << name << ".\n"
	  << "This will not be allowed in future releases.\n"
	  << "This module has been moved to the endpath.\n";

	tmp_wrongly_placed_.push_back(*wi);
      } else {
	goodworkers.push_back(*wi);
      }

      if(dynamic_cast<FilterWorker*>(tworker)!=0) {
	hasFilter = true;
      }

      holder.push_back(tworker);
    }

    // an empty path will cause an extra bit that is not used
    if(!goodworkers.empty()) {
      Path p(bitpos,name,goodworkers,trptr,pset_,*act_table_,act_reg_);
      trig_paths_.push_back(p);
    }
    all_workers_.insert(holder.begin(),holder.end());

    return hasFilter;
  }

  void Schedule::fillEndPath(int bitpos,const string& name)
  {
    PathWorkers tmpworkers;
    fillWorkers(name,tmpworkers);
    Workers holder;

    transform(tmpworkers.begin(),tmpworkers.end(),
	      back_inserter(holder),ToWorker());

    Path p(bitpos,name,tmpworkers,endpath_results_,pset_,*act_table_,act_reg_);
    end_paths_.push_back(p);
    all_workers_.insert(holder.begin(),holder.end());
  }

  void Schedule::runOneEvent(EventPrincipal& ep, EventSetup const& es)
  {
    ++total_events_;

    RunStopwatch stopwatch(stopwatch_);
    resetWorkers();
    results_->reset();
    endpath_results_->reset();
    state_ = Running;

    //now setup the on-demand system
    // NOTE: who owns the productdescrption?  Just copied by value
    unscheduled_->setEventSetup(es);
    ep.setUnscheduledHandler(unscheduled_);
    for(vector<boost::shared_ptr<Group> >::iterator itGroup = demandGroups_.begin();
        itGroup != demandGroups_.end();
        ++itGroup) {
      auto_ptr<Provenance> prov(new Provenance((*itGroup)->productDescription()));
      auto_ptr<Group> theGroup(new Group(prov));
      ep.addGroup(theGroup);
    }
    try {
	CallPrePost cpp(act_reg_.get(),&ep,&es);

	// go through normal paths and check only trigger paths for accept
	bool result = false;
	int which_one = 0;
	TrigPaths::iterator ti(trig_paths_.begin()),te(trig_paths_.end());
	for(;ti!=te;++ti) {
	    ti->runOneEvent(ep,es);
	    if (trig_name_set_.find(ti->name())!=trig_name_set_.end()) {
		result = result || ((*results_)[which_one]).accept();
		++which_one;
	    }
	}
	
	if(result) ++total_passed_;
	state_ = Latched;
	
	if(results_inserter_.get()) results_inserter_->doWork(ep,es);
	
	// go through end paths next.  Note there is no state-checking
	// safety controlling the activation/deactivation of endpaths.
	if (endpathsAreActive_) {
	    TrigPaths::iterator ei(end_paths_.begin()),ee(end_paths_.end());
	    for( ; ei != ee; ++ei) { ei->runOneEvent(ep,es); }
	}
    }
    catch(cms::Exception& e) {
	actions::ActionCodes code = act_table_->find(e.rootCause());

	switch(code) {
	  case actions::IgnoreCompletely: {
	      LogWarning(e.category())
		<< "exception being ignored for current event:\n"
		<< e.what();
	      break;
	  }
	  case actions::SkipEvent: {
	      LogWarning(e.category())
		<< "an exception occurred and event is being skipped: \n"
		<< e.what();
	      Service<JobReport> reportSvc;
	      reportSvc->reportSkippedEvent(ep.id());
	      break;
	  }
	  default: {
	      state_ = Ready;
	      throw edm::Exception(errors::EventProcessorFailure,
				   "EventProcessingStopped",e)
		<< "an exception ocurred during current event processing\n";
	  }
	}
    }
    catch(...) {
	LogError("PassingThrough")
	  << "an exception ocurred during current event processing\n";
	state_ = Ready;
	throw;
      }

    // next thing probably is not needed, the product insertion code clears it
    state_ = Ready;

  }

  void Schedule::endJob()
  {
    bool failure = false;
    cms::Exception accumulated("endJob");
    AllWorkers::iterator ai(all_workers_.begin()),ae(all_workers_.end());
    for(;ai!=ae;++ai) {
      try {
	(*ai)->endJob();
      }
      catch (seal::Error& e) {
        accumulated << "seal::Exception caught in Schedule::endJob\n"
		  << e.explainSelf();
        failure = true;
      }
      catch (cms::Exception& e) {
        accumulated << "cms::Exception caught in Schedule::endJob\n"
		  << e.explainSelf();
        failure = true;
      }
      catch (exception& e) {
        accumulated << "Standard library exception caught in Schedule::endJob\n"
		  << e.what();
        failure = true;
      }
      catch (...) {
        accumulated << "Unknown exception caught in Schedule::endJob\n";
        failure = true;
      }
    }
    if (failure) {
      throw accumulated;
    }


    if(wantSummary_ == false) return;

    TrigPaths::const_iterator pi,pe;

    // The trigger report (pass/fail etc.):

    cout << endl;
    cout << "TrigReport " << "---------- Event  Summary ------------\n";
    cout << "TrigReport"
	 << " Events total = " << totalEvents()
	 << " passed = " << totalEventsPassed()
	 << " failed = " << (totalEventsFailed())
	 << "\n";

    cout << endl;
    cout << "TrigReport " << "---------- Path   Summary ------------\n";
    cout << "TrigReport "
	 << right << setw(10) << "Trig Bit#" << " "
	 << right << setw(10) << "Run" << " "
	 << right << setw(10) << "Passed" << " "
	 << right << setw(10) << "Failed" << " "
	 << right << setw(10) << "Error" << " "
	 << "Name" << "\n";
    pi=trig_paths_.begin();
    pe=trig_paths_.end();
    for(;pi!=pe;++pi) {
	cout << "TrigReport "
             << right << setw( 5) << (trig_name_set_.find(pi->name())!=trig_name_set_.end())
             << right << setw( 5) << pi->bitPosition() << " "
	     << right << setw(10) << pi->timesRun() << " "
	     << right << setw(10) << pi->timesPassed() << " "
	     << right << setw(10) << pi->timesFailed() << " "
	     << right << setw(10) << pi->timesExcept() << " "
	     << pi->name() << "\n";
    }

    cout << endl;
    cout << "TrigReport " << "-------End-Path   Summary ------------\n";
    cout << "TrigReport "
	 << right << setw(10) << "Trig Bit#" << " "
	 << right << setw(10) << "Run" << " "
	 << right << setw(10) << "Passed" << " "
	 << right << setw(10) << "Failed" << " "
	 << right << setw(10) << "Error" << " "
	 << "Name" << "\n";
    pi=end_paths_.begin();
    pe=end_paths_.end();
    for(;pi!=pe;++pi) {
	cout << "TrigReport "
             << right << setw( 5) << (trig_name_set_.find(pi->name())!=trig_name_set_.end())
             << right << setw( 5) << pi->bitPosition() << " "
	     << right << setw(10) << pi->timesRun() << " "
	     << right << setw(10) << pi->timesPassed() << " "
	     << right << setw(10) << pi->timesFailed() << " "
	     << right << setw(10) << pi->timesExcept() << " "
	     << pi->name() << "\n";
    }

    pi=trig_paths_.begin();
    pe=trig_paths_.end();
    for(;pi!=pe;++pi) {
	cout << endl;
	cout << "TrigReport " << "---------- Modules in Path: " << pi->name() << " ------------\n";
	cout << "TrigReport "
	     << right << setw(10) << "Trig Bit#" << " "
	     << right << setw(10) << "Visited" << " "
	     << right << setw(10) << "Passed" << " "
	     << right << setw(10) << "Failed" << " "
	     << right << setw(10) << "Error" << " "
	     << "Name" << "\n";

	for (unsigned int i=0; i<pi->size(); ++i) {
	    cout << "TrigReport "
		 << right << setw( 5) << (trig_name_set_.find(pi->name())!=trig_name_set_.end())
		 << right << setw( 5) << pi->bitPosition() << " "
		 << right << setw(10) << pi->timesVisited(i) << " "
		 << right << setw(10) << pi->timesPassed(i) << " "
		 << right << setw(10) << pi->timesFailed(i) << " "
		 << right << setw(10) << pi->timesExcept(i) << " "
		 << pi->getWorker(i)->description().moduleLabel_ << "\n";
	}
    }

    pi=end_paths_.begin();
    pe=end_paths_.end();
    for(;pi!=pe;++pi) {
	cout << endl;
	cout << "TrigReport " << "------ Modules in End-Path: " << pi->name() << " ------------\n";
	cout << "TrigReport "
	     << right << setw(10) << "Trig Bit#" << " "
	     << right << setw(10) << "Visited" << " "
	     << right << setw(10) << "Passed" << " "
	     << right << setw(10) << "Failed" << " "
	     << right << setw(10) << "Error" << " "
	     << "Name" << "\n";

	for (unsigned int i=0; i<pi->size(); ++i) {
	    cout << "TrigReport "
		 << right << setw( 5) << (trig_name_set_.find(pi->name())!=trig_name_set_.end())
		 << right << setw( 5) << pi->bitPosition() << " "
		 << right << setw(10) << pi->timesVisited(i) << " "
		 << right << setw(10) << pi->timesPassed(i) << " "
		 << right << setw(10) << pi->timesFailed(i) << " "
		 << right << setw(10) << pi->timesExcept(i) << " "
		 << pi->getWorker(i)->description().moduleLabel_ << "\n";
	}
    }

    cout << endl;
    cout << "TrigReport " << "---------- Module Summary ------------\n";
    cout << "TrigReport "
	 << right << setw(10) << "Visited" << " "
	 << right << setw(10) << "Run" << " "
	 << right << setw(10) << "Passed" << " "
	 << right << setw(10) << "Failed" << " "
	 << right << setw(10) << "Error" << " "
	 << "Name" << "\n";
    ai=all_workers_.begin();
    ae=all_workers_.end();
    for(;ai!=ae;++ai) {
	cout << "TrigReport "
	     << right << setw(10) << (*ai)->timesVisited() << " "
	     << right << setw(10) << (*ai)->timesRun() << " "
	     << right << setw(10) << (*ai)->timesPassed() << " "
	     << right << setw(10) << (*ai)->timesFailed() << " "
	     << right << setw(10) << (*ai)->timesExcept() << " "
	     << (*ai)->description().moduleLabel_ << "\n";

    }
    cout << endl;

    // The timing report (CPU and Real Time):

    cout << setprecision(6) << fixed << endl;
    cout << "TimeReport " << "---------- Event  Summary ---[sec]----\n";
    cout << "TimeReport"
	 << " CPU/event = " << timeCpuReal().first/max(1,totalEvents())
	 << " Real/event = " << timeCpuReal().second/max(1,totalEvents())
	 << "\n";

    cout << endl;
    cout << "TimeReport " << "---------- Path   Summary ---[sec]----\n";
    cout << "TimeReport "
	 << right << setw(10) << "CPU/event" << " "
	 << right << setw(10) << "Real/event" << " "
	 << "Name" << "\n";
    pi=trig_paths_.begin();
    pe=trig_paths_.end();
    for(;pi!=pe;++pi) {
	cout << "TimeReport "
	     << right << setw(10) << pi->timeCpuReal().first/max(1,pi->timesRun()) << " "
	     << right << setw(10) << pi->timeCpuReal().second/max(1,pi->timesRun()) << " "
	     << pi->name() << "\n";
    }

    cout << endl;
    cout << "TimeReport " << "-------End-Path   Summary ---[sec]----\n";
    cout << "TimeReport "
	 << right << setw(10) << "CPU/event" << " "
	 << right << setw(10) << "Real/event" << " "
	 << "Name" << "\n";
    pi=end_paths_.begin();
    pe=end_paths_.end();
    for(;pi!=pe;++pi) {
	cout << "TimeReport "
	     << right << setw(10) << pi->timeCpuReal().first/max(1,pi->timesRun()) << " "
	     << right << setw(10) << pi->timeCpuReal().second/max(1,pi->timesRun()) << " "
	     << pi->name() << "\n";
    }

    pi=trig_paths_.begin();
    pe=trig_paths_.end();
    for(;pi!=pe;++pi) {
	cout << endl;
	cout << "TimeReport " << "---------- Modules in Path: " << pi->name() << " ---[sec]----\n";
	cout << "TimeReport "
	     << right << setw(10) << "CPU/event" << " "
	     << right << setw(10) << "Real/event" << " "
	     << "Name" << "\n";
	for (unsigned int i=0; i<pi->size(); ++i) {
	    cout << "TimeReport "
		 << right << setw(10) << pi->timeCpuReal(i).first/max(1,pi->timesVisited(i)) << " "
		 << right << setw(10) << pi->timeCpuReal(i).second/max(1,pi->timesVisited(i)) << " "
		 << pi->getWorker(i)->description().moduleLabel_ << "\n";
	}
    }

    pi=end_paths_.begin();
    pe=end_paths_.end();
    for(;pi!=pe;++pi) {
	cout << endl;
	cout << "TimeReport " << "------ Modules in End-Path: " << pi->name() << " ---[sec]----\n";
	cout << "TimeReport "
	     << right << setw(10) << "CPU/event" << " "
	     << right << setw(10) << "Real/event" << " "
	     << "Name" << "\n";
	for (unsigned int i=0; i<pi->size(); ++i) {
	    cout << "TimeReport "
		 << right << setw(10) << pi->timeCpuReal(i).first/max(1,pi->timesVisited(i)) << " "
		 << right << setw(10) << pi->timeCpuReal(i).second/max(1,pi->timesVisited(i)) << " "
		 << pi->getWorker(i)->description().moduleLabel_ << "\n";
	}
    }

    cout << endl;
    cout << "TimeReport " << "---------- Module Summary ---[sec]----\n";
    cout << "TimeReport "
	 << right << setw(10) << "CPU" << " "
	 << right << setw(10) << "Real" << " "
	 << right << setw(10) << "CPU" << " "
	 << right << setw(10) << "Real" << " "
	 << "Name" << "\n";
    cout << "TimeReport "
	 << right << setw(22) << "per visited event "
	 << right << setw(22) << "per run event " << "\n";
    ai=all_workers_.begin();
    ae=all_workers_.end();
    for(;ai!=ae;++ai) {
	cout << "TimeReport "
	     << right << setw(10) << (*ai)->timeCpuReal().first/max(1,(*ai)->timesVisited()) << " "
	     << right << setw(10) << (*ai)->timeCpuReal().second/max(1,(*ai)->timesVisited()) << " "
	     << right << setw(10) << (*ai)->timeCpuReal().first/max(1,(*ai)->timesRun()) << " "
	     << right << setw(10) << (*ai)->timeCpuReal().second/max(1,(*ai)->timesRun()) << " "
	     << (*ai)->description().moduleLabel_ << "\n";
    }
    cout << endl;
    cout << "T---Report end!" << endl;
    cout << endl;

  }

  void Schedule::beginJob(EventSetup const& es)
  {
    AllWorkers::iterator i(all_workers_.begin()),e(all_workers_.end());
    for(;i!=e;++i) { (*i)->beginJob(es); }

  }


  vector<ModuleDescription const*>
  Schedule::getAllModuleDescriptions() const
  {
    AllWorkers::const_iterator i(all_workers_.begin());
    AllWorkers::const_iterator e(all_workers_.end());

    vector<ModuleDescription const*> result;
    result.reserve(all_workers_.size());

    for ( ; i!=e; ++i) {
	ModuleDescription const* p = (*i)->descPtr();
	result.push_back( p );
    }
    return result;
  }

  void
  Schedule::enableEndPaths(bool active)
  {
    endpathsAreActive_ = active;
  }

  bool
  Schedule::endPathsEnabled() const
  {
    return endpathsAreActive_;
  }

  void
  fillModuleInPathSummary(Path const& path, 
			  ModuleInPathSummary& sum)
  {
  }


  void
  fillModuleInPathSummary(Path const& path, 
			  size_t which, 
			  ModuleInPathSummary& sum)
  {
	sum.timesVisited = path.timesVisited(which);
	sum.timesPassed  = path.timesPassed(which);
	sum.timesFailed  = path.timesFailed(which);
	sum.timesExcept  = path.timesExcept(which);
	sum.moduleLabel  = 
	  path.getWorker(which)->description().moduleLabel_;
  }

  void 
  fillPathSummary(Path const& path, PathSummary& sum)
  {
    sum.name        = path.name();
    sum.bitPosition = path.bitPosition();
    sum.timesRun    = path.timesRun();
    sum.timesPassed = path.timesPassed();
    sum.timesFailed = path.timesFailed();
    sum.timesExcept = path.timesExcept();

    Path::size_type sz = path.size();
    vector<ModuleInPathSummary> temp(sz);
    for (size_t i = 0; i != sz; ++i)
      {
	fillModuleInPathSummary(path, i, temp[i]);
      }
    sum.moduleInPathSummaries.swap(temp);
  }

  void 
  fillWorkerSummaryAux(Worker const& w, WorkerSummary& sum)
  {
    sum.timesVisited = w.timesVisited();
    sum.timesRun     = w.timesRun();
    sum.timesPassed  = w.timesPassed();
    sum.timesFailed  = w.timesFailed();
    sum.timesExcept  = w.timesExcept();
    sum.moduleLabel  = w.description().moduleLabel_;
  }

  void
  fillWorkerSummary(Worker const* pw, WorkerSummary& sum)
  {
    fillWorkerSummaryAux(*pw, sum);
  }
  
  void
  Schedule::getTriggerReport(TriggerReport& rep) const
  {
    rep.eventSummary.totalEvents = totalEvents();
    rep.eventSummary.totalEventsPassed = totalEventsPassed();
    rep.eventSummary.totalEventsFailed = totalEventsFailed();

    fill_summary(trig_paths_,  rep.trigPathSummaries, &fillPathSummary);
    fill_summary(end_paths_,   rep.endPathSummaries,  &fillPathSummary);
    fill_summary(all_workers_, rep.workerSummaries,   &fillWorkerSummary);
  }

  void Schedule::resetWorkers()
  {
    AllWorkers::iterator i(all_workers_.begin()),e(all_workers_.end());
    for(;i!=e;++i) { (*i)->reset(); }
  }
}
