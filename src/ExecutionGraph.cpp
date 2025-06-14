/*
 * GenMC -- Generic Model Checking.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-3.0.html.
 *
 * Author: Michalis Kokologiannakis <michalis@mpi-sws.org>
 */

#include "config.h"
#include "ExecutionGraph.hpp"
#include "GraphIterators.hpp"
#include "LBCalculatorLAPOR.hpp"
#include "MOCalculator.hpp"
#include "Parser.hpp"
#include "WBCalculator.hpp"
#include "PersistencyChecker.hpp"
#include <llvm/IR/DebugInfo.h>

/************************************************************
 ** Class constructors/destructors
 ***********************************************************/

ExecutionGraph::ExecutionGraph(unsigned maxSize /* UINT_MAX */)
	: timestamp(1), persChecker(nullptr), relations(), relsCache(), warnOnGraphSize(maxSize)
{
	/* Create an entry for main() and push the "initializer" label */
	events.push_back({});
	addOtherLabelToGraph( std::unique_ptr<ThreadStartLabel>(
				     new ThreadStartLabel(
					     0, llvm::AtomicOrdering::Acquire,
					     Event(0, 0),
					     Event::getInitializer() )
				      ) );

	relations.global.push_back(Calculator::GlobalRelation());
	relsCache.global.push_back(Calculator::GlobalRelation());
	relationIndex[RelationId::hb] = 0;
	calculatorIndex[RelationId::hb] = -42; /* no calculator for hb */
	return;
}

ExecutionGraph::~ExecutionGraph() = default;

/************************************************************
 ** Basic getter methods
 ***********************************************************/

const EventLabel *ExecutionGraph::getPreviousNonEmptyLabel(Event e) const
{
	for (int i = e.index - 1; i > 0; i--) {
		const EventLabel *eLab = getEventLabel(Event(e.thread, i));
		if (!llvm::isa<EmptyLabel>(eLab))
			return eLab;
	}
	return getEventLabel(Event(e.thread, 0));
}

Event ExecutionGraph::getPreviousNonTrivial(const Event e) const
{
	for (auto i = e.index - 1; i >= 0; i--) {
		if (isNonTrivial(Event(e.thread, i)))
			return Event(e.thread, i);
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getLastThreadStoreAtLoc(Event upperLimit, SAddr addr) const
{
	for (auto j = upperLimit.index - 1; j > 0; j--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, j));
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(lab)) {
			if (wLab->getAddr() == addr)
				return wLab->getPos();
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getLastThreadReleaseAtLoc(Event upperLimit, SAddr addr) const
{
	for (int i = upperLimit.index - 1; i > 0; i--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, i));
		if (llvm::isa<ThreadCreateLabel>(lab) || llvm::isa<ThreadFinishLabel>(lab) ||
		    llvm::isa<UnlockLabelLAPOR>(lab)) {
			return Event(upperLimit.thread, i);
		}
		if (auto *fLab = llvm::dyn_cast<FenceLabel>(lab)) {
			if (fLab->isAtLeastRelease())
				return Event(upperLimit.thread, i);
		}
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(lab)) {
			if (wLab->isAtLeastRelease() && wLab->getAddr() == addr)
				return Event(upperLimit.thread, i);
		}
	}
	return Event(upperLimit.thread, 0);
}

Event ExecutionGraph::getLastThreadRelease(Event upperLimit) const
{
	for (int i = upperLimit.index - 1; i > 0; i--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, i));
		if (llvm::isa<ThreadCreateLabel>(lab) || llvm::isa<ThreadFinishLabel>(lab) ||
		    llvm::isa<UnlockLabelLAPOR>(lab)) {
			return Event(upperLimit.thread, i);
		}
		if (auto *fLab = llvm::dyn_cast<FenceLabel>(lab)) {
			if (fLab->isAtLeastRelease())
				return Event(upperLimit.thread, i);
		}
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(lab)) {
			if (wLab->isAtLeastRelease())
				return Event(upperLimit.thread, i);
		}
	}
	return Event(upperLimit.thread, 0);
}

/* Assumes that all events prior to upperLimit have already been added */
std::vector<Event> ExecutionGraph::getThreadAcquiresAndFences(Event upperLimit) const
{
	std::vector<Event> result;

	result.push_back(Event(upperLimit.thread, 0));
	for (int i = 1u; i < upperLimit.index; i++) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, i));
		if (llvm::isa<FenceLabel>(lab) || lab->isAtLeastAcquire())
			result.push_back(lab->getPos());
	}
	return result;
}

