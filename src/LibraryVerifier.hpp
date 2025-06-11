#ifndef __CONCURRENTLIBRARY_HPP__
#define __CONCURRENTLIBRARY_HPP__

#include "FunctionCall.hpp"

class ConcurrentLibrary {

public:
	enum LibraryKind {
		L_HWQUEUE,
    //  L_STACK,
        L_NON
	};

protected:

//	ConcurrentLibrary() : {}

public:
    bool verify(std::vector<FunctionCall> functions);

private:
    LibraryKind kind;


};

#endif /* __CONCURRENTLIBRARY_HPP__ */