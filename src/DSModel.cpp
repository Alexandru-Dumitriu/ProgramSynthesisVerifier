
#include "DSModel.hpp"
#include "Error.hpp"
#include <iostream>
#include <fstream>

DSModel loadDSModelFromFile(std::string configFileLink) {
    DSModelType t = DSModelType::M_None;
    std::map<FunctionName, std::string> functioncallMap;

    // No file path given so no model to check.
    if (configFileLink.compare("") == 0) {
        llvm::outs() << "---NO MODEL LOADED---\n";
        return DSModel();
    }

    std::ifstream configFile;
    configFile.open(configFileLink);

    // Something was wrong with the filepath.
    if (!configFile.is_open() ) {
        llvm::outs() << configFileLink << " is not a correct file path.\n";
        return DSModel();
    }

    bool fileError = false;
    bool comment = false;
    std::map<std::string, std::string> data;
    std::string key;
    std::string val;
    bool isKey = true;
    while (configFile) {
        char current = configFile.get();
        std::string curKey = key;
        std::string curVal = val;
        switch (current)
        {
        case '%':
            comment = true;
            break;
        case EOF:
        case '\n':
            isKey = true;
            comment = false;
            data.insert(data.begin(), std::pair<std::string, std::string>(curKey, curVal));
            val.clear();
            key.clear();
            break;
        case ':':
            isKey = false;
            break;
        case ' ':
        case 13:
            // We skip some control characters.
            // TODO check if skipping more characters is needed.
            break;
        default:
            if(comment) {
                break;
            }
            if(isKey) {
                key += current;
            } else {
                val += current;
            }
            break;
        }
    }
    auto modelPos = data.find("model");
    if(modelPos == data.end()) {
        llvm::outs() << "A correct config file should include a 'model'\n";
        BUG();
    }
    if(data["model"].compare("mutex") == 0) {
        t = DSModelType::M_Mutex;
    } else if(data["model"].compare("relaxed_queue") == 0) {
        t = DSModelType::M_Rel_Queue;
    } else if(data["model"].compare("weak_queue") == 0) {
        t = DSModelType::M_Weak_Queue;
    } else if(data["model"].compare("hw_queue") == 0) {
        t = DSModelType::M_HW_Queue;
    } else if (data["model"].compare("strong_queue") == 0) {
        t = DSModelType::M_Strong_Queue;
    } else if(data["model"].compare("relaxed-stack") == 0) {
        t = DSModelType::M_Rel_Stack;
    } else if (data["model"].compare("weak-stack") == 0) {
        t = DSModelType::M_Weak_Stack;
    } else if (data["model"].compare("c-stack") == 0) {
        t = DSModelType::M_C_Stack;
    // }  else if (data["model"].compare("strong-stack") == 0) { need to implement strong stack (checks for TO)
    //  t = DSModelType::M_Strong_Stack;
    } else if (data["model"].compare("hp-queue") == 0) {
        t = DSModelType::M_Hp_Queue;
    } else {
        llvm::outs() << data["model"] << " is not an implemented model.\n";
        BUG();
    }
    // If we are looking at a model for SMR.
    if(t == M_Hp_Queue) {
        auto protect0Pos = data.find("protect0");
        auto unprotect0Pos = data.find("unprotect0");
        auto protect1Pos = data.find("protect1");
        auto unprotect1Pos = data.find("unprotect1");
        auto reclaimPos = data.find("reclaim");

        if(protect0Pos == data.end() || unprotect0Pos == data.end() || protect1Pos == data.end() || unprotect1Pos == data.end() || reclaimPos == data.end()) {
            llvm::outs() << "A queue datastructure with HP should have 'protect' and 'unprotect' methods defined.\n";
            BUG();
        }
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Protect0, data["protect0"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Unprotect0, data["unprotect0"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Protect1, data["protect1"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Unprotect1, data["unprotect1"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Reclaim, data["reclaim"]));
        DSModel ret = DSModel(t, functioncallMap);
        return ret;
    }
    // If we are looking at a model for a queue.
    if(t > M_Queue_Start && t < M_Queue_End) {
        // Then we should find a description for an Enqueue and Dequeue method.
        auto enqPos = data.find("enqueue");
        auto deqPos = data.find("dequeue");
    //  auto initPos = data.find("init");
        if(enqPos == data.end() || deqPos == data.end()) {
            llvm::outs() << "A queue datastructure should have a 'enqueue' and 'dequeue' method defined.\n";
            BUG();
        }
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Enq, data["enqueue"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Deq, data["dequeue"]));
        DSModel ret = DSModel(t, functioncallMap);
        return ret;
    }
    // If we are looking at a model for a mutex. 
    else if (t == M_Mutex) {
        auto lockPos = data.find("lock");
        auto unlockPos = data.find("unlock");
        auto initPos = data.find("init");
        if(lockPos == data.end() || unlockPos == data.end() || initPos == data.end()) {
            llvm::outs() << "A mutex datastructure should have a 'lock', 'unlock', and 'init' method defined.\n";
            BUG();
        }
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Lock, data["lock"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Unlock, data["unlock"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Init, data["init"]));
        DSModel ret = DSModel(t, functioncallMap);
        return ret;
    } else if (t > M_Stack_Start && t < M_Stack_End) {
        auto pushPos = data.find("push");
        auto popPos = data.find("pop");
    //  auto initPos = data.find("init");
        if(pushPos == data.end() || popPos == data.end()) {
            llvm::outs() << "A stack datastructure should have a 'push', and 'pop' method defined.\n";
            BUG();
        }
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Push, data["push"]));
        functioncallMap.insert(std::pair<FunctionName, std::string>(FunctionName::FN_Pop, data["pop"]));
        DSModel ret = DSModel(t, functioncallMap);
        return ret;
    } else {
        // Not yet implemented.
        llvm::outs() << "Models other than queue, stack, and mutex are not yet implemented here.\n";
        BUG();
    }
}

bool checkNamesForModel(DSModelType t, FunctionName n) {
    if (n == FN_NON) {
        // No methodcall should be called non.
        return false;
    }
    if (n == FN_Init) {
        // All constructors should be called Init so is good for any model.
        return true;
    }
    if(t == M_Mutex) {
        // Mutex so Lock or Unlock.
        return n == FN_Lock || n == FN_Unlock;
    } else if(t > M_Queue_Start && t < M_Queue_End) {
        // Queue so Enq or Deq.
        return n == FN_Enq || n == FN_Deq;
    } else if(t > M_Stack_Start && t < M_Stack_End) {
        // Stack so Push or Pop.
        return n == FN_Push || n == FN_Pop;
    } else if(t == M_Hp_Queue) {
        // HP Queue so Protect, Unprotect or Reclaim.
        return n == FN_Protect0 || n == FN_Unprotect0 || n == FN_Protect1 || n == FN_Unprotect1 || n == FN_Reclaim;
    } else {
        // Unknown Model so no idea.
        return false;
    }
}