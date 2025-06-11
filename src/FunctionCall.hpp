#ifndef __FUNCTIONCALL_HPP__
#define __FUNCTIONCALL_HPP__

#include "functionType.hpp"
#include "Event.hpp"
#include "EventLabel.hpp"

struct FunctionCall {
public:
    FunctionCall(bool d, FunctionEvent e, std::vector<EventLabel*> lbl, std::vector<ReadLabel*> r, std::vector<WriteLabel*> w): done(d), event(e), 
        labels(lbl), reads(r), writes(w) {};
    FunctionCall(bool d, FunctionEvent e, std::vector<EventLabel*> lbl, std::vector<ReadLabel*> r, std::vector<WriteLabel*> w, std::vector<int> vals): done(d), event(e), 
        labels(lbl), reads(r), writes(w), values(vals){
            if(values.size() > 0) {
                validValue = true;
            }
        };

    FunctionEvent getEvent() { return event; }
    std::vector<EventLabel*> getlabels() { return labels; }
    std::vector<WriteLabel*> getWritelabels() { return writes; }
    std::vector<ReadLabel*> getReadlabels() { return reads; }
    bool isDone() { return done; }
    bool isValueValid() {return validValue; }
    int getVal(int i) {
        if(validValue) {
            if(i >= values.size()) {
                return -2;
            }
            return values[i];  
        }
        return -1; 
    }
    EventLabel* getLastEvent() { return labels.back(); }
    WriteLabel* getNthWrite(int n) { if(n >= writes.size()) { return nullptr; } return writes[n]; }
    WriteLabel* getLastWrite() { if(writes.size() == 0  || !done) { return nullptr; } return writes.back(); }
    std::vector<FunctionEvent> getCOMorder() {return getLastEvent()->getCOM(); }
    std::vector<FunctionEvent> getSOorder() {return getLastEvent()->getSO(); }

    inline bool operator==(const FunctionCall &e) const {
        return event == e.event;
    }
    inline bool operator!=(const FunctionCall &e) const {
        return !(*this == e);
    }
    inline bool operator<(const FunctionCall &e) const {
         return event < e.event;
    }
    inline bool operator>(const FunctionCall &e) const {
        return event > e.event;
    }
    
private:
    // if the function is fully executed.
    bool done;
    bool validValue = false;
    std::vector<int> values;
    FunctionEvent event;
    // All eventlabels of the methodcall.
    std::vector<EventLabel*> labels;
    // All writes of the functioncall.
    std::vector<WriteLabel*> writes;
    // All reads of the functioncall (might not need).
    std::vector<ReadLabel*> reads;
};

    // for printing.
    llvm::raw_ostream& operator<<(llvm::raw_ostream &s, FunctionCall c);

#endif /* __FUNCTIONTYPE_HPP__ */
