#include "lab.hpp"
#include <limits>
#include <unistd.h>

namespace mlab {
namespace {
struct Drag { bool active=false; Vec3 anchor,normal; Json before; } drag;
Object* find(const std::string& key) {
    for(auto& o:state.objects) if(o.key==key) return &o;
    return nullptr;
}
Json pose(const Object& o) {
    return {{"position",jvec(o.transform.pos)},{"quaternion",jquat(o.transform.rot)},{"scale",jvec(o.scale)}};
}
Transform transform(const Json& j) {
    auto q=j.at("quaternion");
    return {vector(j.at("position")),Quat{q.at(0),q.at(1),q.at(2),q.at(3)}.normalized()};
}
Json capture() {
    Json result=Json::object();
    for(const auto& key:state.selected) if(auto* o=find(key)) if(o->editable) result[key]=pose(*o);
    return result;
}
void remember(Json before) {
    if(before.empty()) return;
    state.undo.push_back(before);if(state.undo.size()>40) state.undo.pop_front();state.redo.clear();
}
void apply(const Json& poses) {
    // Reject unsupported transforms for the entire group before touching any
    // native body, vertex buffer, or collider.
    for(auto it=poses.begin();it!=poses.end();++it)
        if(find(it.key())) validateObject(it.key(),transform(it.value()),vector(it.value().at("scale"),100));
    for(auto it=poses.begin();it!=poses.end();++it)
        if(find(it.key())) applyObject(it.key(),transform(it.value()),vector(it.value().at("scale"),100));
    collectObjects();
}
Vec3 viewPosition() {return state.detached()||state.flyingPlayer()||state.mode=="look"?state.camera:state.normalCamera;}
Quat viewRotation() {return state.detached()||state.flyingPlayer()||state.mode=="look"?state.rotation:state.normalRotation;}
Vec3 ray(float x,float y) {
    float tangent=std::tan((state.fovOverride?state.fov:state.normalFov)*PI/360);
    return viewRotation().rotate({(x*2-1)*tangent,(1-y*2)*tangent/state.aspect,-1}).normalized();
}
Json project(Vec3 point) {
    Vec3 p=viewRotation().inverse().rotate(point-viewPosition());
    if(p.z>=-.01f) return nullptr;
    float t=std::tan((state.fovOverride?state.fov:state.normalFov)*PI/360);
    return Json::array({.5f+.5f*p.x/(-p.z*t),.5f-.5f*p.y*state.aspect/(-p.z*t)});
}
Object* pick(float x,float y) {
    Vec3 origin=viewPosition(),direction=ray(x,y);float closest=std::numeric_limits<float>::infinity();Object* hit=nullptr;
    for(auto& o:state.objects) {
        if(!o.bounds.valid()) continue;
        float distance=raycastObject(o,origin,direction);
        if(distance>=0&&distance<closest) {closest=distance;hit=&o;}
    }
    return hit;
}
Vec3 planePoint(float x,float y) {
    Vec3 d=ray(x,y),p=viewPosition();float denominator=d.dot(drag.normal);
    if(std::abs(denominator)<.0001f) throw std::runtime_error("Drag direction is parallel to the editing plane");
    float t=(drag.anchor-p).dot(drag.normal)/denominator;
    if(t<0||t>10000) throw std::runtime_error("Object is behind the editing camera");
    return p+d*t;
}
void requirePaused() {
    if(!state.paused) throw std::runtime_error("Pause the world before editing geometry");
}
}
void installEditor() {state.savedEdits=loadJson("level-edits.json",Json::object());}
bool editorCommand(const Json& c) {
    std::string op=c.value("op","");
    if(op=="select"||op=="pick"||op=="drag_begin") {
        collectObjects();Object* object=nullptr;
        if(c.contains("key")) object=find(c.at("key"));
        else object=pick(c.at("x"),c.at("y"));
        if(!object) {if(!c.value("add",false)) state.selected.clear();return true;}
        if(!c.value("add",false)&&!(op=="drag_begin"&&state.selected.count(object->key))) state.selected.clear();
        if(c.value("toggle",false)&&state.selected.count(object->key)) state.selected.erase(object->key);
        else state.selected.insert(object->key);
        state.message=object->name.empty()?object->key:object->name;
        if(op=="drag_begin") {
            requirePaused();drag.active=true;drag.normal=viewRotation().rotate({0,0,-1});
            drag.anchor=object->bounds.center();drag.before=capture();
            drag.anchor=planePoint(c.at("x"),c.at("y"));
        }
    } else if(op=="select_all") {
        collectObjects();state.selected.clear();
        std::string section=c.value("section","");
        for(auto& o:state.objects) if(o.editable&&(section.empty()||o.section==section)) state.selected.insert(o.key);
    } else if(op=="clear_selection") {state.selected.clear();}
    else if(op=="drag") {
        requirePaused();if(!drag.active) return true;
        Vec3 delta=planePoint(c.at("x"),c.at("y"))-drag.anchor;
        float snap=c.value("snap",0.f);
        if(snap>0) delta={std::round(delta.x/snap)*snap,std::round(delta.y/snap)*snap,std::round(delta.z/snap)*snap};
        Json result=drag.before;
        for(auto& p:result) p["position"]=jvec(vector(p.at("position"))+delta);
        apply(result);
    } else if(op=="drag_end") {
        if(drag.active) {remember(drag.before);drag.active=false;event("edit_drag",{{"objects",state.selected.size()}});}
    } else if(op=="transform") {
        requirePaused();collectObjects();auto before=capture();
        if(before.empty()) throw std::runtime_error("Select an editable object first");
        Json result=before;Vec3 center;for(auto& p:before) center=center+vector(p.at("position"));center=center/static_cast<float>(before.size());
        Vec3 delta=c.contains("move")?vector(c.at("move")):Vec3{};
        Vec3 angles=c.contains("rotate")?vector(c.at("rotate"),360)*(PI/180):Vec3{};
        Quat rotation=Quat::euler(angles);Vec3 scale=c.contains("scale_by")?vector(c.at("scale_by"),100):Vec3{1,1,1};
        for(auto& p:result) {
            Transform t=transform(p);t.pos=center+rotation.rotate((t.pos-center).mul(scale))+delta;t.rot=(rotation*t.rot).normalized();
            Vec3 s=vector(p.at("scale")).mul(scale);
            if(c.contains("position")) {if(result.size()!=1) throw std::runtime_error("Use Move for a group; absolute position requires one object");t.pos=vector(c.at("position"));}
            if(c.contains("rotation")) t.rot=Quat::euler(vector(c.at("rotation"),360)*(PI/180));
            if(c.contains("scale")) s=vector(c.at("scale"),100);
            if(std::min({s.x,s.y,s.z})<.02f) throw std::runtime_error("Scale must be positive and at least 0.02");
            p={{"position",jvec(t.pos)},{"quaternion",jquat(t.rot)},{"scale",jvec(s)}};
        }
        apply(result);remember(before);event("edit_transform",{{"objects",result}});
    } else if(op=="apply_poses") {
        requirePaused();collectObjects();const auto& poses=c.at("poses");
        if(!poses.is_object()||poses.empty()||poses.size()>4096)throw std::runtime_error("Provide between 1 and 4096 native object poses");
        Json before=Json::object();
        for(auto it=poses.begin();it!=poses.end();++it) {
            auto* o=find(it.key());if(!o||!o->editable)throw std::runtime_error("An edited object is no longer loaded: "+it.key());
            Vec3 scale=vector(it->at("scale"),100);if(std::min({scale.x,scale.y,scale.z})<.02f)throw std::runtime_error("Scale must be at least 0.02");
            before[it.key()]=pose(*o);
        }
        apply(poses);remember(before);state.selected.clear();
        for(auto it=poses.begin();it!=poses.end();++it)state.selected.insert(it.key());
        event("edit_apply_poses",{{"count",poses.size()}});
    } else if(op=="undo"||op=="redo") {
        requirePaused();collectObjects();auto& from=op=="undo"?state.undo:state.redo;auto& to=op=="undo"?state.redo:state.undo;
        if(from.empty()) throw std::runtime_error("No edit to "+op);
        Json before=from.back(),current=Json::object();
        for(auto it=before.begin();it!=before.end();++it) if(auto* o=find(it.key())) current[it.key()]=pose(*o);
        apply(before);from.pop_back();to.push_back(current);
    } else if(op=="save_edits") {
        requirePaused();collectObjects();
        auto selected=capture();
        for(auto it=selected.begin();it!=selected.end();++it) state.savedEdits[it.key()]=it.value();
        saveJson("level-edits.json",state.savedEdits);
        state.message="Saved "+std::to_string(selected.size())+" selected objects for future runs";
    } else if(op=="clear_saved_edits") {
        state.savedEdits=Json::object();saveJson("level-edits.json",state.savedEdits);
        state.message="Saved overrides cleared. Reload the level to restore original geometry.";
    } else if(op=="export_scene") {
        collectObjects();Json objects=Json::array();
        for(auto& o:state.objects) {auto j=pose(o);j.update({{"key",o.key},{"name",o.name},{"type",o.type},{"section",o.section},{"detail",o.detail},
            {"editable",o.editable},{"geometry",objectGeometry(o)}});objects.push_back(j);}
        saveJson("scene.json",{{"format","mediocre-native-scene"},{"version",1},{"game",gameId()},{"library_sha256",LAB_LIBRARY_SHA},
            {"pid",getpid()},{"scene_epoch",state.sceneEpoch},{"objects",objects},{"sections",state.sections},
            {"camera",{{"position",jvec(viewPosition())},{"quaternion",jquat(viewRotation())},{"horizontal_fov",state.fovOverride?state.fov:state.normalFov}}}});
        state.message="Native scene data exported to the Lab files folder";
    } else return false;
    return true;
}
Json editorSnapshot() {
    // Loading and physics can replace or move native bodies outside Edit mode.
    // Never publish a stale object list after a reload or streaming transition.
    collectObjects();
    Json selected=Json::array(),items=Json::array();
    for(auto& o:state.objects) {
        Json j={{"key",o.key},{"name",o.name},{"type",o.type},{"section",o.section},{"editable",o.editable}};
        items.push_back(j);
        if(!state.selected.count(o.key)) continue;
        j.update(pose(o));j["rotation_degrees"]=jvec(o.transform.rot.euler()*(180/PI));j["detail"]=o.detail;
        j["bounds"]={{"min",jvec(o.bounds.min)},{"max",jvec(o.bounds.max)}};
        Json corners=Json::array();
        for(int i=0;i<8;++i) corners.push_back(project({(i&1)?o.bounds.max.x:o.bounds.min.x,(i&2)?o.bounds.max.y:o.bounds.min.y,(i&4)?o.bounds.max.z:o.bounds.min.z}));
        j["screen_bounds"]=corners;selected.push_back(j);
    }
    return {{"objects",items},{"object_count",items.size()},{"selected",selected},{"saved_edit_count",state.savedEdits.size()},
            {"undo_count",state.undo.size()},{"redo_count",state.redo.size()},{"aspect",state.aspect}};
}
}
