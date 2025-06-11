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
#include "FunctionInlinerPass.hpp"
#include "Error.hpp"
#include "DeclareInternalsPass.hpp"
#include "InterpreterEnumAPI.hpp"
#include "MDataCollectionPass.hpp"
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/Cloning.h>

using namespace llvm;

void FunctionInlinerPass::getAnalysisUsage(AnalysisUsage &au) const
{
	au.addRequired<DominatorTreeWrapperPass>();
	au.addRequired<DeclareInternalsPass>();
	au.addRequired<MDataCollectionPass>();
}

bool isInlinable(CallGraph &CG, CallGraphNode *node, SmallVector<CallGraphNode *, 4> &chain)
{
	/* Base cases: indirect/empty/recursive calls */
	auto *F = node->getFunction();
	if (node == CG.getCallsExternalNode())
		return false;
	if (F->empty())
		return isInternalFunction(F->getName().str());
	if (std::find(chain.begin(), chain.end(), node) != chain.end())
		return false;

	chain.push_back(node);
	for (auto &c : *node) {
		if (!isInlinable(CG, c.second, chain))
			return false;
	}
	chain.pop_back();
	return true;
}

bool isInlinable(CallGraph &CG, Function &F)
{
	SmallVector<CallGraphNode *, 4> chain;
	return isInlinable(CG, CG[&F], chain);
}

bool inlineCall(CallInst *ci)
{
	llvm::InlineFunctionInfo ifi;

#if LLVM_VERSION_MAJOR >= 11
	BUG_ON(!InlineFunction(*ci, ifi).isSuccess());
#else
	BUG_ON(!InlineFunction(ci, ifi));
#endif
      return true;
}

bool allowedFunction(std::string funName, DSModel dsmodel) {
	if(dsmodel.type == DSModelType::M_None) {
		return false;
	}
	for(auto kv : dsmodel.functioncallMap) {
		if (funName.compare(kv.second) == 0) {
			// llvm::outs() << funName << "\n";
			return true;
		}
	}
	return false;
}


bool inlineFunction(Module &M, Function *toInline, DSModel dsmodel)
{
	std::vector<CallInst *> calls;
	for (auto &F : M) {
		for (auto &iit : instructions(F)) {
			auto *ci = dyn_cast<CallInst>(&iit);
			if (ci) {
				if (ci->getCalledFunction() == toInline) {
					calls.push_back(ci);
				}
			}
		}
	}

	bool changed = false;
	std::for_each(calls.begin(), calls.end(), [&](CallInst *ci){
		/* --add-- */
		if(allowedFunction(toInline->getFunction().getName().str(), dsmodel)) {
			LLVMContext& C = ci->getContext();
			MDNode* beforeMark = MDNode::get(C, MDString::get(C, toInline->getFunction().getName().str()+"_start"));
			auto *p = Constant::getNullValue(PointerType::get(M.getFunction("main")->getReturnType(), 0));
			auto *v = ConstantInt::get(M.getFunction("main")->getReturnType(), 0);
			//auto *before = new llvm::LoadInst(M.getFunction("main")->getReturnType(), p, Twine(), false, Align());
			auto *before = new llvm::FenceInst(C, llvm::AtomicOrdering::Release);
			before->setMetadata("new.function.name", beforeMark);
			before->insertBefore(ci);
			MDNode* afterMark = MDNode::get(C, MDString::get(C, toInline->getFunction().getName().str()+"_end"));
			//auto *after = new llvm::LoadInst(M.getFunction("main")->getReturnType(), p, Twine(), false, Align());
			//auto *after = llvm::BinaryOperator::Create(llvm::Instruction::BinaryOps::BinaryOpsBegin, v, v, Twine());
			auto *after = new llvm::FenceInst(C, llvm::AtomicOrdering::Release);
			after->setMetadata("new.function.name", afterMark);
			after->insertAfter(ci);
		}
		
		/* --end-- */
		changed |= inlineCall(ci);
	});
	return changed;
}

bool FunctionInlinerPass::runOnModule(Module &M)
{
	CallGraph CG(M);

	auto changed = false;
	for (auto &F : M) {
		if (!F.empty() && isInlinable(CG, F)) {
			changed |= inlineFunction(M, &F, dsmodel);
		}
	}
	return changed;
}

ModulePass *createFunctionInlinerPass(DSModel m)
{
	return new FunctionInlinerPass(m);
}

char FunctionInlinerPass::ID = 42;
static llvm::RegisterPass<FunctionInlinerPass> P("function-inliner",
						 "Inlines all non-recursive functions.");
