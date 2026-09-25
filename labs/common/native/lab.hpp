#pragma once
#include "../../../dev/native/math.hpp"
#include "json.hpp"
#include <android/asset_manager.h>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace mlab {
using Json = nlohmann::json;
using shdev::Vec3;
using shdev::Quat;
using shdev::Bounds;
using shdev::PI;
struct Transform {Vec3 pos;Quat rot;};
template<class T> T read(const void* p, size_t off=0) {
    T v{}; if(p) std::memcpy(&v, static_cast<const char*>(p)+off, sizeof(v)); return v;
}
template<class T> void write(void* p, size_t off, const T& v) {
    if(!p) throw std::runtime_error("Native object no longer exists");
    std::memcpy(static_cast<char*>(p)+off,&v,sizeof(v));
}
inline void* at(void* p,size_t off=0) { return p ? static_cast<char*>(p)+off : nullptr; }
inline void* ptr(void* p,size_t off=0) { return read<void*>(p,off); }
inline Json jvec(Vec3 v) { return Json::array({v.x,v.y,v.z}); }
inline Json jquat(Quat v) { return Json::array({v.x,v.y,v.z,v.w}); }
Vec3 vector(const Json& j,float limit=100000);
std::string qiString(void* p);
std::vector<void*> pointers(void* p,size_t off,int limit=100000);
std::string address(void* p);
std::string digest(const void* data,size_t bytes);

struct Engine {
    void* library=nullptr;
    void** global=nullptr;
    uintptr_t base=0;
    std::map<std::string,std::vector<void**>> slots;
    std::string buildId;
    void load(const std::string& verifiedHash);
    void* symbol(const char* name,bool required=true);
    template<class T> T function(const char* name) { return reinterpret_cast<T>(symbol(name)); }
    void hook(const char* name,void* replacement,void** original);
    void* game() const { return global ? *global : nullptr; }
};
extern Engine engine;

struct Object {
    void* native=nullptr;
    std::string key,name,type,section;
    Transform transform;
    Vec3 scale{1,1,1};
    Bounds bounds;
    bool editable=false;
    Json detail;
};

struct State {
    bool installed=false, paused=false, immortal=false, noclip=true;
    bool cameraReady=false, fog=true, fovOverride=false, multi=false,recenterNextFrame=false;
    std::string mode="play", files, gameName, message, error;
    float fov=70, normalFov=70, moveSpeed=6, lookSpeed=1, simulationSpeed=1;
    float nativeNear=.05f,nativeFar=300,aspect=1;
    Vec3 camera, normalCamera, movement, playerTarget, playerCameraOffset;
    Quat rotation, normalRotation;
    bool playerTargetValid=false;
    uint64_t frame=0,updates=0,skipped=0,lastSequence=0,sceneEpoch=0;
    double elapsed=0;
    void* viewport=nullptr;
    std::vector<Object> objects;
    std::set<std::string> selected;
    Json bookmark, sections=Json::array(), commandResult, savedEdits=Json::object();
    Json nativeProbe;
    Json replies=Json::object();std::deque<std::string> replyOrder;
    std::deque<Json> undo,redo;
    std::mutex mutex;
    std::deque<Json> commands;
    std::string published="{}",publishedUi="{}";
    AAssetManager* assets=nullptr;
    bool detached() const { return mode=="camera" || mode=="edit"; }
    bool flyingPlayer() const { return mode=="player"; }
};
extern State state;

const char* libraryName();
const char* gameId();
bool interceptsInput();
Vec3 worldUp();
void* gameDisplay();
void* gameViewport();
void* gameLevel();
Vec3 toWorld(Vec3 local);
Vec3 toLocal(Vec3 world);
void installAdapter();
void beforeFrame(float dt);
void afterFrame();
Json adapterSnapshot();
bool adapterCommand(const Json& command);
void collectObjects();
void validateObject(const std::string& key,Transform transform,Vec3 scale);
void applyObject(const std::string& key,Transform transform,Vec3 scale);
float raycastObject(const Object& object,Vec3 origin,Vec3 direction);
Json objectGeometry(const Object& object);
void replaySavedEdits();
void installEditor();
void installGraphics();
void installInput();
void inputRegions(std::vector<float> rectangles,bool exclusive);
Json inputSnapshot();
Json graphicsSnapshot();
bool editorCommand(const Json& command);
Json editorSnapshot();
void refreshCamera();
void event(const std::string& kind,Json data=Json::object());
void queue(const std::string& text);
std::string published();
std::string publishedUi();
std::string install(AAssetManager* assets,const std::string& files,const std::string& verifiedHash);
void startTransport();
void saveSettings();
void saveJson(const std::string& name,const Json& data);
Json loadJson(const std::string& name,Json fallback);
std::string asset(const std::string& name);
}
