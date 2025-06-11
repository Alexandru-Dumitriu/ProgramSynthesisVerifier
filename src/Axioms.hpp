#ifndef __AXIOMS_HPP__
#define __AXIOMS_HPP__

#include "FunctionType.hpp"
#include "FunctionCall.hpp"
#include "SVal.hpp"
#include <string>
#include <map>

// std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel

/* Checks if there are no methodcalls at all or if there is exactly one constructor call. */
bool isAtMostOneConstructor(std::vector<FunctionCall> methods, llvm::raw_string_ostream err);

/* Checks if for the current FunctionEvent COM matched specific methodcalls */
bool doesComMatch(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, std::vector<FunctionName> A, std::vector<FunctionName> B, llvm::raw_string_ostream &err);

/* Checks if COM matches at most one:one. */
bool isComAtMostOneToOne(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err);


/* Checks if a functioncall is unmatched (invcom empty) also returns empty. */
// bool unMatchedFunctionEmpty(FunctionEvent curr, SVal val, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err);

/* Checks if methodcall with previous unmatched methods can return empty. */
// bool doesMatchPreviousUnmatched(FunctionEvent curr,
//    FunctionName n,
//    std::vector<FunctionCall> calls,
//    std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, 
//    llvm::raw_string_ostream &err
//);

/* Checks if COM = SO for the current functionEvent. */
bool isComSynchronizing(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err);

/* Checks if methodcals matched by COM adhere to FiFo */
bool isFiFo(std::vector<FunctionCall> calls, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err);

/* Checks if this functionEvent is matched. Invcom should be > 0. */
bool isAllMatched(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err);

#endif /* __AXIOMS_HPP__ */