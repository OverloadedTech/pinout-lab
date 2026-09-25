#include "lab.hpp"
#include <GLES2/gl2.h>

namespace mlab {
namespace {
void (*sourceOriginal)(GLuint,GLsizei,const GLchar* const*,const GLint*);
void (*elementsOriginal)(GLenum,GLsizei,GLenum,const void*);
void (*arraysOriginal)(GLenum,GLint,GLsizei);
void (*deleteOriginal)(GLuint);
struct Uniforms {GLint fog=-1,bend=-1;};
std::map<GLuint,Uniforms> uniforms;unsigned fogSources=0;uint64_t inspectionDraws=0;
void sourceHook(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths) {
    std::string text;
    for(int i=0;i<count;++i)text.append(strings[i],lengths&&lengths[i]>=0?lengths[i]:std::strlen(strings[i]));
    const std::string before="vFogColor.a = clamp(",after="vFogColor.a = uLabFog * clamp(";
    size_t at=0;bool patched=false;
    while((at=text.find(before,at))!=std::string::npos){text.replace(at,before.size(),after);at+=after.size();patched=true;}
    if(patched) {
        size_t pos=0;if(text.rfind("#version",0)==0){pos=text.find('\n');pos=pos==std::string::npos?text.size():pos+1;}
        text.insert(pos,"\nuniform mediump float uLabFog;\n");
        const char* source=text.c_str();GLint size=text.size();sourceOriginal(shader,1,&source,&size);++fogSources;
    } else sourceOriginal(shader,count,strings,lengths);
}
struct DrawState {bool inspect=false,cull=false,bend=false;GLint location=-1;GLfloat previous[2]{};};
DrawState prepare() {
    DrawState d;d.inspect=state.detached()||state.flyingPlayer()||state.mode=="look";
    if(!fogSources&&!d.inspect)return d;
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);if(!program)return d;
    auto it=uniforms.find(program);
    if(it==uniforms.end())it=uniforms.emplace(program,Uniforms{glGetUniformLocation(program,"uLabFog"),glGetUniformLocation(program,"uBend")}).first;
    if(it->second.fog>=0)glUniform1f(it->second.fog,state.fog?1.f:0.f);
    if(d.inspect){
        ++inspectionDraws;d.cull=glIsEnabled(GL_CULL_FACE);if(d.cull)glDisable(GL_CULL_FACE);
        if(it->second.bend>=0){d.bend=true;d.location=it->second.bend;glGetUniformfv(program,d.location,d.previous);glUniform2f(d.location,0,0);}
    }
    return d;
}
void restore(const DrawState& d){if(d.bend)glUniform2fv(d.location,1,d.previous);if(d.cull)glEnable(GL_CULL_FACE);}
void elementsHook(GLenum mode,GLsizei count,GLenum type,const void* indices){auto saved=prepare();elementsOriginal(mode,count,type,indices);restore(saved);}
void arraysHook(GLenum mode,GLint first,GLsizei count){auto saved=prepare();arraysOriginal(mode,first,count);restore(saved);}
void deleteHook(GLuint program){uniforms.erase(program);deleteOriginal(program);}
}
void installGraphics(){
#define HOOK(n,f,o) engine.hook(n,reinterpret_cast<void*>(f),reinterpret_cast<void**>(&o))
    HOOK("glShaderSource",sourceHook,sourceOriginal);HOOK("glDrawElements",elementsHook,elementsOriginal);
    HOOK("glDrawArrays",arraysHook,arraysOriginal);HOOK("glDeleteProgram",deleteHook,deleteOriginal);
#undef HOOK
}
Json graphicsSnapshot(){return {{"fog_supported",fogSources>0},{"fog_shader_sources",fogSources},{"inspection_draws",inspectionDraws}};}
}