Event ExecutionGraph::getMatchingLock(const Event unlock) const
{
	std::vector<Event> locUnlocks;

	auto *uLab = llvm::dyn_cast<UnlockWriteLabel>(getEventLabel(unlock));
	BUG_ON(!uLab);

	for (auto j = unlock.index - 1; j > 0; j--) {
		const EventLabel *lab = getEventLabel(Event(unlock.thread, j));

		/* In case support for reentrant locks is added... */
		if (auto *suLab = llvm::dyn_cast<UnlockWriteLabel>(lab)) {
			if (suLab->getAddr() == uLab->getAddr())
				locUnlocks.push_back(suLab->getPos());
		}
		if (auto *lLab = llvm::dyn_cast<CasWriteLabel>(lab)) {
			if ((llvm::isa<LockCasWriteLabel>(lLab) || llvm::isa<TrylockCasWriteLabel>(lLab)) &&
			    lLab->getAddr() == uLab->getAddr()) {
				if (locUnlocks.empty())
					return lLab->getPos().prev();
				locUnlocks.pop_back();

			}
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getMatchingUnlock(const Event lock) const
{
	std::vector<Event> locLocks;

	auto *lLab = llvm::dyn_cast<CasReadLabel>(getEventLabel(lock));
	BUG_ON(!lLab || (!llvm::isa<LockCasReadLabel>(lLab) && !llvm::isa<TrylockCasReadLabel>(lLab)));

	for (auto j = lock.index + 2; j < getThreadSize(lock.thread); j++) { /* skip next event */
		const EventLabel *lab = getEventLabel(Event(lock.thread, j));

		/* In case support for reentrant locks is added... */
		if (auto *slLab = llvm::dyn_cast<CasWriteLabel>(lab)) {
			if ((llvm::isa<LockCasWriteLabel>(slLab) || llvm::isa<TrylockCasWriteLabel>(slLab)) &&
			    slLab->getAddr() == lLab->getAddr())
				locLocks.push_back(slLab->getPos().prev());
		}
		if (auto *uLab = llvm::dyn_cast<UnlockWriteLabel>(lab)) {
			if (uLab->getAddr() == lLab->getAddr()) {
				if (locLocks.empty())
					return uLab->getPos();
				locLocks.pop_back();
			}
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getMatchingRCUUnlockLKMM(Event lock) const
{
	std::vector<Event> locks;

	BUG_ON(!llvm::isa<RCULockLabelLKMM>(getEventLabel(lock)));
	for (auto j = lock.index + 1; j < getThreadSize(lock.thread); j++) {
		const EventLabel *lab = getEventLabel(Event(lock.thread, j));

		if (auto *lLab = llvm::dyn_cast<RCULockLabelLKMM>(lab))
			locks.push_back(lLab->getPos());

		if (auto *uLab = llvm::dyn_cast<RCUUnlockLabelLKMM>(lab)) {
			if (locks.empty())
				return uLab->getPos();
			locks.pop_back();
		}
	}
	return getLastThreadEvent(lock.thread).next();
}

Event ExecutionGraph::getMatchingSpeculativeRead(Event conf, Event *sc /* = nullptr */) const
{
	auto *cLab = llvm::dyn_cast<ReadLabel>(getEventLabel(conf));
	BUG_ON(!cLab);

	for (auto j = conf.index - 1; j > 0; j--) {
		const EventLabel *lab = getEventLabel(Event(conf.thread, j));

		if (sc && lab->isSC())
			*sc = lab->getPos();

		/* We don't care whether all previous confirmations are matched;
		 * the same speculation maybe confirmed multiple times (e.g., baskets) */
		if (auto *rLab = llvm::dyn_cast<SpeculativeReadLabel>(lab)) {
			if (rLab->getAddr() == cLab->getAddr())
				return rLab->getPos();
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getLastThreadUnmatchedLockLAPOR(const Event upperLimit) const
{
	std::vector<SAddr > unlocks;

	for (auto j = upperLimit.index; j >= 0; j--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, j));

		if (auto *lLab = llvm::dyn_cast<LockLabelLAPOR>(lab)) {
			if (std::find_if(unlocks.rbegin(), unlocks.rend(),
					 [&](SAddr addr){ return lLab->getLockAddr() == addr; })
			    ==  unlocks.rend())
				return lLab->getPos();
		}

		if (auto *uLab = llvm::dyn_cast<UnlockLabelLAPOR>(lab))
			unlocks.push_back(uLab->getLockAddr());
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getMatchingUnlockLAPOR(const Event lock) const
{
	std::vector<Event> locLocks;

	const EventLabel *lockL = getEventLabel(lock);
	BUG_ON(!llvm::isa<LockLabelLAPOR>(lockL));
	auto *lLab = static_cast<const LockLabelLAPOR *>(lockL);

	for (auto j = lock.index + 1; j < getThreadSize(lock.thread); j++) {
		const EventLabel *lab = getEventLabel(Event(lock.thread, j));

		if (auto *slLab = llvm::dyn_cast<LockLabelLAPOR>(lab)) {
			if (slLab->getLockAddr() == lLab->getLockAddr())
				locLocks.push_back(slLab->getPos());
		}
		if (auto *uLab = llvm::dyn_cast<UnlockLabelLAPOR>(lab)) {
			if (uLab->getLockAddr() == lLab->getLockAddr()) {
				if (locLocks.empty())
					return uLab->getPos();
				else
					locLocks.pop_back();
			}
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getLastThreadLockAtLocLAPOR(const Event upperLimit, SAddr loc) const
{
	for (auto j = upperLimit.index; j >= 0; j--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, j));

		if (auto *lLab = llvm::dyn_cast<LockLabelLAPOR>(lab)) {
			if (lLab->getLockAddr() == loc)
				return lLab->getPos();
		}

	}
	return Event::getInitializer();
}

Event ExecutionGraph::getLastThreadUnlockAtLocLAPOR(const Event upperLimit, SAddr loc) const
{
	for (auto j = upperLimit.index; j >= 0; j--) {
		const EventLabel *lab = getEventLabel(Event(upperLimit.thread, j));

		if (auto *lLab = llvm::dyn_cast<UnlockLabelLAPOR>(lab)) {
			if (lLab->getLockAddr() == loc)
				return lLab->getPos();
		}

	}
	return Event::getInitializer();
}

Event ExecutionGraph::getMalloc(const SAddr &addr) const
{
	auto it = std::find_if(label_begin(*this), label_end(*this), [&](const EventLabel *lab){
				       if (auto *aLab = llvm::dyn_cast<MallocLabel>(lab))
					       return aLab->contains(addr);
				       return false;
				});
	return it != label_end(*this) ? it->getPos() : Event::getInitializer();
}

Event ExecutionGraph::getMallocCounterpart(const FreeLabel *fLab) const
{
	const auto &before = getEventLabel(fLab->getPos().prev())->getHbView();
	for (auto i = 0u; i < getNumThreads(); i++) {
		for (auto j = 0u; j < getThreadSize(i); j++) {
			const EventLabel *oLab = getEventLabel(Event(i, j));
			if (auto *aLab = llvm::dyn_cast<MallocLabel>(oLab)) {
				if (aLab->getAllocAddr() == fLab->getFreedAddr())
					return aLab->getPos();
			}
		}
	}
	return Event::getInitializer();
}

Event ExecutionGraph::getMinimumStampEvent(const std::vector<Event> &es) const
{
	if (es.empty())
		return Event::getInitializer();
	return *std::min_element(es.begin(), es.end(), [&](const Event &e1, const Event &e2){
		return getEventLabel(e1)->getStamp() < getEventLabel(e2)->getStamp();
	});
}

Event ExecutionGraph::getPendingRMW(const WriteLabel *sLab) const
{
	/* If this is _not_ an RMW event, return an empty vector */
	if (!isRMWStore(sLab))
		return Event::getInitializer();

	/* Otherwise, scan for other RMWs that successfully read the same val */
	auto *pLab = llvm::dyn_cast<ReadLabel>(getPreviousLabel(sLab));
	std::vector<Event> pending;

	/* Fastpath: non-init rf */
	if (!pLab->getRf().isInitializer()) {
		auto *wLab = llvm::dyn_cast<WriteLabel>(getEventLabel(pLab->getRf()));
		BUG_ON(!wLab);
		std::for_each(wLab->readers_begin(), wLab->readers_end(), [&](const Event &r){
				if (isRMWLoad(r) && r != pLab->getPos())
					pending.push_back(r);
			});
		return getMinimumStampEvent(pending);
	}

	/* Slowpath: scan the graph */
	std::for_each(label_begin(*this), label_end(*this), [&](const EventLabel *lab){
			     auto *rLab = llvm::dyn_cast<ReadLabel>(lab);
			     if (rLab && rLab->getRf() == pLab->getRf() &&
				 rLab->getAddr() == pLab->getAddr() &&
				 rLab->getPos() != pLab->getPos() && isRMWLoad(rLab))
				     pending.push_back(rLab->getPos());
		     });
	return getMinimumStampEvent(pending);
}

std::vector<Event> ExecutionGraph::getRevisitable(const WriteLabel *sLab) const
{
	auto &before = getPorfBefore(sLab->getPos());
	auto pendingRMW = getPendingRMW(sLab);
	std::vector<Event> loads;

	for (auto i = 0u; i < getNumThreads(); i++) {
		for (auto j = before[i] + 1u; j < getThreadSize(i); j++) {
			const EventLabel *lab = getEventLabel(Event(i, j));
			if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab)) {
				if (rLab->getAddr() == sLab->getAddr() &&
				    rLab->isRevisitable() && rLab->wasAddedMax())
					loads.push_back(rLab->getPos());
			}
		}
	}
	if (!pendingRMW.isInitializer())
		loads.erase(std::remove_if(loads.begin(), loads.end(), [&](Event &e){
			auto *confLab = getEventLabel(pendingRMW);
			return getEventLabel(e)->getStamp() > confLab->getStamp();
		}), loads.end());
	return loads;
}

/* Returns a vector with all reads of a particular location reading from INIT */
std::vector<Event> ExecutionGraph::getInitRfsAtLoc(SAddr addr) const
{
	std::vector<Event> result;

	for (const auto *lab : labels(*this)) {
		if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab))
			if (rLab->getRf().isInitializer() && rLab->getAddr() == addr)
				result.push_back(rLab->getPos());
	}
	return result;
}


/*******************************************************************************
 **                       Label addition methods
 ******************************************************************************/

const ReadLabel *ExecutionGraph::addReadLabelToGraph(std::unique_ptr<ReadLabel> lab,
						     Event rf /* = BOTTOM */)
{
	if (!lab->getRf().isBottom()) {
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(getEventLabel(lab->getRf()))) {
			llvm::outs() << "rLab: " << lab->method << "|wLab: " << wLab->method << "\n";
			wLab->addReader(lab->getPos());
		}
	}

	return static_cast<const ReadLabel *>(addOtherLabelToGraph(std::move(lab)));
}

const WriteLabel *ExecutionGraph::addWriteLabelToGraph(std::unique_ptr<WriteLabel> lab,
						       int offsetMO /* = -1 */)
{
	auto *wLab = static_cast<const WriteLabel *>(addOtherLabelToGraph(std::move(lab)));
	if (offsetMO >= 0)
		getCoherenceCalculator()->addStoreToLoc(wLab->getAddr(), wLab->getPos(), offsetMO);
	return wLab;
}

const WriteLabel *ExecutionGraph::addWriteLabelToGraph(std::unique_ptr<WriteLabel> lab,
						       Event pred)
{
	auto *wLab = static_cast<const WriteLabel *>(addOtherLabelToGraph(std::move(lab)));
	getCoherenceCalculator()->addStoreToLocAfter(wLab->getAddr(), wLab->getPos(), pred);
	return wLab;
}

const LockLabelLAPOR *ExecutionGraph::addLockLabelToGraphLAPOR(std::unique_ptr<LockLabelLAPOR> lab)
{
	getLbCalculatorLAPOR()->addLockToList(lab->getLockAddr(), lab->getPos());
	return static_cast<const LockLabelLAPOR *>(addOtherLabelToGraph(std::move(lab)));
}

const EventLabel *ExecutionGraph::addOtherLabelToGraph(std::unique_ptr<EventLabel> lab)
{
	setFPStatus(FS_Stale);

	/* Ensure the stamp is valid */
	if (lab->getStamp() == 0 && !lab->getPos().isInitializer())
		lab->setStamp(nextStamp());

	auto pos = lab->getPos();

	if (pos.index < events[pos.thread].size()) {
		events[pos.thread][pos.index] = std::move(lab);
	} else {
		events[pos.thread].push_back(std::move(lab));
	}
	BUG_ON(pos.index > events[pos.thread].size());
	WARN_ON_ONCE(pos.index > warnOnGraphSize, "large-graph",
		     "Graph too large! Are all loops bounded?\n");
	return getEventLabel(pos);
}

/* --Add-- */
FunctionName ExecutionGraph::previousEventMethod(Event e) 
{
	// Event (0,0) never passes through here so also set correctly for thread 0.
	if(e.index < 1 || (e.thread == 0 && e.index < 2)) {
		return FunctionName::FN_NON;
	}
	auto *prev = getEventLabel(Event(e.thread, e.index-1));
	if(prev->getKind() == EventLabel::EL_FunEnd) {
		return FunctionName::FN_NON;
	}
	return prev->getMethod();
}

unsigned int ExecutionGraph::previousEventInvocation(Event e) 
{
	// Event (0,0) never passes through here so also set correctly for thread 0.
	if(e.index < 1 || (e.thread == 0 && e.index < 2)) {
		return 0;
	}
	auto *prev = getEventLabel(Event(e.thread, e.index-1));
	return prev->getInvocation();
}

unsigned int ExecutionGraph::getFunInvocationCount(FunctionName funName)
{
	int count = 0;
	for(int i = 0; i < getNumThreads(); i++) {
		for(int j = getThreadSize(i)-1; j > 0; j--) {
			const EventLabel *eLab = getEventLabel(Event(i, j));
			if (eLab->getKind() == EventLabel::EL_FunStart && funName == eLab->getMethod()) {
				if(eLab->getInvocation() >= count) {
					count = eLab->getInvocation()+1;
				}
			}
		}
	}
	return count;
}

// finds the latest added eventlabel from a given function (and invocation).
const EventLabel *ExecutionGraph::getLastFunEvent(FunctionName funName, unsigned int invocation) 
{
	for(int i = 0; i < getNumThreads(); i++) {
		for(int j = getThreadSize(i)-1; j > 0; j--) {
			const EventLabel *eLab = getEventLabel(Event(i, j));
			if (funName == eLab->getMethod() && eLab->getInvocation() == invocation) {
				return eLab;
			}
		}
	}
	return getEventLabel(Event(0, 0));
}
// --End


/************************************************************
 ** Calculation of [(po U rf)*] predecessors and successors
 ***********************************************************/

void ExecutionGraph::addCalculator(std::unique_ptr<Calculator> cc, RelationId r,
				 bool perLoc, bool partial /* = false */)
{
	/* Add a calculator for this relation */
	auto calcSize = getCalcs().size();
	consistencyCalculators.push_back(std::move(cc));
	if (partial)
		partialConsCalculators.push_back(calcSize);

	/* Add a matrix for this relation */
	auto relSize = 0u;
	if (perLoc) {
		relSize = relations.perLoc.size();
		relations.perLoc.push_back(Calculator::PerLocRelation());
		relsCache.perLoc.push_back(Calculator::PerLocRelation());
	} else {
		relSize = relations.global.size();
		relations.global.push_back(Calculator::GlobalRelation());
		relsCache.global.push_back(Calculator::GlobalRelation());
	}

	/* Update indices trackers */
	calculatorIndex[r] = calcSize;
	relationIndex[r] = relSize;
}

Calculator::GlobalRelation& ExecutionGraph::getGlobalRelation(RelationId id)
{
	BUG_ON(relationIndex.count(id) == 0);
	return relations.global[relationIndex[id]];
}

Calculator::PerLocRelation& ExecutionGraph::getPerLocRelation(RelationId id)
{
	BUG_ON(relationIndex.count(id) == 0);
	return relations.perLoc[relationIndex[id]];
}

Calculator::GlobalRelation& ExecutionGraph::getCachedGlobalRelation(RelationId id)
{
	BUG_ON(relationIndex.count(id) == 0);
	return relsCache.global[relationIndex[id]];
}

Calculator::PerLocRelation& ExecutionGraph::getCachedPerLocRelation(RelationId id)
{
	BUG_ON(relationIndex.count(id) == 0);
	return relsCache.perLoc[relationIndex[id]];
}

void ExecutionGraph::cacheRelations(bool copy /* = true */)
{
	if (copy)
		relsCache = relations;
	else
		relsCache = std::move(relations);
	return;
}

void ExecutionGraph::restoreCached(bool move /* = false */)
{
	if (!move)
		relations = relsCache;
	else
		relations = std::move(relsCache);
	return;
}

bool ExecutionGraph::hasCalculator(RelationId id) const
{
	if (!relationIndex.count(id))
		return false;
	auto idx = relationIndex.at(id);
	return 0 <= idx && idx < consistencyCalculators.size();
}

Calculator *ExecutionGraph::getCalculator(RelationId id)
{
	return consistencyCalculators[calculatorIndex[id]].get();
}

CoherenceCalculator *ExecutionGraph::getCoherenceCalculator()
{
	return static_cast<CoherenceCalculator *>(
		consistencyCalculators[relationIndex[RelationId::co]].get());
}

CoherenceCalculator *ExecutionGraph::getCoherenceCalculator() const
{
	return static_cast<CoherenceCalculator *>(
		consistencyCalculators.at(relationIndex.at(RelationId::co)).get());
}

LBCalculatorLAPOR *ExecutionGraph::getLbCalculatorLAPOR()
{
	return static_cast<LBCalculatorLAPOR *>(
		consistencyCalculators[relationIndex[RelationId::lb]].get());
}

LBCalculatorLAPOR *ExecutionGraph::getLbCalculatorLAPOR() const
{
	return static_cast<LBCalculatorLAPOR *>(
		consistencyCalculators.at(relationIndex.at(RelationId::lb)).get());
}

std::vector<Event> ExecutionGraph::getLbOrderingLAPOR() const
{
	return getLbCalculatorLAPOR()->getLbOrdering();
}

const std::vector<Calculator *> ExecutionGraph::getCalcs() const
{
	std::vector<Calculator *> result;

	for (auto i = 0u; i < consistencyCalculators.size(); i++)
		result.push_back(consistencyCalculators[i].get());
	return result;
}

const std::vector<Calculator *> ExecutionGraph::getPartialCalcs() const
{
	std::vector<Calculator *> result;

	for (auto i = 0u; i < partialConsCalculators.size(); i++)
		result.push_back(consistencyCalculators[partialConsCalculators[i]].get());
	return result;
}

void ExecutionGraph::addPersistencyChecker(std::unique_ptr<PersistencyChecker> pc)
{
	persChecker = std::move(pc);
	return;
}

bool ExecutionGraph::isHbBefore(Event a, Event b, CheckConsType t /* = fast */)
{
	if (getFPStatus() == FS_Done && getFPType() == t)
		return getGlobalRelation(ExecutionGraph::RelationId::hb)(a, b);
	if (t == CheckConsType::fast)
		return getEventLabel(b)->getHbView().contains(a);

	/* We have to trigger a calculation */
	isConsistent(t);
	return getGlobalRelation(ExecutionGraph::RelationId::hb)(a, b);
}

bool isCoMaximalInRel(const Calculator::PerLocRelation &co, SAddr addr, const Event &e)
{
	auto &coLoc = co.at(addr);
	return (e.isInitializer() && coLoc.empty()) ||
		(!e.isInitializer() && coLoc.adj_begin(e) == coLoc.adj_end(e));
}

bool ExecutionGraph::isCoMaximal(SAddr addr, Event e, bool checkCache /* = false */,
				 CheckConsType t /* = fast */)
{
	auto *cc = getCoherenceCalculator();

	if (checkCache)
		return cc->isCachedCoMaximal(addr, e);
	if (getFPStatus() == FS_Done && getFPType() == t)
		return isCoMaximalInRel(getPerLocRelation(RelationId::co), addr, e);
	if (t == CheckConsType::fast)
		return cc->isCoMaximal(addr, e);

	isConsistent(t);
	return isCoMaximalInRel(getPerLocRelation(RelationId::co), addr, e);
}

void ExecutionGraph::doInits(bool full /* = false */)
{
	auto &hb = relations.global[relationIndex[RelationId::hb]];
	populateHbEntries(hb);
	hb.transClosure();

	/* Clear out unused locations */
	for (auto i = 0u; i < relations.perLoc.size(); i++) {
		relations.perLoc[i].clear();
		relsCache.perLoc[i].clear();
       }

	auto &calcs = consistencyCalculators;
	auto &partial = partialConsCalculators;
	for (auto i = 0u; i < calcs.size(); i++) {
		if (!full && std::find(partial.begin(), partial.end(), i) == partial.end())
			continue;

		calcs[i]->initCalc();
	}
	return;
}

Calculator::CalculationResult ExecutionGraph::doCalcs(bool full /* = false */)
{
	Calculator::CalculationResult result;

	auto &calcs = consistencyCalculators;
	auto &partial = partialConsCalculators;
	for (auto i = 0u; i < calcs.size(); i++) {
		if (!full && std::find(partial.begin(), partial.end(), i) == partial.end())
			continue;

		result |= calcs[i]->doCalc();

		/* If an inconsistency was spotted, no reason to call
		 * the other calculators */
		if (!result.cons)
			return result;
	}
	return result;
}


void addPerLocRelationToExtend(Calculator::PerLocRelation &rel,
			       std::vector<Calculator::GlobalRelation *> &toExtend,
			       std::vector<SAddr > &extOrder)

{
	for (auto &loc : rel) {
		toExtend.push_back(&loc.second);
		extOrder.push_back(loc.first);
	}
}

void extendPerLocRelation(Calculator::PerLocRelation &rel,
			  std::vector<std::vector<Event> >::iterator extsBegin,
			  std::vector<std::vector<Event> >::iterator extsEnd,
			  std::vector<SAddr >::iterator locsBegin,
			  std::vector<SAddr >::iterator locsEnd)

{
	BUG_ON(std::distance(extsBegin, extsEnd) != std::distance(locsBegin, locsEnd));
	auto locIt = locsBegin;
	for (auto extIt = extsBegin; extIt != extsEnd; ++extIt, ++locIt) {
		rel[*locIt] = Calculator::GlobalRelation(*extIt);
		for (auto j = 1; j < extIt->size(); j++)
			rel[*locIt].addEdge((*extIt)[j - 1], (*extIt)[j]);
	}
}

bool ExecutionGraph::doFinalConsChecks(bool checkFull /* = false */)
{
	if (!checkFull)
		return true;

	bool hasLB = hasCalculator(RelationId::lb);
	bool hasWB = llvm::isa<WBCalculator>(getCoherenceCalculator());
	if (!hasLB && !hasWB)
		return true;

	/* Cache all relations because we will need to restore them
	 * after possible extensions were tried*/
	cacheRelations();

	/* We flatten all per-location relations that need to be
	 * extended.  However, we need to ensure that, for each
	 * per-location relation, the order in which the locations of
	 * the relation are added to the extension list is the same as
	 * the order in which the extensions of said locations are
	 * restored.
	 *
	 * This is not the case in all platforms if we are simply
	 * iterating over unordered_maps<> (e.g. macΟS). The iteration
	 * order may differ if, e.g., when saving an extension we
	 * iterate over a relation's cache, while when restoring we
	 * iterate over the relation itself, even though the relation
	 * is the same as its cache just before restoring. */
	auto coSize = 0u;
	auto lbSize = 0u;
	std::vector<Calculator::GlobalRelation *> toExtend;
	std::vector<SAddr> extOrder;
	if (hasWB) {
		addPerLocRelationToExtend(getCachedPerLocRelation(ExecutionGraph::RelationId::co),
					  toExtend, extOrder);
		coSize = getCachedPerLocRelation(ExecutionGraph::RelationId::co).size();
	}
	if (hasLB) {
		addPerLocRelationToExtend(getCachedPerLocRelation(ExecutionGraph::RelationId::lb),
					  toExtend, extOrder);
		lbSize = getCachedPerLocRelation(ExecutionGraph::RelationId::lb).size();
	}

	auto res = Calculator::GlobalRelation::
		combineAllTopoSort(toExtend, [&](std::vector<std::vector<Event>> &sortings){
			restoreCached();
			auto count = 0u;
			if (hasWB) {
				extendPerLocRelation(getPerLocRelation(ExecutionGraph::RelationId::co),
						     sortings.begin(), sortings.begin() + coSize,
						     extOrder.begin(), extOrder.begin() + coSize);
			}
			count += coSize;
			if (hasLB && count >= coSize) {
				extendPerLocRelation(getPerLocRelation(ExecutionGraph::RelationId::lb),
						     sortings.begin() + count,
						     sortings.begin() + count + lbSize,
						     extOrder.begin() + count,
						     extOrder.begin() + count + lbSize);
			}
			count += lbSize;
			return doCalcs(true).cons;
		});
	return res;
}

bool ExecutionGraph::isConsistent(CheckConsType checkT)
{
	/* Fastpath: We have cached info or no fixpoint is required */
	if (getFPStatus() == FS_Done && getFPType() == checkT)
		return getFPResult().cons;
	if (checkT == CheckConsType::fast)
		return true;

	/* Slowpath: Go calculate fixpoint */
	setFPStatus(FS_InProgress);
	doInits(checkT == CheckConsType::full);
	do {
		setFPResult(doCalcs(checkT == CheckConsType::full));
		if (!getFPResult().cons)
			return false;
	} while (getFPResult().changed);

	/* Do final checks, after the fixpoint is over */
	setFPResult(FixpointResult(false, doFinalConsChecks(checkT == CheckConsType::full)));
	setFPStatus(FS_Done);
	setFPType(checkT);
	return getFPResult().cons;
}

void ExecutionGraph::trackCoherenceAtLoc(SAddr addr)
{
	return getCoherenceCalculator()->trackCoherenceAtLoc(addr);
}

std::pair<int, int>
ExecutionGraph::getCoherentPlacings(SAddr addr, Event pos, bool isRMW) {
	return getCoherenceCalculator()->getPossiblePlacings(addr, pos, isRMW);
};

std::vector<Event>
ExecutionGraph::getCoherentStores(SAddr addr, Event pos)
{
	return getCoherenceCalculator()->getCoherentStores(addr, pos);
}

std::vector<Event>
ExecutionGraph::getCoherentRevisits(const WriteLabel *wLab)
{
	return getCoherenceCalculator()->getCoherentRevisits(wLab);
}

std::unique_ptr<VectorClock>
ExecutionGraph::getRevisitView(const BackwardRevisit &r) const
{
	auto *rLab = getReadLabel(r.getPos());
	auto preds = std::make_unique<View>(getViewFromStamp(rLab->getStamp()));
	preds->update(getWriteLabel(r.getRev())->getPorfView());
	if (auto *br = llvm::dyn_cast<BackwardRevisitHELPER>(&r))
		preds->update(getWriteLabel(br->getMid())->getPorfView());
	return std::move(preds);
}

const DepView &ExecutionGraph::getPPoRfBefore(Event e) const
{
	return getEventLabel(e)->getPPoRfView();
}

const View &ExecutionGraph::getPorfBefore(Event e) const
{
	return getEventLabel(e)->getPorfView();
}

const View &ExecutionGraph::getHbPoBefore(Event e) const
{
	return getPreviousNonEmptyLabel(e)->getHbView();
}

#define IMPLEMENT_POPULATE_ENTRIES(MATRIX, GET_VIEW)			\
do {								        \
									\
        const std::vector<Event> &es = MATRIX.getElems();		\
        auto len = es.size();						\
									\
        for (auto i = 0u; i < len; i++) {				\
		for (auto j = 0u; j < len; j++) {			\
			if (es[i] != es[j] &&				\
			    GET_VIEW(es[j]).contains(es[i]))		\
				MATRIX.addEdge(i, j);			\
		}							\
        }								\
} while (0)

void ExecutionGraph::populatePorfEntries(AdjList<Event, EventHasher> &relation) const
{
	IMPLEMENT_POPULATE_ENTRIES(relation, getPorfBefore);
}

void ExecutionGraph::populatePPoRfEntries(AdjList<Event, EventHasher> &relation) const
{
	IMPLEMENT_POPULATE_ENTRIES(relation, getPPoRfBefore);
}

void ExecutionGraph::populateHbEntries(AdjList<Event, EventHasher> &relation) const
{
	std::vector<Event> elems;
	std::vector<std::pair<Event, Event> > edges;

	for (auto i = 0u; i < getNumThreads(); i++) {
		auto thrIdx = elems.size();
		for (auto j = 0u; j < getThreadSize(i); j++) {
			auto *lab = getEventLabel(Event(i, j));
			if (!isNonTrivial(lab))
				continue;

			auto labIdx = elems.size();
			elems.push_back(Event(i, j));

			if (labIdx == thrIdx) {
				auto *bLab = getEventLabel(Event(i, 0));
				BUG_ON(!llvm::isa<ThreadStartLabel>(bLab));

				auto parentLast = getPreviousNonTrivial(
					llvm::dyn_cast<ThreadStartLabel>(bLab)->getParentCreate());
				if (!parentLast.isInitializer())
					edges.push_back(std::make_pair(parentLast, elems[labIdx]));
			}
			if (labIdx > thrIdx)
				edges.push_back(std::make_pair(elems[labIdx - 1], elems[labIdx]));
			if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab)) {
				if (!rLab->getRf().isInitializer()) {
					auto pred = (labIdx > thrIdx) ?
						elems[labIdx - 1] : Event::getInitializer();
					auto &v = rLab->getHbView();
					auto &predV = getEventLabel(pred)->getHbView();
					for (auto k = 0u; k < v.size(); k++) {
						if (k != rLab->getThread() &&
						    v[k] > 0 &&
						    !predV.contains(Event(k, v[k]))) {
							auto cndt = getPreviousNonTrivial(Event(k, v[k]).next());
							if (cndt.isInitializer())
								continue;
							edges.push_back(std::make_pair(cndt, rLab->getPos()));
						}
					}
				}
			}
		}
	}
	relation = AdjList<Event, EventHasher>(std::move(elems));
	for (auto &e : edges)
		relation.addEdge(e.first, e.second);
	return;
}


/************************************************************
 ** Calculation of particular sets of events/event labels
 ***********************************************************/

std::unique_ptr<VectorClock> ExecutionGraph::getPredsView(Event e) const
{
	auto stamp = getEventLabel(e)->getStamp();
	return std::make_unique<View>(getViewFromStamp(stamp));
}

#ifdef ENABLE_GENMC_DEBUG
std::vector<std::unique_ptr<EventLabel> >
ExecutionGraph::getPrefixLabelsNotBefore(const WriteLabel *sLab,
					 const ReadLabel *rLab) const
{
	std::vector<std::unique_ptr<EventLabel> > result;

	auto &prefix = sLab->getPorfView();
	auto before = getViewFromStamp(rLab->getStamp());
	for (auto i = 0u; i < getNumThreads(); i++) {
		for (auto j = before[i] + 1; j <= prefix[i]; j++) {
			const EventLabel *lab = getEventLabel(Event(i, j));
			result.push_back(lab->clone());

			auto &curLab = result.back();
			if (auto *wLab = llvm::dyn_cast<WriteLabel>(curLab.get())) {
				wLab->removeReader([&](Event r) {
						return !prefix.contains(r) &&
						       !before.contains(r);
					});
			} else if (auto *eLab = llvm::dyn_cast<ThreadFinishLabel>(curLab.get())) {
				if (!prefix.contains(eLab->getParentJoin()) &&
				    !before.contains(eLab->getParentJoin()))
					eLab->setParentJoin(Event::getInitializer());
			} else if (auto *cLab = llvm::dyn_cast<ThreadCreateLabel>(curLab.get())) {
				/* We can keep the begin event of the child
				 * the since it will not be deleted */
				;
			}
		}
	}
	return result;
}

std::vector<Event>
ExecutionGraph::extractRfs(const std::vector<std::unique_ptr<EventLabel> > &labs) const
{
	std::vector<Event> rfs;

	for (auto const &lab : labs) {
		if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab.get()))
			rfs.push_back(rLab->getRf());
	}
	return rfs;
}

std::vector<std::pair<Event, Event> >
ExecutionGraph::saveCoherenceStatus(const std::vector<std::unique_ptr<EventLabel> > &prefix,
				    const ReadLabel *rLab) const
{
	return getCoherenceCalculator()->saveCoherenceStatus(prefix, rLab);
}
#endif

/************************************************************
 ** Calculation of writes a read can read from
 ***********************************************************/

void ExecutionGraph::remove(const EventLabel *lab)
{
	setFPStatus(FS_Stale);
	if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab)) {
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(getEventLabel(rLab->getRf())))
			wLab->removeReader([&](const Event &r){ return r == rLab->getPos(); });
	}
	if (lab != getLastThreadLabel(lab->getThread()))
		addOtherLabelToGraph(EmptyLabel::create(lab->getPos()));
	BUG_ON(lab->getIndex() >= getThreadSize(lab->getThread()));
	resizeThread(lab->getPos());
}

