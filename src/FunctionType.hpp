#ifndef __FUNCTIONTYPE_HPP__
#define __FUNCTIONTYPE_HPP__

#include <llvm/ADT/Hashing.h>
#include <llvm/Support/raw_ostream.h>


    enum FunctionName {
        FN_NON,
        FN_Init,
        FN_Lock,
        FN_Unlock,
        FN_Enq,
        FN_Deq,
        FN_Push,
        FN_Pop,
        FN_Protect0,
        FN_Unprotect0,
        FN_Protect1,
        FN_Unprotect1,
        FN_Reclaim
    };

    struct FunctionEvent {
        FunctionName name;
        unsigned int invocation;

        FunctionEvent(FunctionName n, unsigned int i): name(n), invocation(i) {};

        inline bool operator==(const FunctionEvent &e) const {
            return e.name == name && e.invocation == invocation;
        }
        inline bool operator!=(const FunctionEvent &e) const {
            return !(*this == e);
        }
        inline bool operator<(const FunctionEvent &e) const {
            return (name < e.name) || (name == e.name && invocation < e.invocation);
        }
        inline bool operator>(const FunctionEvent &e) const {
            return (name > e.name) || (name == e.name && invocation > e.invocation);
        }
    };

    enum OrderType {
        PO,
        COM,
        INVCOM,
        SO,
        INVSO,
        LHB
    };

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionName n);

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionEvent e);

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, OrderType e);


#endif /* __FUNCTIONTYPE_HPP__ */
