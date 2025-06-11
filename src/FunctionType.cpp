#include "FunctionType.hpp"

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionName n)
{
    switch (n)
    {
    case FunctionName::FN_Enq:
        return s << "enqueue";
    case FunctionName::FN_Deq:
        return s << "dequeue";
    case FunctionName::FN_Lock:
        return s << "lock";
    case FunctionName::FN_Unlock:
        return s << "unlock";
    case FunctionName::FN_Pop:
        return s << "pop";
    case FunctionName::FN_Push:
        return s << "push";
    case FunctionName::FN_Init:
        return s << "init";
    case FunctionName::FN_NON:
        return s << "-";
    case FunctionName::FN_Protect0:
        return s << "protect0";
    case FunctionName::FN_Unprotect0:
        return s << "unprotect0";
    case FunctionName::FN_Protect1:
        return s << "protect1";
    case FunctionName::FN_Unprotect1:
        return s << "unprotect1";
    case FunctionName::FN_Reclaim:
        return s << "reclaim";
    default:
        return s << "?";
    }
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, OrderType n)
{
    switch (n)
    {
    case OrderType::PO:
        return s << "PO";
    case OrderType::COM:
        return s << "COM";
    case OrderType::INVCOM:
        return s << "COM-1";
    case OrderType::SO:
        return s << "SO";
    case OrderType::INVSO:
        return s << "SO-1";
    case OrderType::LHB:
        return s << "LHB";
    default:
        return s << "";
    }
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionEvent e) 
{
    return s << e.name << e.invocation; 
}