bool ExecutionGraph::isNonTrivial(const Event e) const
{
	return isNonTrivial(getEventLabel(e));
}

bool ExecutionGraph::isNonTrivial(const EventLabel *lab) const
{
	if (auto *lLab = llvm::dyn_cast<LockLabelLAPOR>(lab))
		return isCSEmptyLAPOR(lLab);
	return llvm::isa<MemAccessLabel>(lab) ||
	       llvm::isa<FenceLabel>(lab);
}

bool ExecutionGraph::isCSEmptyLAPOR(const LockLabelLAPOR *lLab) const
{
	if (lLab->getIndex() == getThreadSize(lLab->getThread()) - 1)
		return true;

	auto *nLab = getEventLabel(lLab->getPos().next());
	if (auto *uLab = llvm::dyn_cast<UnlockLabelLAPOR>(nLab))
		return lLab->getLockAddr() == uLab->getLockAddr();
	return false;
}

bool ExecutionGraph::isHbOptRfBefore(const Event e, const Event write) const
{
	const EventLabel *lab = getEventLabel(write);

	BUG_ON(!llvm::isa<WriteLabel>(lab));
	auto *sLab = static_cast<const WriteLabel *>(lab);
	if (sLab->getHbView().contains(e))
		return true;

	for (auto &r : sLab->getReadersList()) {
		if (getEventLabel(r)->getHbView().contains(e))
			return true;
	}
	return false;
}

