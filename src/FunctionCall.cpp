#include "FunctionCall.hpp"

llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionCall c)
{
    s << c.getEvent();
    if(c.isValueValid()) {
        s << " with val: " << c.getVal(1);
    }
    return s;
}