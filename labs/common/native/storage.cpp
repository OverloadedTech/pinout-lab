#include "lab.hpp"
#include <android/log.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace mlab {
std::string asset(const std::string& name) {
    AAsset* a=AAssetManager_open(state.assets,name.c_str(),AASSET_MODE_BUFFER);
    if(!a) throw std::runtime_error("Missing lab asset: "+name);
    std::string result(static_cast<const char*>(AAsset_getBuffer(a)),AAsset_getLength(a));
    AAsset_close(a);return result;
}
Json loadJson(const std::string& name,Json fallback) {
    std::ifstream in(state.files+"/"+name);
    if(!in) return fallback;
    try { return Json::parse(in); }
    catch(const std::exception& e) { event("saved_data_error",{{"file",name},{"error",e.what()}});return fallback; }
}
void saveJson(const std::string& name,const Json& data) {
    std::string path=state.files+"/"+name,tmp=path+".tmp",text=data.dump(2)+"\n";
    int fd=open(tmp.c_str(),O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);
    if(fd<0) throw std::runtime_error("Cannot create saved lab data");
    size_t done=0;
    while(done<text.size()) {
        ssize_t n=::write(fd,text.data()+done,text.size()-done);
        if(n<=0) {close(fd);throw std::runtime_error("Cannot write saved lab data");}done+=n;
    }
    int result=fsync(fd);close(fd);
    if(result||rename(tmp.c_str(),path.c_str())) throw std::runtime_error("Cannot commit saved lab data");
}
void event(const std::string& kind,Json data) {
    if(state.files.empty()) return;
    data["event"]=kind;data["frame"]=state.frame;data["elapsed"]=state.elapsed;
    auto now=std::chrono::system_clock::now().time_since_epoch();
    data["time_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::string path=state.files+"/events.jsonl";
    struct stat status{};
    if(!stat(path.c_str(),&status)&&status.st_size>8*1024*1024) {
        rename((path+".1").c_str(),(path+".2").c_str());rename(path.c_str(),(path+".1").c_str());
    }
    std::ofstream out(path,std::ios::app);out<<data.dump()<<'\n';
}
void saveSettings() {
    saveJson("preferences.json",{{"fov",state.fov},{"fov_override",state.fovOverride},{"move_speed",state.moveSpeed},
        {"look_speed",state.lookSpeed},{"simulation_speed",state.simulationSpeed},{"fog",state.fog}});
}
}
