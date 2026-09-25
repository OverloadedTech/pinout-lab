#include "lab.hpp"
#include <android/input.h>
#include <atomic>

namespace mlab {
namespace {
std::mutex inputMutex;
std::vector<float> rectangles;
bool exclusive=false,captured=false;
std::atomic<uint64_t> forwarded{0};
int32_t (*getOriginal)(AInputQueue*,AInputEvent**);
void (*finishOriginal)(AInputQueue*,AInputEvent*,int);
bool belongsToTools(AInputEvent* event) {
    std::lock_guard<std::mutex> lock(inputMutex);
    if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY)
        return exclusive||AKeyEvent_getKeyCode(event)==AKEYCODE_F1;
    if(AInputEvent_getType(event)!=AINPUT_EVENT_TYPE_MOTION)return false;
    int action=AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK;
    if(action==AMOTION_EVENT_ACTION_DOWN) {
        float x=AMotionEvent_getX(event,0),y=AMotionEvent_getY(event,0);captured=exclusive;
        for(size_t i=0;i+3<rectangles.size();i+=4)
            if(x>=rectangles[i]&&y>=rectangles[i+1]&&x<rectangles[i+2]&&y<rectangles[i+3])captured=true;
    }
    bool result=captured;
    if(action==AMOTION_EVENT_ACTION_UP||action==AMOTION_EVENT_ACTION_CANCEL)captured=false;
    return result;
}
int32_t getHook(AInputQueue* queue,AInputEvent** output) {
    for(;;) {
        int32_t result=getOriginal(queue,output);if(result<0)return result;
        if(!belongsToTools(*output))return result;
        // NativePostImeInputStage precedes Android's View dispatch. Returning
        // unhandled here forwards the real event to the Java tools and prevents
        // the same touch from jumping/shooting in the original game.
        finishOriginal(queue,*output,0);++forwarded;
    }
}
}
void installInput() {
    if(!interceptsInput())return;
    finishOriginal=engine.function<decltype(finishOriginal)>("AInputQueue_finishEvent");
    engine.hook("AInputQueue_getEvent",reinterpret_cast<void*>(getHook),reinterpret_cast<void**>(&getOriginal));
}
void inputRegions(std::vector<float> values,bool all) {
    std::lock_guard<std::mutex> lock(inputMutex);rectangles=std::move(values);exclusive=all;
}
Json inputSnapshot() {return {{"android_tool_input_events",forwarded.load()}};}
}
