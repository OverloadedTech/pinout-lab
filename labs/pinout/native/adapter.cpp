#include "lab.hpp"
#include <GLES2/gl2.h>
#include <fstream>

namespace mlab {
namespace {
struct MeshRecord {
    void* mesh=nullptr;void* body=nullptr;void* table=nullptr;void* vb=nullptr;
    std::string key,name;int first=0,count=0,stride=0,posOffset=0,normalOffset=12;
    bool baked=false,replayed=false;Vec3 scale{1,1,1};Transform original;
    std::vector<Vec3> vertices,normals,collision,collisionNormals;
    std::vector<std::array<uint32_t,3>> triangles;
};
std::map<void*,MeshRecord> meshes;
void (*tickOriginal)(void*);void (*updateOriginal)(void*);void (*menuOriginal)(void*);
void (*loadOriginal)(void*,void*);void (*unloadOriginal)(void*);void (*resetOriginal)(void*);void (*meshUnloadOriginal)(void*);
void (*convexOriginal)(void*,void*,void*);void (*contactsOriginal)(void*,void*,void*);
void (*floorOriginal)(void*,void*);void (*ballContactOriginal)(void*,void*,void*);
void (*insertOriginal)(void*,void*);
void (*bodyTransform)(void*,const Transform*);void (*makeVbo)(void*);
void (*dbvtClear)(void*);void* (*dbvtCreate)(void*,const Vec3*,const Vec3*,void*);
void (*respawn)(void*);void (*levelStart)(void*,int);
bool (*nativeRay)(void*,const Vec3*,const Vec3*,int,Vec3*,Vec3*);
void (*stringCtor)(void*,const char*);void (*stringDtor)(void*);
// QiString is 32 bytes with a nontrivial destructor in both verified 64-bit
// builds. Declaring a nontrivial return type preserves each ABI's hidden-result
// convention when invoking Game::handleCommand.
struct NativeString {
    alignas(8) char data[32]{};
    NativeString(const char* s){stringCtor(data,s);}
    ~NativeString(){stringDtor(data);}
};
NativeString (*gameCommand)(void*,const NativeString&);
void commandGame(const char* command){NativeString input(command);auto result=gameCommand(engine.game(),input);}
std::string previousSections;uint64_t physicsContacts=0,blockedContacts=0;
void* ball() {return ptr(gameLevel(),0x108);}
float origin() {return read<float>(gameLevel(),0x314);}
std::vector<void*> tables() {return gameLevel()?pointers(gameLevel(),0x128,4096):std::vector<void*>{};}
int tableIndex(void* table) {auto all=tables();auto i=std::find(all.begin(),all.end(),table);return i==all.end()?-1:static_cast<int>(i-all.begin());}
std::string sectionName(void* table) {return qiString(at(table,0x140));}
Transform bodyPose(void* b) {auto t=read<Transform>(b,0x158);t.pos=toWorld(t.pos);return t;}
void setBall(Vec3 position) {
    void* b=ball();if(!b) throw std::runtime_error("Start a game before moving the ball");
    auto t=read<Transform>(b,0x158);t.pos=toLocal(position);bodyTransform(b,&t);
    write(b,0x174,Vec3{});write(b,0x180,Vec3{});
    write(gameLevel(),0x484,0.f);write(gameLevel(),0x488,0);
}
bool noBallContact(void* a,void* b=nullptr) {
    bool skip=state.flyingPlayer()&&state.noclip&&(a==ball()||b==ball());
    ++physicsContacts;if(skip) ++blockedContacts;return skip;
}
void convexHook(void* p,void* a,void* b) {if(!noBallContact(a,b)) convexOriginal(p,a,b);}
void contactsHook(void* p,void* a,void* b) {if(!noBallContact(a,b)) contactsOriginal(p,a,b);}
void ballContactHook(void* p,void* a,void* b) {if(!noBallContact(a,b)) ballContactOriginal(p,a,b);}
void floorHook(void* p,void* a) {if(!noBallContact(a)) floorOriginal(p,a);}
void insertHook(void* solver,void* body) {
    if(state.flyingPlayer()&&state.noclip&&body==at(ball(),0x144)) return;
    insertOriginal(solver,body);
}
void tickHook(void* level) {
    if(state.paused) return;
    float old=read<float>(engine.game(),0x150);write(engine.game(),0x150,old*state.simulationSpeed);
    tickOriginal(level);write(engine.game(),0x150,old);
}
void updateHook(void* level) {
    if(state.paused) {++state.skipped;return;}
    ++state.updates;float old=read<float>(engine.game(),0x150),clock=read<float>(level,0x2f8);
    write(engine.game(),0x150,old*state.simulationSpeed);
    updateOriginal(level);write(engine.game(),0x150,old);
    if(state.immortal) write(level,0x2f8,std::max(clock,read<float>(level,0x2f8)));
    if(state.flyingPlayer()&&state.playerTargetValid) setBall(state.playerTarget);
}
void menuHook(void* menu) {if(!state.paused) menuOriginal(menu);}
void resetHook(void* level) {
    ++state.sceneEpoch;
    for(auto& [_,m]:meshes) {applyObject(m.key,m.original,{1,1,1});m.replayed=false;}
    state.objects.clear();state.selected.clear();state.undo.clear();state.redo.clear();
    state.playerTargetValid=false;resetOriginal(level);event("level_reset");
}
void meshUnloadHook(void* mesh) {if(meshes.erase(mesh))++state.sceneEpoch;meshUnloadOriginal(mesh);}
void unloadHook(void* table) {
    ++state.sceneEpoch;
    for(auto it=meshes.begin();it!=meshes.end();) {
        if(it->second.table==table) it=meshes.erase(it);else ++it;
    }
    event("table_unload_bodies",{{"index",tableIndex(table)},{"name",sectionName(table)}});
    unloadOriginal(table);
}
void loadHook(void* mesh,void* stream) {
    bool already=read<uint8_t>(mesh,0x252)!=0;
    void* b=ptr(mesh,8);void* table=ptr(b,8);
    bool baked=!read<uint8_t>(mesh,0x256)&&read<uint8_t>(b,0x194)&&qiString(at(b,0x18)).empty();
    void* vb=baked?at(table,0x180):at(mesh,0x1d0);int first=baked?read<int>(vb,0x20):0;
    void* buffers[]={baked?at(table,0x1b8):at(mesh,0x208),baked?at(table,0x1d8):at(mesh,0x208)};
    int indexStarts[]={baked?read<int>(buffers[0]):0,baked?read<int>(buffers[1]):0};
    loadOriginal(mesh,stream);
    if(already||!b||!table||meshes.count(mesh)) return;
    MeshRecord m;m.mesh=mesh;m.body=b;m.table=table;m.vb=vb;m.first=first;m.baked=baked;
    m.count=read<int>(vb,0x20)-first;m.stride=read<int>(vb,0x28);
    void* format=ptr(vb);m.posOffset=read<int>(format,0x38);m.normalOffset=read<int>(format,0x78);
    if(m.count<1||m.count>1000000||m.stride<24||m.stride>256||m.posOffset<0||m.normalOffset<0||m.normalOffset+12>m.stride) return;
    auto bodies=pointers(table,0xf0);auto it=std::find(bodies.begin(),bodies.end(),b);
    int index=it==bodies.end()?-1:static_cast<int>(it-bodies.begin());
    m.name=qiString(at(b,0x18));
    m.key=sectionName(table)+"@"+std::to_string(tableIndex(table))+"/body/"+std::to_string(index);
    auto t=read<Transform>(b,0x158);m.original=t;m.original.pos=toWorld(t.pos);float offset=read<float>(table,0x170);void* data=ptr(vb,8);
    for(int i=0;i<m.count;++i) {
        Vec3 p=read<Vec3>(data,(first+i)*m.stride+m.posOffset),n=read<Vec3>(data,(first+i)*m.stride+m.normalOffset);
        if(baked) {p.y+=offset;p=t.rot.inverse().rotate(p-t.pos);n=t.rot.inverse().rotate(n);}
        m.vertices.push_back(p);m.normals.push_back(n);
    }
    for(int k=0;k<(baked?2:1);++k) {
        int end=read<int>(buffers[k]);void* indices=ptr(buffers[k],8);
        if(end<indexStarts[k]||end>3000000||(!indices&&end))throw std::runtime_error("Unexpected native index buffer bounds");
        for(int i=indexStarts[k];i+2<end;i+=3) {
            uint32_t x=read<uint16_t>(indices,i*2),y=read<uint16_t>(indices,i*2+2),z=read<uint16_t>(indices,i*2+4);
            if(std::min({x,y,z})<static_cast<uint32_t>(first)||std::max({x,y,z})>=static_cast<uint32_t>(first+m.count))
                throw std::runtime_error("Native render triangle escaped this body's vertex range");
            m.triangles.push_back({x-first,y-first,z-first});
        }
    }
    int count=read<int>(mesh,0x168);void* dataC=ptr(mesh,0x170);
    if(count<0||count>1000000) throw std::runtime_error("Unexpected collision vertex count");
    for(int i=0;i<count;++i) {m.collision.push_back(read<Vec3>(dataC,i*48));m.collisionNormals.push_back(read<Vec3>(dataC,i*48+12));}
    event("mesh_loaded",{{"key",m.key},{"vertices",m.count},{"collision_vertices",count},{"shared_buffer",baked},{"stride",m.stride}});
    meshes.emplace(mesh,std::move(m));
}
MeshRecord* findRecord(const std::string& key) {for(auto& [_,m]:meshes) if(m.key==key) return &m;return nullptr;}
void rebuildCollision(MeshRecord& m) {
    void* vertices=ptr(m.mesh,0x170);
    for(size_t i=0;i<m.collision.size();++i) {
        write(vertices,i*48,m.collision[i].mul(m.scale));Vec3 n=m.collisionNormals[i];
        write(vertices,i*48+12,Vec3{n.x/m.scale.x,n.y/m.scale.y,n.z/m.scale.z}.normalized());
    }
    dbvtClear(at(m.mesh,0x160));int count=read<int>(m.mesh,0x178);void* faces=ptr(m.mesh,0x180);
    for(int i=0;i<count;++i) {
        Bounds bounds;
        for(int k=0;k<3;++k) {unsigned index=read<uint16_t>(faces,i*12+k*2);if(index>=m.collision.size()) throw std::runtime_error("Collision face index out of bounds");bounds.add(read<Vec3>(vertices,index*48));}
        Vec3 lo=bounds.min-Vec3{.001f,.001f,.001f},hi=bounds.max+Vec3{.001f,.001f,.001f};
        dbvtCreate(at(m.mesh,0x160),&lo,&hi,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
}
}
const char* libraryName() {return "libpinout.so";}
const char* gameId() {return "pinout";}
bool interceptsInput() {return true;}
Vec3 worldUp() {return {0,0,1};}
void* gameDisplay() {return ptr(engine.game(),0x10);}
void* gameViewport() {return at(gameDisplay(),8);}
void* gameLevel() {return ptr(engine.game(),0x40);}
Vec3 toWorld(Vec3 local) {local.y+=origin();return local;}
Vec3 toLocal(Vec3 world) {world.y-=origin();return world;}
void installAdapter() {
    bodyTransform=engine.function<decltype(bodyTransform)>("_ZN4Body13setTransform3ERK12QiTransform3");
    makeVbo=engine.function<decltype(makeVbo)>("_ZN14QiVertexBuffer7makeVboEv");
    dbvtClear=engine.function<decltype(dbvtClear)>("_ZN7QiDbvt35clearEv");
    dbvtCreate=engine.function<decltype(dbvtCreate)>("_ZN7QiDbvt36createERK6QiVec3S2_Pv");
    respawn=engine.function<decltype(respawn)>("_ZN5Level7respawnEv");
    levelStart=engine.function<decltype(levelStart)>("_ZN5Level5startEi");
    stringCtor=engine.function<decltype(stringCtor)>("_ZN8QiStringC1EPKc");
    stringDtor=engine.function<decltype(stringDtor)>("_ZN8QiStringD1Ev");
    gameCommand=engine.function<decltype(gameCommand)>("_ZN4Game13handleCommandERK8QiString");
    nativeRay=engine.function<decltype(nativeRay)>("_ZN7Physics7raycastERK6QiVec3S2_iRS0_PS0_");
#define HOOK(n,f,o) engine.hook(n,reinterpret_cast<void*>(f),reinterpret_cast<void**>(&o))
    HOOK("_ZN5Level4tickEv",tickHook,tickOriginal);HOOK("_ZN5Level6updateEv",updateHook,updateOriginal);
    HOOK("_ZN4Menu4tickEv",menuHook,menuOriginal);HOOK("_ZN5Level5resetEv",resetHook,resetOriginal);
    HOOK("_ZN4Mesh12loadGeometryER13QiInputStream",loadHook,loadOriginal);
    HOOK("_ZN4Mesh14unloadGeometryEv",meshUnloadHook,meshUnloadOriginal);
    HOOK("_ZN5Table12unloadBodiesEv",unloadHook,unloadOriginal);
    HOOK("_ZN7Physics10convexBallEP4BodyS1_",convexHook,convexOriginal);
    HOOK("_ZN7Physics16generateContactsEP4BodyS1_",contactsHook,contactsOriginal);
    HOOK("_ZN7Physics19generateBallContactEP4BodyS1_",ballContactHook,ballContactOriginal);
    HOOK("_ZN7Physics20generateFloorContactEP4Body",floorHook,floorOriginal);
    HOOK("tdSolverInsertBody",insertHook,insertOriginal);
#undef HOOK
}
void beforeFrame(float dt) {
    if(state.immortal&&ball()&&read<float>(gameLevel(),0x2f8)<1) write(gameLevel(),0x2f8,60.f);
    if(state.flyingPlayer()&&ball()) {
        if(!state.playerTargetValid) {state.playerTarget=bodyPose(ball()).pos;state.playerTargetValid=true;}
        // PinOut's play surface is XY; the third flight axis is height above it.
        state.playerTarget=state.playerTarget+Vec3{state.movement.x,state.movement.z,state.movement.y}*(state.moveSpeed*dt);
        setBall(state.playerTarget);state.camera=state.playerTarget+state.playerCameraOffset;
    }
}
void afterFrame() {
    if(state.flyingPlayer()&&state.playerTargetValid&&ball()) setBall(state.playerTarget);
    Json active=Json::array();auto all=tables();
    for(size_t i=0;i<all.size();++i) if(read<uint8_t>(all[i],0x2a8)) active.push_back(i);
    std::string value=active.dump();
    if(value!=previousSections) {event("active_tables",{{"active",active},{"ball",ball()?jvec(bodyPose(ball()).pos):Json(nullptr)}});previousSections=value;}
}
Json adapterSnapshot() {
    Json sections=Json::array();int current=-1;void* level=gameLevel();void* currentTable=ptr(level,0x120);auto all=tables();
    for(size_t i=0;i<all.size();++i) {
        void* t=all[i];bool active=read<uint8_t>(t,0x2a8)!=0;int stage=read<int>(t,0x350),bodies=read<int>(t,0xf0);
        if(t==currentTable) current=i;
        sections.push_back({{"index",i},{"name",sectionName(t)},{"current",t==currentTable},{"active",active},
          {"loaded",stage==100},{"stage",stage},{"bodies",bodies},{"retained_base_body",ptr(t,0x340)!=nullptr},
          {"start",read<float>(t,0x170)+origin()},{"length",read<float>(t,0x178)},{"vertices",read<int>(t,0x1a0)}});
    }
    state.sections=sections;long resident=0;std::ifstream mem("/proc/self/statm");long total;mem>>total>>resident;
    return {{"title","PinOut Lab"},{"engine","Qi / Td physics"},{"game_state",read<int>(engine.game(),0x180)},
      {"player",ball()?Json{{"position",jvec(bodyPose(ball()).pos)},{"rotation",jquat(bodyPose(ball()).rot)},{"velocity",jvec(read<Vec3>(ball(),0x174))}}:Json(nullptr)},
      {"time_remaining",read<float>(level,0x2f8)},{"current_section",current},{"origin_y",origin()},{"sections",sections},
      {"mesh_records",meshes.size()},{"contact_queries",physicsContacts},{"noclip_blocked_contacts",blockedContacts},
      {"resident_pages",resident},{"world_axes","X left/right, Y table progression, Z height"},
      {"capabilities",{{"scale_xyz",true},{"rotation_xyz",true},{"player_3d",true},{"sections",true},{"physics_ray",true},{"immortality_label","Unlimited time"}}}};
}
void primePausedRun() {
    // Restart/reset clears the current Table and its buffers. Activation normally
    // happens in the next Level::tick, which the inspection pause suppresses.
    // Run that native loading path once with zero elapsed simulation time.
    if(!state.paused||read<int>(engine.game(),0x180)!=2||tables().empty())return;
    float dt=read<float>(engine.game(),0x150);write(engine.game(),0x150,0.f);
    tickOriginal(gameLevel());write(engine.game(),0x150,dt);
}
bool adapterCommand(const Json& c) {
    std::string op=c.value("op","");
    if(op=="start_run") {
        int current=read<int>(engine.game(),0x180);if(current!=1&&current!=2)throw std::runtime_error("Wait for the main menu before starting a run");
        commandGame(current==2?"level.restart":"level.start 0");
        primePausedRun();state.recenterNextFrame=true;
    }
    else if(op=="return_menu") {commandGame("game.menu");}
    else if(op=="player_begin") {
        if(!ball()||read<int>(engine.game(),0x180)!=2) throw std::runtime_error("Start a game before controlling the ball");
        state.playerTarget=bodyPose(ball()).pos;state.playerTargetValid=true;state.playerCameraOffset=state.camera-state.playerTarget;
    } else if(op=="player_end") {state.playerTargetValid=false;if(ball()) {write(ball(),0x174,Vec3{});write(ball(),0x180,Vec3{});}}
    else if(op=="teleport_player") {
        state.playerTarget=vector(c.at("position"));setBall(state.playerTarget);state.playerTargetValid=state.flyingPlayer();
        if(c.value("stream",true)) tickOriginal(gameLevel());
    } else if(op=="goto_section") {
        auto all=tables();int index=c.at("index");if(index<0||index>=static_cast<int>(all.size())) throw std::runtime_error("Table index outside this run");
        Vec3 position=ball()?bodyPose(ball()).pos:Vec3{0,0,.1f};Vec3 offset=state.normalCamera-position;
        position.y=read<float>(all[index],0x170)+origin()+read<float>(all[index],0x178)*.5f;
        setBall(position);state.playerTarget=position;tickOriginal(gameLevel());
        if(c.value("recenter",true)){state.camera=position+offset;state.rotation=state.normalRotation;}
        state.message="Ball moved to table "+std::to_string(index);
    } else if(op=="reload_level") {if(!gameLevel()||read<int>(engine.game(),0x180)!=2) throw std::runtime_error("Start a game first");resetHook(gameLevel());primePausedRun();state.recenterNextFrame=true;}
    else if(op=="respawn") {if(gameLevel()) respawn(gameLevel());}
    else if(op=="physics_ray") {
        void* physics=ptr(gameLevel(),0x100);if(!physics)throw std::runtime_error("Load a run before querying native collision");
        Vec3 from=toLocal(vector(c.at("from"))),to=toLocal(vector(c.at("to"))),point{},normal{};
        bool hit=nativeRay(physics,&from,&to,c.value("flags",31),&point,&normal);
        state.nativeProbe={{"engine_function","Physics::raycast"},{"hit",hit},{"position",hit?jvec(toWorld(point)):Json(nullptr)},
            {"normal_native",hit?jvec(normal):Json(nullptr)}};
    }
    else return false;
    return true;
}
void collectObjects() {
    state.objects.clear();
    for(auto& [_,m]:meshes) {
        Object o;o.native=m.body;o.key=m.key;o.name=m.name;o.type=read<uint8_t>(m.body,0x194)?"Static body":"Dynamic body";
        o.section=sectionName(m.table)+" @ "+std::to_string(tableIndex(m.table));o.transform=bodyPose(m.body);o.scale=m.scale;
        for(auto p:m.vertices) o.bounds.add(o.transform.pos+o.transform.rot.rotate(p.mul(m.scale)));
        o.editable=true;o.detail={{"native_address",address(m.body)},{"mesh_address",address(m.mesh)},
          {"render_vertices",m.count},{"render_triangles",m.triangles.size()},{"collision_vertices",m.collision.size()},{"collision_faces",read<int>(m.mesh,0x178)},
          {"buffer",m.baked?"shared table VBO":"body VBO"},{"collider",m.collision.empty()?"No per-mesh collider":"native triangle mesh + QiDbvt3"},{"parent","Table"},
          {"motion_note",read<uint8_t>(m.body,0x194)?"Static geometry":"Physics and scripts may move this body when the world runs"}};
        if(state.selected.count(m.key))o.detail["native_render_fnv1a32"]=digest(at(ptr(m.vb,8),m.first*m.stride),m.count*m.stride);
        state.objects.push_back(std::move(o));
    }
}
void validateObject(const std::string& key,Transform target,Vec3 scale) {
    MeshRecord* m=findRecord(key);if(!m) throw std::runtime_error("Object was unloaded; select a currently loaded body");
    if(!ptr(m->vb,8)||read<int>(m->vb,0x20)<m->first+m->count)throw std::runtime_error("The native vertex buffer changed; reload the table");
    if((m->scale-scale).length()>.00001f) {
        int count=read<int>(m->mesh,0x178);void* faces=ptr(m->mesh,0x180);
        if(count<0||count>1000000||(!faces&&count)||read<int>(m->mesh,0x168)!=static_cast<int>(m->collision.size()))
            throw std::runtime_error("The native collision mesh changed; reload the table");
        for(int i=0;i<count;++i)for(int k=0;k<3;++k)
            if(read<uint16_t>(faces,i*12+k*2)>=m->collision.size())throw std::runtime_error("Collision face index out of bounds");
    }
}
void applyObject(const std::string& key,Transform target,Vec3 scale) {
    validateObject(key,target,scale);MeshRecord* m=findRecord(key);
    Transform local=target;local.pos=toLocal(local.pos);bodyTransform(m->body,&local);
    // Keep the authored 2D transform coherent for scripts and the native body's reset path.
    write(m->body,0x48,local.pos.x);write(m->body,0x4c,local.pos.y);
    write(m->body,0x50,std::atan2(2*(local.rot.w*local.rot.z+local.rot.x*local.rot.y),1-2*(local.rot.y*local.rot.y+local.rot.z*local.rot.z)));
    bool scaled=(m->scale-scale).length()>.00001f;m->scale=scale;
    void* data=ptr(m->vb,8);float offset=read<float>(m->table,0x170);
    for(int i=0;i<m->count;++i) {
        Vec3 p=m->vertices[i].mul(scale),n=m->normals[i];n=Vec3{n.x/scale.x,n.y/scale.y,n.z/scale.z}.normalized();
        if(m->baked) {p=local.pos+local.rot.rotate(p);p.y-=offset;n=local.rot.rotate(n);}
        write(data,(m->first+i)*m->stride+m->posOffset,p);write(data,(m->first+i)*m->stride+m->normalOffset,n);
    }
    if(read<unsigned>(m->vb,0x30)) makeVbo(m->vb);
    if(scaled) rebuildCollision(*m);
}
float raycastObject(const Object& o,Vec3 origin,Vec3 direction) {
    if(!shdev::rayBounds(origin,direction,o.bounds)) return INFINITY;
    auto* m=findRecord(o.key);if(!m) return INFINITY;
    Vec3 a=o.transform.rot.inverse().rotate(origin-o.transform.pos),d=o.transform.rot.inverse().rotate(direction);
    float nearest=INFINITY;
    for(auto face:m->triangles)nearest=std::min(nearest,shdev::rayTriangle(a,d,m->vertices[face[0]].mul(m->scale),m->vertices[face[1]].mul(m->scale),m->vertices[face[2]].mul(m->scale)));
    return nearest;
}
Json objectGeometry(const Object& o) {
    auto* m=findRecord(o.key);if(!m)return nullptr;
    Json points=Json::array(),triangles=Json::array(),collisionPoints=Json::array(),collisionTriangles=Json::array();
    for(auto p:m->vertices)points.push_back(jvec(p));for(auto face:m->triangles)triangles.push_back(Json::array({face[0],face[1],face[2]}));
    for(auto p:m->collision)collisionPoints.push_back(jvec(p));
    int count=read<int>(m->mesh,0x178);void* faces=ptr(m->mesh,0x180);
    for(int i=0;i<count;++i) {
        unsigned x=read<uint16_t>(faces,i*12),y=read<uint16_t>(faces,i*12+2),z=read<uint16_t>(faces,i*12+4);
        if(std::max({x,y,z})<m->collision.size())collisionTriangles.push_back(Json::array({x,y,z}));
    }
    return {{"kind","native render triangles"},{"vertices",points},{"triangles",triangles},
            {"collision",{{"vertices",collisionPoints},{"triangles",collisionTriangles}}}};
}
void replaySavedEdits() {
    for(auto& [_,m]:meshes) {
        if(m.replayed||read<int>(m.table,0x350)!=100) continue;
        m.replayed=true;auto it=state.savedEdits.find(m.key);if(it==state.savedEdits.end()) continue;
        auto q=it->at("quaternion");Transform t{vector(it->at("position")),Quat{q.at(0),q.at(1),q.at(2),q.at(3)}.normalized()};
        applyObject(m.key,t,vector(it->at("scale"),100));event("saved_edit_applied",{{"key",m.key}});
    }
}
}