bool ExecutionGraph::isHbOptRfBeforeInView(const Event e, const Event write,
					   const VectorClock &v) const
{
	const EventLabel *lab = getEventLabel(write);

	BUG_ON(!llvm::isa<WriteLabel>(lab));
	auto *sLab = static_cast<const WriteLabel *>(lab);
	if (sLab->getHbView().contains(e))
		return true;

	for (auto &r : sLab->getReadersList()) {
		if (v.contains(r) && r != e && getEventLabel(r)->getHbView().contains(e))
			return true;
	}
	return false;
}

bool ExecutionGraph::isWriteRfBefore(Event a, Event b) const
{
	auto &before = getEventLabel(b)->getHbView();
	if (before.contains(a))
		return true;

	const EventLabel *lab = getEventLabel(a);

	BUG_ON(!llvm::isa<WriteLabel>(lab));
	auto *wLab = static_cast<const WriteLabel *>(lab);
	for (auto &r : wLab->getReadersList())
		if (before.contains(r))
			return true;
	return false;
}

bool ExecutionGraph::isStoreReadByExclusiveRead(Event store, SAddr ptr) const
{
	for (const auto *lab : labels(*this)) {
		if (!isRMWLoad(lab))
			continue;

		auto *rLab = llvm::dyn_cast<ReadLabel>(lab);
		if (rLab->getRf() == store && rLab->getAddr() == ptr)
			return true;
	}
	return false;
}

