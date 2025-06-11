#include "Axioms.hpp"

/* Axioms for concurrent datastructure model checking. */
/* All Axioms return true if an error was found and report the error by writing to err. */
/* TODO somehow the axioms are affecting each other fix this before using!!!*/

/* Checks if there are no methodcalls at all or if there is exactly one constructor call. */
bool isAtMostOneConstructor(std::vector<FunctionCall> methods, llvm::raw_string_ostream &err)
{
    if (methods.size() == 0) {
        return false;
    }
    int i = 0;
    for (auto m : methods) {
        if (m.getEvent().name == FN_Init) {
            i++;
        }
    }
    if (i == 0 || i > 1) {
        err << "Expected one constructor event but got: " << i << "!\n";
        return true;
    }
    return false;
}

/* Checks if for the current FunctionEvent COM matched specific methodcalls */
bool doesComMatch(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, std::vector<FunctionName> A, std::vector<FunctionName> B, llvm::raw_string_ostream &err)
{
    if (std::find(A.begin(), A.end(), curr.name) == A.end() && rel[COM][curr].size() > 0) {
        // If curr is not in A then it should not have any COM relations.
        err << curr << " Should not have any COM relations but it had: " << rel[COM][curr].size() << "!\n";
        return true;
    }
    if (std::find(B.begin(), B.end(), curr.name) == B.end() && rel[INVCOM][curr].size() > 0) {
        // If curr is not in B then it should not have any INVCOM relations.
        err << curr << " Should not have any INVCOM relations but it had: " << rel[INVCOM][curr].size() << "!\n";
        return true;
    }
    return false;
}

/* Checks if COM matches at most 1:1. */
bool isComAtMostOneToOne(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err)
{
    if (rel[COM][curr].size() > 1) {
        err << "COM should be 1:1, but for: " << curr << " COM size was: " << rel[COM][curr].size() << "!\n";
        return true;
    }
    if (rel[INVCOM][curr].size() > 1) {
        err << "COM should be 1:1, but for: " << curr << " INVCOM size was: " << rel[INVCOM][curr].size() << "!\n";
        return true;
    }
    return false;
}

/* Checks if COM = SO for the current functionEvent. */
bool isComSynchronizing(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err)
{
    if (rel[COM][curr] != rel[SO][curr]) {
        err << "For " << curr << " COM is different from SO but COM should be synchronising!\n";
        return true;
    }
    return false;
}

/* Checks if methodcals matched by COM adhere to FiFo */
bool isFiFo(std::vector<FunctionCall> calls, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err)
{
    for(auto c : calls) {
        FunctionEvent A = c.getEvent();
		std::vector<FunctionEvent> currLHB = rel[LHB][A];
		for (auto B : currLHB) {
			// FunctionEvent 'A' lhb 'B'. 
			// check if both events are of the same type. If so we need to be able to match the events that are com(-1) with them.
			if (A.name == B.name && A.name == FunctionName::FN_Deq && rel[INVCOM][A].size() == 1 && rel[INVCOM][B].size() == 1) {
				// C(enq) COM A(deq), D(enq) COM B(deq).
				FunctionEvent C = rel[INVCOM][A][0]; FunctionEvent D = rel[INVCOM][B][0];
				std::vector<FunctionEvent> DLHB = rel[LHB][D];
				if (std::find(DLHB.begin(), DLHB.end(), C) != DLHB.end()) {
                    err << "Not FiFo: " << A << LHB << B << ", " << D << LHB << C << ", " << C << COM << A << ", and " << D << COM << B << "\n";
					return true;
				}
			}
			if (A.name == B.name && A.name == FunctionName::FN_Enq && rel[COM][A].size() == 1 && rel[COM][B].size() == 1) {
				// A(enq) COM C(deq), B(enq) COM D(deq).
				FunctionEvent C = rel[COM][A][0]; FunctionEvent D = rel[COM][B][0];
				std::vector<FunctionEvent> DLHB = rel[LHB][D];
				if (std::find(DLHB.begin(), DLHB.end(), C) != DLHB.end()) {
                    err << "Not FiFo: " << A << LHB << B << ", " << D << LHB << C << ", " << A << COM << C << ", and " << B << COM << D << "\n";
					return true;	
				}
			}
		}
	}
    return false;
}

/* Checks if this functionEvent is matched. Invcom should be > 0. */
bool isMatched(FunctionEvent curr, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err)
{
    if (rel[INVCOM][curr].size() == 0) {
        err << curr << " Should be matched but INVCOM size is: " << rel[INVCOM][curr].size() << "!\n";
	    return true;
    }
    return false;
}

/* Checks if a methodcall is unmatched (invcom empty) also returns empty. */
/*
bool unMatchedFunctionEmpty(FunctionEvent curr, SVal val, std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, llvm::raw_string_ostream &err)
{
    if (rel[INVCOM][curr].size() == 0 && val.get() != 0) {
        err << curr << " Value of unmatched methodcall should be empty but it was: " << val << "!\n";
        return true;
    }
    return false;
}//*/

/* Checks if methodcall with previous unmatched methods can return empty. */
/*bool doesMatchPreviousUnmatched(FunctionEvent curr,
    FunctionName n,
    std::vector<FunctionCall> calls,
    std::map<OrderType, std::map<FunctionEvent, std::vector<FunctionEvent>>> rel, 
    llvm::raw_string_ostream &err
)
{
    if (rel[INVCOM][curr].size() > 0) {
        return false;
    }
    for (auto fun : calls) {
		if (fun.getEvent().name == n && rel[COM][fun.getEvent()].size() == 0 && fun.isDone()) {
			err << curr << " Should not return empty when " << fun.getEvent() << " is unmatched!\n";
            return true;
        }
	}
    return false;
}//*/
