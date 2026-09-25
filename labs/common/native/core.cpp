#include "lab.hpp"
#include <GLES2/gl2.h>
#include <android/log.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace mlab {
State state;
namespace {
void (*originalFrame)(void*)=nullptr;
void (*originalPosition)(void*,const Vec3*)=nullptr;
void (*originalRotation)(void*,const Quat*)=nullptr;
void (*originalMode)(void*,float,float,float)=nullptr;
void (*originalViewport)(void*,void*)=nullptr;
auto previous=std::chrono::steady_clock::now();
double lastPublish=-1;

bool cameraOverride() {return state.detached()||state.flyingPlayer()||state.mode=="look";}
void positionHook(void* viewport,const Vec3* position) {
    if(viewport==gameViewport()&&read<int>(viewport)==4) {
        state.viewport=viewport;state.normalCamera=toWorld(*position);
        if(!state.cameraReady||!cameraOverride()) state.camera=state.normalCamera;
        if(state.mode=="look") state.camera=state.normalCamera;
        if(cameraOverride()&&state.cameraReady) {Vec3 local=toLocal(state.camera);originalPosition(viewport,&local);return;}
    }
    originalPosition(viewport,position);
}
void rotationHook(void* viewport,const Quat* rotation) {
    if(viewport==gameViewport()&&read<int>(viewport)==4) {
        state.normalRotation=*rotation;
        if(!state.cameraReady||!cameraOverride()) {state.rotation=*rotation;state.cameraReady=true;}
        if(cameraOverride()) {originalRotation(viewport,&state.rotation);return;}
    }
    originalRotation(viewport,rotation);
}
void modeHook(void* viewport,float fov,float near,float far) {
    if(viewport==gameViewport()) {
        state.normalFov=fov;state.nativeNear=near;state.nativeFar=far;
        if(state.fovOverride) fov=state.fov;
        if(cameraOverride()) far=std::max(far,1500.f);
    }
    originalMode(viewport,fov,near,far);
}
void viewportHook(void* renderer,void* viewport) {
    if(viewport==gameViewport()) refreshCamera();
    originalViewport(renderer,viewport);
}
void process(const Json& c) {
    if(c.contains("expected_pid")&&c.at("expected_pid").get<int>()!=getpid())throw std::runtime_error("The game restarted; reconnect the desktop editor");
    if(c.contains("expected_scene_epoch")&&c.at("expected_scene_epoch").get<uint64_t>()!=state.sceneEpoch)throw std::runtime_error("The native scene changed; reload the desktop scene");
    std::string op=c.value("op","");
    if(op=="mode") {
        std::string mode=c.at("value");
        if(mode!="play"&&mode!="tools"&&mode!="camera"&&mode!="edit"&&mode!="player"&&mode!="look")
            throw std::runtime_error("Unknown workspace mode");
        state.movement={};
        if(mode=="camera"||mode=="edit"||mode=="look"||mode=="player") {
            if(!state.cameraReady) throw std::runtime_error("Wait for the original scene to draw");
            if(!cameraOverride()) {state.camera=state.normalCamera;state.rotation=state.normalRotation;}
        }
        if(state.flyingPlayer()&&mode!="player") adapterCommand({{"op","player_end"}});
        if(mode=="player"&&!state.flyingPlayer()) adapterCommand({{"op","player_begin"}});
        state.mode=mode;
        state.paused=mode=="tools"||mode=="camera"||mode=="edit";
        state.message=state.paused?"World paused · editing time will not advance the run":"World running";
        if(mode=="edit") collectObjects();
    } else if(op=="pause") {state.paused=c.at("value");state.movement={};}
    else if(op=="move") state.movement=vector(c.at("value"),1);
    else if(op=="look") {
        if(!cameraOverride()) throw std::runtime_error("Open Camera or Look around first");
        float yaw=std::clamp(c.value("x",0.f),-2.f,2.f)*state.lookSpeed;
        float pitch=std::clamp(c.value("y",0.f),-2.f,2.f)*state.lookSpeed;
        state.rotation=(Quat::axis(worldUp(),-yaw)*state.rotation*Quat::axis({1,0,0},-pitch)).normalized();
    } else if(op=="look_back") {
        if(!cameraOverride()) throw std::runtime_error("Open Camera or Look around first");
        state.rotation=(Quat::axis(worldUp(),PI)*state.rotation).normalized();
    } else if(op=="teleport_camera") {
        state.camera=vector(c.at("position"));state.mode="camera";state.paused=true;
        if(c.contains("rotation")) state.rotation=Quat::euler(vector(c.at("rotation"),360)*(PI/180));
    } else if(op=="bookmark_save") {
        state.bookmark={{"position",jvec(state.camera)},{"rotation",jquat(state.rotation)}};
        saveJson("camera-bookmark.json",state.bookmark);state.message="Camera position saved";
    } else if(op=="bookmark_restore") {
        if(!state.bookmark.is_object()) throw std::runtime_error("Save a camera position first");
        state.camera=vector(state.bookmark.at("position"));auto q=state.bookmark.at("rotation");
        state.rotation=Quat{q.at(0),q.at(1),q.at(2),q.at(3)}.normalized();state.mode="camera";state.paused=true;
    } else if(op=="settings") {
        if(c.contains("fov")) {state.fov=std::clamp(c.at("fov").get<float>(),20.f,150.f);state.fovOverride=true;}
        if(c.contains("fov_override")) state.fovOverride=c.at("fov_override");
        if(c.contains("move_speed")) state.moveSpeed=std::clamp(c.at("move_speed").get<float>(),.1f,600.f);
        if(c.contains("look_speed")) state.lookSpeed=std::clamp(c.at("look_speed").get<float>(),.1f,10.f);
        if(c.contains("simulation_speed")) state.simulationSpeed=std::clamp(c.at("simulation_speed").get<float>(),.05f,10.f);
        if(c.contains("immortal")) state.immortal=c.at("immortal");
        if(c.contains("noclip")) state.noclip=c.at("noclip");
        if(c.contains("fog")) state.fog=c.at("fog");
        saveSettings();
    } else if(op=="refresh") collectObjects();
    else if(!editorCommand(c)&&!adapterCommand(c)) throw std::runtime_error("Unknown lab command: "+op);
}
void publish() {
    Json j=adapterSnapshot();
    long totalPages=0,residentPages=0;std::ifstream memory("/proc/self/statm");memory>>totalPages>>residentPages;
    j.update({{"installed",true},{"game",gameId()},{"pid",getpid()},{"build_id",engine.buildId},
        {"addon_source_sha256",LAB_ADDON_SOURCE_SHA},{"resident_bytes",residentPages*sysconf(_SC_PAGESIZE)},
        {"library_sha256",LAB_LIBRARY_SHA},{"frame",state.frame},{"updates",state.updates},{"skipped_updates",state.skipped},
        {"mode",state.mode},{"paused",state.paused},{"camera_ready",state.cameraReady},
        {"camera",{{"position",jvec(state.camera)},{"rotation",jquat(state.rotation)},
                    {"rotation_degrees",jvec(state.rotation.euler()*(180/PI))}}},
        {"normal_camera",{{"position",jvec(state.normalCamera)},{"rotation",jquat(state.normalRotation)}}},
        {"fov",state.fovOverride?state.fov:state.normalFov},{"fov_override",state.fovOverride},
        {"move_speed",state.moveSpeed},{"look_speed",state.lookSpeed},{"simulation_speed",state.simulationSpeed},
        {"immortal",state.immortal},{"noclip",state.noclip},{"fog",state.fog},
        {"message",state.message},{"error",state.error},{"sequence",state.lastSequence},{"command_result",state.commandResult},
        {"scene_epoch",state.sceneEpoch},{"replies",state.replies},{"native_probe",state.nativeProbe}});
    j.update(editorSnapshot());
    j.update(graphicsSnapshot());
    j.update(inputSnapshot());
    // The Android status strip needs only these changing fields. Parsing the
    // full research snapshot (all objects, campaign sections and command
    // replies) on every UI poll caused excessive GC on older Android devices.
    // Keep the complete snapshot for explicit inspection and desktop clients.
    Json ui=Json::object();
    for(const char* key:{"mode","paused","current_section","error"})
        if(j.contains(key))ui[key]=j[key];
    for(const char* key:{"player","camera"})
        if(j.contains(key)&&j[key].is_object()&&j[key].contains("position"))
            ui[key]={{"position",j[key]["position"]}};
    ui["selected"]=Json::array();
    for(const auto& object:j["selected"])
        if(object.contains("screen_bounds"))
            ui["selected"].push_back({{"screen_bounds",object["screen_bounds"]}});
    std::string full=j.dump(),compact=ui.dump();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.published=std::move(full);state.publishedUi=std::move(compact);
}
void frameHook(void* game) {
    ++state.frame;auto now=std::chrono::steady_clock::now();
    if(state.frame<=12)event("frame_entry",{{"argument",address(game)},{"global",address(engine.game())}});
    float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),0.f,.12f);previous=now;state.elapsed+=dt;
    std::deque<Json> commands;
    {std::lock_guard<std::mutex> lock(state.mutex);commands.swap(state.commands);}
    for(const auto& c:commands) {
        state.lastSequence=c.value("sequence",uint64_t(0));
        try {process(c);state.error.clear();state.commandResult={{"ok",true}};}
        catch(const std::exception& e) {state.error=e.what();state.commandResult={{"ok",false},{"error",state.error}};}
        if(c.contains("request_id")) {
            std::string id=c.at("request_id");state.commandResult["request_id"]=id;state.replies[id]=state.commandResult;
            state.replyOrder.push_back(id);
            if(state.replyOrder.size()>32){state.replies.erase(state.replyOrder.front());state.replyOrder.pop_front();}
        }
        std::string op=c.value("op","");
        if(op!="move"&&op!="look") event("command",{{"command",c},{"result",state.commandResult}});
    }
    if(state.detached())
        state.camera=state.camera+(state.rotation.rotate({state.movement.x,0,-state.movement.z})+
                                  worldUp()*state.movement.y)*(state.moveSpeed*dt);
    try {beforeFrame(dt);}catch(const std::exception& e){state.error=e.what();state.paused=true;}
    originalFrame(game);
    try {afterFrame();replaySavedEdits();}catch(const std::exception& e){state.error=e.what();state.paused=true;}
    if(state.recenterNextFrame&&state.cameraReady) {
        state.recenterNextFrame=false;state.camera=state.normalCamera;state.rotation=state.normalRotation;
        if(state.flyingPlayer()&&state.playerTargetValid)state.playerCameraOffset=state.camera-state.playerTarget;
    }
    if(!commands.empty()||state.elapsed-lastPublish>.2) {
        lastPublish=state.elapsed;
        try {publish();}catch(const std::exception& e){state.error=e.what();}
    }
}
}
void refreshCamera() {
    void* viewport=gameViewport();if(!viewport) return;
    int width=read<int>(viewport,0xc)-read<int>(viewport,4),height=read<int>(viewport,0x10)-read<int>(viewport,8);
    if(width>0&&height>0) state.aspect=static_cast<float>(width)/height;
    if(read<int>(viewport)!=4) return;
    if(state.fovOverride||cameraOverride())
        originalMode(viewport,state.fovOverride?state.fov:state.normalFov,state.nativeNear,
                     cameraOverride()?std::max(1500.f,state.nativeFar):state.nativeFar);
    if(cameraOverride()&&state.cameraReady) {
        Vec3 local=toLocal(state.camera);originalPosition(viewport,&local);originalRotation(viewport,&state.rotation);
    }
}
void queue(const std::string& text) {
    try {
        Json c=Json::parse(text);
        if(!c.is_object()) return;
        std::lock_guard<std::mutex> lock(state.mutex);
        if(state.commands.size()<512) state.commands.push_back(std::move(c));
    }catch(...) {}
}
std::string published() {std::lock_guard<std::mutex> lock(state.mutex);return state.published;}
std::string publishedUi() {std::lock_guard<std::mutex> lock(state.mutex);return state.publishedUi;}
std::string install(AAssetManager* assets,const std::string& files,const std::string& hash) {
    if(state.installed) return {};
    try {
        state.assets=assets;state.files=files+"/mediocre-lab";mkdir(state.files.c_str(),0700);
        engine.load(hash);
        auto settings=loadJson("preferences.json",Json::object());
        state.fov=std::clamp(settings.value("fov",70.f),20.f,150.f);state.fovOverride=settings.value("fov_override",false);
        state.moveSpeed=std::clamp(settings.value("move_speed",6.f),.1f,600.f);
        state.lookSpeed=std::clamp(settings.value("look_speed",1.f),.1f,10.f);
        state.simulationSpeed=std::clamp(settings.value("simulation_speed",1.f),.05f,10.f);
        state.fog=settings.value("fog",true);
        state.bookmark=loadJson("camera-bookmark.json",nullptr);
        // Resolve the complete common integration surface before installing hooks.
        for(const char* name:{"_ZN10QiViewport12setCameraPosERK6QiVec3","_ZN10QiViewport12setCameraRotERK6QiQuat",
            "_ZN10QiViewport9setMode3DEfff","_ZN10QiRenderer11setViewportERK10QiViewport","_ZN4Game5frameEv"})engine.symbol(name);
        installAdapter();installEditor();installGraphics();installInput();
#define HOOK(name,fn,original) engine.hook(name,reinterpret_cast<void*>(fn),reinterpret_cast<void**>(&original))
        HOOK("_ZN10QiViewport12setCameraPosERK6QiVec3",positionHook,originalPosition);
        HOOK("_ZN10QiViewport12setCameraRotERK6QiQuat",rotationHook,originalRotation);
        HOOK("_ZN10QiViewport9setMode3DEfff",modeHook,originalMode);
        HOOK("_ZN10QiRenderer11setViewportERK10QiViewport",viewportHook,originalViewport);
        HOOK("_ZN4Game5frameEv",frameHook,originalFrame);
#undef HOOK
        state.installed=true;startTransport();event("installed",{{"game",gameId()},{"pid",getpid()},{"hash",hash}});
        __android_log_print(ANDROID_LOG_INFO,"MEDIOCRE_LAB","Installed %s",gameId());
        return {};
    }catch(const std::exception& e) {
        state.error=e.what();state.published=Json{{"installed",false},{"error",state.error}}.dump();state.publishedUi=state.published;
        __android_log_print(ANDROID_LOG_ERROR,"MEDIOCRE_LAB","%s",state.error.c_str());return state.error;
    }
}
}