bool ExecutionGraph::isStoreReadBySettledRMW(Event store, SAddr ptr, const VectorClock &prefix) const
{
	for (const auto *lab : labels(*this)) {
		if (!isRMWLoad(lab))
			continue;

		auto *rLab = llvm::dyn_cast<ReadLabel>(lab);
		if (rLab->getRf() != store || rLab->getAddr() != ptr)
			continue;

		auto *wLab = llvm::dyn_cast<WriteLabel>(getNextLabel(rLab));
		if (!rLab->isRevisitable() && !wLab->hasAttr(WriteAttr::RevBlocker))
			return true;
		if (prefix.contains(rLab->getPos()))
			return true;
	}
	return false;
}

bool ExecutionGraph::isRecoveryValid() const
{
	PersistencyChecker *pc = getPersChecker();
	BUG_ON(!pc);
	return pc->isRecAcyclic();
}

bool ExecutionGraph::hasBeenRevisitedByDeleted(const BackwardRevisit &r,
					       const EventLabel *eLab) const
{
	auto *lab = llvm::dyn_cast<ReadLabel>(eLab);
	if (!lab)
		return false;

	auto *rfLab = getEventLabel(lab->getRf());
	auto v = getRevisitView(r);
	return !v->contains(rfLab->getPos()) &&
		rfLab->getStamp() > lab->getStamp() &&
		!prefixContainsSameLoc(r, rfLab) &&
		!prefixContainsMatchingLock(r, rfLab) &&
		(!hasBAM() || !llvm::isa<BIncFaiWriteLabel>(getEventLabel(rfLab->getPos())));
}

