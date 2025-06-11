#ifndef __MODEL_HPP__
#define __MODEL_HPP__

#include "FunctionType.hpp"
#include <string>
#include <map>

    // Add new models here.
    /* The start and end enums are only used as markers to see what category of model something belongs to. */
    enum DSModelType {
        M_None,
        // Mutex
        M_Mutex,
        // Queue
        M_Queue_Start,
        M_Rel_Queue,
        M_Weak_Queue,
        M_HW_Queue,
        M_Strong_Queue,
        M_Queue_End,
        // Stack
        M_Stack_Start,
        M_Rel_Stack,
        M_Weak_Stack,
        M_C_Stack,
        M_Strong_Stack,
        M_Stack_End,
        // SMR
        M_Hp_Queue
    };

    struct DSModel {
        DSModelType type;
        std::map<FunctionName, std::string> functioncallMap;

        DSModel(): type(M_None), functioncallMap(std::map<FunctionName, std::string>()) {};
        DSModel(DSModelType t, std::map<FunctionName, std::string> m): type(t), functioncallMap(m) {};
    };

DSModel loadDSModelFromFile(std::string configFileLink);

bool checkNamesForModel(DSModelType t, FunctionName n);

#endif /* __MODEL_HPP__ */