bool ExecutionGraph::isMaximalExtension(const BackwardRevisit &r) const
{
	return getCoherenceCalculator()->inMaximalPath(r);
}

bool ExecutionGraph::revisitModifiesGraph(const BackwardRevisit &r) const
{
	auto v = getRevisitView(r);
	for (auto i = 0u; i < getNumThreads(); i++) {
		if ((*v)[i] + 1 != (int) getThreadSize(i) &&
		    !EventLabel::denotesThreadEnd(getEventLabel(Event(i, (*v)[i] + 1))))
			return true;
	}
	return false;
}


/************************************************************
 ** Graph modification methods
 ***********************************************************/

void ExecutionGraph::changeRf(Event read, Event store)
{
	setFPStatus(FS_Stale);

	/* First, we set the new reads-from edge */
	ReadLabel *rLab = llvm::dyn_cast<ReadLabel>(getEventLabel(read));
	BUG_ON(!rLab);
	Event oldRf = rLab->getRf();
	rLab->setRf(store);

	/*
	 * Now, we need to delete the read from the readers list of oldRf.
	 * For that we need to check:
	 *     1) That the old write it was reading from still exists
	 *     2) That oldRf has not been deleted (and a different event is
	 *        now in its place, perhaps after the restoration of some prefix
	 *        during a revisit)
	 *     3) That oldRf is not the initializer */
	if (oldRf.index > 0 && oldRf.index < (int) getThreadSize(oldRf.thread)) {
		auto *labRef = getEventLabel(oldRf);
		if (auto *oldLab = llvm::dyn_cast<WriteLabel>(labRef))
			oldLab->removeReader([&](Event r){ return r == rLab->getPos(); });
	}

	/* If this read is now reading from bottom, nothing else to do */
	if (store.isBottom())
		return;

	/* Otherwise, add it to the write's reader list */
	auto *rfLab = getEventLabel(store);
	if (auto *wLab = llvm::dyn_cast<WriteLabel>(rfLab))
		wLab->addReader(rLab->getPos());
}

bool ExecutionGraph::updateJoin(Event join, Event childLast)
{
	setFPStatus(FS_Stale);

	auto *jLab = llvm::dyn_cast<ThreadJoinLabel>(getEventLabel(join));
	BUG_ON(!jLab);

	/* If the child thread has not terminated, do not update anything */
	auto *fLab = llvm::dyn_cast<ThreadFinishLabel>(getEventLabel(childLast));
	if (!fLab)
		return false;

	jLab->setChildLast(childLast);
	fLab->setParentJoin(jLab->getPos());
	return true;
}

void ExecutionGraph::resetJoin(Event join)
{
	setFPStatus(FS_Stale);

	/* If there is no parent join label, return */
	auto *jLab = llvm::dyn_cast<ThreadJoinLabel>(getEventLabel(join));
	if (!jLab)
		return;

	/* Otherwise, reset parent join */
	jLab->setChildLast(Event::getInitializer());
	return;
}

/*
 * In the case where the events are not added out-of-order in the graph
 * (i.e., an event has a larger timestamp than all its po-predecessors)
 * we can obtain a view of the graph, given a timestamp. This function
 * returns such a view.
 */
View ExecutionGraph::getViewFromStamp(unsigned int stamp) const
{
	View preds;

	for (auto i = 0u; i < getNumThreads(); i++) {
		for (auto j = (int) getThreadSize(i) - 1; j >= 0; j--) {
			const EventLabel *lab = getEventLabel(Event(i, j));
			if (lab->getStamp() <= stamp) {
				preds[i] = j;
				break;
			}
		}
	}
	return preds;
}

DepView ExecutionGraph::getDepViewFromStamp(unsigned int stamp) const
{
	DepView preds;

	for (auto i = 0u; i < getNumThreads(); i++) {
		int prevPos = 0; /* Position of last concrent event in view */
		for (auto j = 1u; j < getThreadSize(i); j++) {
			const EventLabel *lab = getEventLabel(Event(i, j));
			if (lab->getStamp() <= stamp) {
				preds[i] = j;
				preds.addHolesInRange(Event(i, prevPos + 1), j);
				prevPos = j;
			}
		}
	}
	return preds;
}

void ExecutionGraph::changeStoreOffset(SAddr addr, Event s, int newOffset)
{
	setFPStatus(FS_Stale);

	if (auto *cohTracker = llvm::dyn_cast<MOCalculator>(getCoherenceCalculator()))
		cohTracker->changeStoreOffset(addr, s, newOffset);
}

void ExecutionGraph::cutToStamp(unsigned int stamp)
{
	setFPStatus(FS_Stale);
	auto preds = getViewFromStamp(stamp);

	/* Inform all calculators about the events cutted */
	auto &calcs = consistencyCalculators;
	for (auto i = 0u; i < calcs.size(); i++)
		calcs[i]->removeAfter(preds);

	/* Restrict the graph according to the view (keep begins around) */
	for (auto i = 0u; i < getNumThreads(); i++) {
		auto &thr = events[i];
		thr.erase(thr.begin() + preds[i] + 1, thr.end());
	}

	/* Remove any 'pointers' to events that have been removed */
	for (auto i = 0u; i < getNumThreads(); i++) {
		for (auto j = 0u; j < getThreadSize(i); j++) {
			auto *lab = getEventLabel(Event(i, j));
			/*
			 * If it is a join and the respective Finish has been
			 * removed, renew the Views of this label and continue
			 */
			if (auto *jLab = llvm::dyn_cast<ThreadJoinLabel>(lab)) {
				Event cl = jLab->getChildLast();
				if (cl.index >= (int) getThreadSize(cl.thread))
					resetJoin(jLab->getPos());
			} else if (auto *wLab = llvm::dyn_cast<WriteLabel>(lab)) {
				wLab->removeReader([&](Event r){
						return !preds.contains(r);
					});
			}
			/* No special action for CreateLabels; we can
			 * keep the begin event of the child the since
			 * it will not be deleted */
		}
	}
	return;
}

void ExecutionGraph::copyGraphUpTo(ExecutionGraph &other, const VectorClock &v) const
{
	/* First, populate calculators, etc */
	other.timestamp = timestamp;

	other.relations = relations;
	other.relsCache = relsCache;

	other.relations.fixStatus = FS_Stale;
	other.relsCache.fixStatus = FS_Stale;

	for (const auto &cc : consistencyCalculators)
		other.consistencyCalculators.push_back(cc->clone(other));

	other.partialConsCalculators = partialConsCalculators;

	other.calculatorIndex = calculatorIndex;
	other.relationIndex = relationIndex;

	if (persChecker.get())
		other.persChecker = persChecker->clone(other);
	other.recoveryTID = recoveryTID;

	other.bam = bam;

	/* Then, copy the appropriate events */
	/* FIXME: Fix LAPOR (use addLockLabelToGraphLAPOR??) */
	auto *cc = getCoherenceCalculator();
	auto *occ = other.getCoherenceCalculator();
	BUG_ON(!cc || !occ);

	// FIXME: The reason why we resize to num of threads instead of v.size() is
	// to keep the same size as the interpreter threads.
	other.events.resize(getNumThreads());
	for (auto i = 0u; i < getNumThreads(); i++) {
		other.addOtherLabelToGraph(std::move(getEventLabel(Event(i, 0))->clone()));
		for (auto j = 1; j <= v[i]; j++) {
			if (!v.contains(Event(i, j))) {
				other.addOtherLabelToGraph(
					EmptyLabel::create(other.nextStamp(), Event(i, j)));
				continue;
			}
			auto *nLab = other.addOtherLabelToGraph(getEventLabel(Event(i, j))->clone());
			if (auto *wLab = llvm::dyn_cast<WriteLabel>(nLab)) {
				const_cast<WriteLabel *>(wLab)->removeReader([&v](const Event &r){
					return !v.contains(r);
				});
			}
			if (auto *mLab = llvm::dyn_cast<MemAccessLabel>(nLab))
				occ->trackCoherenceAtLoc(mLab->getAddr());
			if (auto *tcLab = llvm::dyn_cast<ThreadCreateLabel>(nLab))
				;
			if (auto *eLab = llvm::dyn_cast<ThreadFinishLabel>(nLab))
				;
			if (auto *lLab = llvm::dyn_cast<LockLabelLAPOR>(nLab))
				other.getLbCalculatorLAPOR()->addLockToList(lLab->getLockAddr(), lLab->getPos());
		}
	}

	/* Finally, copy coherence info */
	/* FIXME: Temporary ugly hack */
	for (auto it = cc->begin(); it != cc->end(); ++it) {
		for (auto sIt = it->second.begin(); sIt != it->second.end(); ++sIt) {
			if (v.contains(*sIt)) {
				occ->addStoreToLoc(it->first, *sIt, -1);
			}
		}
	}
	/* FIXME: Make sure all fields are copied */
	return;
}

std::unique_ptr<ExecutionGraph> ExecutionGraph::getCopyUpTo(const VectorClock &v) const
{
	auto og = std::unique_ptr<ExecutionGraph>(new ExecutionGraph(warnOnGraphSize));
	copyGraphUpTo(*og, v);
	return og;
}


/************************************************************
 ** PSC calculation
 ***********************************************************/

bool ExecutionGraph::isRMWLoad(const EventLabel *lab) const
{
	if (!llvm::isa<CasReadLabel>(lab) && !llvm::isa<FaiReadLabel>(lab))
		return false;
	const ReadLabel *rLab = static_cast<const ReadLabel *>(lab);

	if (lab->getIndex() == (int) getThreadSize(lab->getThread()) - 1)
		return false;
	const EventLabel *nLab =
		getEventLabel(Event(rLab->getThread(), rLab->getIndex() + 1));

	if (!llvm::isa<MemAccessLabel>(nLab))
		return false;
	auto *mLab = static_cast<const MemAccessLabel *>(nLab);

	return isRMWStore(mLab) && mLab->getAddr() == rLab->getAddr();
}

std::pair<std::vector<Event>, std::vector<Event> >
ExecutionGraph::getSCs() const
{
	std::vector<Event> scs, fcs;

	for (const auto *lab : labels(*this)) {
		if (lab->isSC() && !isRMWLoad(lab))
			scs.push_back(lab->getPos());
		if (lab->isSC() && llvm::isa<FenceLabel>(lab))
			fcs.push_back(lab->getPos());
	}
	return std::make_pair(scs,fcs);
}


/************************************************************
 ** Debugging methods
 ***********************************************************/

void ExecutionGraph::validate(void)
{
	for (auto *lab : labels(*this)) {
		if (auto *rLab = llvm::dyn_cast<ReadLabel>(lab)) {
			if (rLab->getRf().isBottom())
				continue;

			if (!containsNonEmpty(rLab->getRf())) {
				llvm::errs() << "Non-existent RF: " << rLab->getPos() << "\n";
				llvm::errs() << *this << "\n";
				BUG();
			}

			if (auto *rfLab = llvm::dyn_cast<WriteLabel>(getEventLabel(rLab->getRf()))) {
				if (std::find(rfLab->readers_begin(), rfLab->readers_end(), rLab->getPos()) ==
				    rfLab->readers_end()) {
					llvm::errs() << "Not in RF's readers list: " << rLab->getPos() << "\n";
					llvm::errs() << *this << "\n";
					BUG();
				}
			}
		}
		if (auto *wLab = llvm::dyn_cast<WriteLabel>(lab)) {
			if (isRMWStore(wLab) &&
			    std::count_if(wLab->readers_begin(), wLab->readers_end(),
					  [&](const Event &r){ return isRMWLoad(r); }) > 1) {
				llvm::errs() << "Atomicity violation: " << wLab->getPos() << "\n";
				llvm::errs() << *this << "\n";
				BUG();
			}

			if (std::any_of(wLab->readers_begin(), wLab->readers_end(),
					[&](const Event &r){ return !containsNonEmpty(r) ||
							!llvm::isa<ReadLabel>(getEventLabel(r)); })) {
				llvm::errs() << "Non-existent/non-read reader: " << wLab->getPos() << "\n";
				llvm::errs() << "Readers: " << format(wLab->getReadersList()) << "\n";
				llvm::errs() << *this << "\n";
				BUG();
			}

			if (std::any_of(wLab->readers_begin(), wLab->readers_end(),
					[&](const Event &r){ return getReadLabel(r)->getRf() != wLab->getPos(); })) {
				llvm::errs() << "RF not properly set: " << wLab->getPos() << "\n";
				llvm::errs() << *this << "\n";
				BUG();
			}
			for (auto it = wLab->readers_begin(), ie = wLab->readers_end(); it != ie; ++it) {
				if (!containsNonEmpty(*it)) {
					llvm::errs() << "Readers list has garbage: " << *it << "\n";
					llvm::errs() << *this << "\n";
					BUG();
				}
			}
		}
	}
	return;
}


/*******************************************************************************
 **                           Overloaded operators
 ******************************************************************************/

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, const ExecutionGraph &g)
{
	for (auto i = 0u; i < g.getNumThreads(); i++) {
		s << "Thread " << i << ":\n";
		for (auto j = 0u; j < g.getThreadSize(i); j++) {
			const EventLabel *lab = g.getEventLabel(Event(i, j));
			s << "\t" << lab->getStamp() << " @ " << *lab << "\n";
		}
	}
	s << "Thread sizes:\n\t";
	for (auto i = 0u; i < g.getNumThreads(); i++)
		s << g.getThreadSize(i) << " ";
	s << "\n";

	auto *cc = g.getCoherenceCalculator();
	BUG_ON(!cc);
	for (auto it = cc->begin(); it != cc->end(); ++it) {
		s << it->first << ": ";
		s << format(it->second) << "\n";
	}
	return s;
}
