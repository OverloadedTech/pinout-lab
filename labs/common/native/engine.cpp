#include "lab.hpp"
#include "dobby.h"
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <link.h>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>

namespace mlab {
Engine engine;
namespace {
int inspect(dl_phdr_info* info,size_t,void*) {
    if(!info->dlpi_name||!info->dlpi_name[0])return 0;
    std::string path=info->dlpi_name;
    if(path.substr(path.find_last_of('/')+1)!=libraryName()) return 0;
    engine.base=info->dlpi_addr;
    const ElfW(Dyn)* dynamic=nullptr;
    for(int i=0;i<info->dlpi_phnum;++i) {
        const auto& h=info->dlpi_phdr[i];
        if(h.p_type==PT_DYNAMIC) dynamic=reinterpret_cast<const ElfW(Dyn)*>(engine.base+h.p_vaddr);
        if(h.p_type==PT_NOTE) {
            uintptr_t p=engine.base+h.p_vaddr,end=p+h.p_memsz;
            while(p+sizeof(ElfW(Nhdr))<=end) {
                auto* note=reinterpret_cast<const ElfW(Nhdr)*>(p);p+=sizeof(*note);
                uintptr_t name=p;p+=(note->n_namesz+3)&~3u;
                uintptr_t value=p;p+=(note->n_descsz+3)&~3u;
                if(p>end) break;
                if(note->n_type==NT_GNU_BUILD_ID && note->n_namesz==4 && !memcmp(reinterpret_cast<void*>(name),"GNU",4))
                    for(uint32_t k=0;k<note->n_descsz;++k) {
                        char hex[3];snprintf(hex,sizeof(hex),"%02x",*reinterpret_cast<unsigned char*>(value+k));
                        engine.buildId+=hex;
                    }
            }
        }
    }
    if(!dynamic) return 1;
    const ElfW(Sym)* symbols=nullptr;const char* names=nullptr;
    uintptr_t rel=0,rela=0,jmp=0;size_t relBytes=0,relaBytes=0,jmpBytes=0;long jmpType=DT_RELA;
    auto absolute=[](uintptr_t p){return p<engine.base?engine.base+p:p;};
    for(auto* d=dynamic;d->d_tag!=DT_NULL;++d) switch(d->d_tag) {
        case DT_SYMTAB:symbols=reinterpret_cast<const ElfW(Sym)*>(absolute(d->d_un.d_ptr));break;
        case DT_STRTAB:names=reinterpret_cast<const char*>(absolute(d->d_un.d_ptr));break;
        case DT_REL:rel=absolute(d->d_un.d_ptr);break;
        case DT_RELSZ:relBytes=d->d_un.d_val;break;
        case DT_RELA:rela=absolute(d->d_un.d_ptr);break;
        case DT_RELASZ:relaBytes=d->d_un.d_val;break;
        case DT_JMPREL:jmp=absolute(d->d_un.d_ptr);break;
        case DT_PLTRELSZ:jmpBytes=d->d_un.d_val;break;
        case DT_PLTREL:jmpType=d->d_un.d_val;break;
    }
    if(!symbols||!names) return 1;
    auto add=[&](uintptr_t offset,uintptr_t info){
#if defined(__LP64__)
        unsigned index=ELF64_R_SYM(info),type=ELF64_R_TYPE(info);
#else
        unsigned index=ELF32_R_SYM(info),type=ELF32_R_TYPE(info);
#endif
#if defined(__aarch64__)
        bool slot=type==R_AARCH64_JUMP_SLOT || type==R_AARCH64_GLOB_DAT;
#elif defined(__x86_64__)
        bool slot=type==R_X86_64_JUMP_SLOT || type==R_X86_64_GLOB_DAT;
#else
        bool slot=type==R_ARM_JUMP_SLOT || type==R_ARM_GLOB_DAT;
#endif
        if(index&&slot) engine.slots[names+symbols[index].st_name].push_back(reinterpret_cast<void**>(engine.base+offset));
    };
    auto gather=[&](uintptr_t data,size_t bytes,bool withAddend){
        if(!data) return;
        if(withAddend) {
            auto* r=reinterpret_cast<ElfW(Rela)*>(data);
            for(size_t i=0;i<bytes/sizeof(*r);++i) add(r[i].r_offset,r[i].r_info);
        } else {
            auto* r=reinterpret_cast<ElfW(Rel)*>(data);
            for(size_t i=0;i<bytes/sizeof(*r);++i) add(r[i].r_offset,r[i].r_info);
        }
    };
    gather(rel,relBytes,false);gather(rela,relaBytes,true);gather(jmp,jmpBytes,jmpType==DT_RELA);
    return 1;
}
int protection(void* p) {
    std::ifstream file("/proc/self/maps");std::string line;
    while(std::getline(file,line)) {
        unsigned long long lo,hi;char permissions[5]{};
        if(sscanf(line.c_str(),"%llx-%llx %4s",&lo,&hi,permissions)==3 &&
           reinterpret_cast<uintptr_t>(p)>=lo && reinterpret_cast<uintptr_t>(p)<hi)
            return (permissions[0]=='r'?PROT_READ:0)|(permissions[1]=='w'?PROT_WRITE:0)|(permissions[2]=='x'?PROT_EXEC:0);
    }
    throw std::runtime_error("Cannot identify original relocation page permissions");
}
}
void Engine::load(const std::string& hash) {
    if(hash!=LAB_LIBRARY_SHA) throw std::runtime_error("Unsupported native library hash");
    library=dlopen(libraryName(),RTLD_NOW|RTLD_NOLOAD);
    if(!library) throw std::runtime_error("Original game library is not loaded");
    dl_iterate_phdr(inspect,nullptr);
    if(!base) throw std::runtime_error("Original mapped library not found");
    global=reinterpret_cast<void**>(symbol("gGame"));
}
void* Engine::symbol(const char* name,bool required) {
    void* result=dlsym(library,name);
    if(!result&&required) throw std::runtime_error(std::string("Missing native symbol: ")+name);
    return result;
}
void Engine::hook(const char* name,void* replacement,void** original) {
    void* target=symbol(name);*original=target;
    auto found=slots.find(name);
    if(found==slots.end()||found->second.empty()) {
        auto hex=[](void* p,int n){std::string result;auto* data=reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(p)&~uintptr_t(1));for(int i=0;i<n;++i){char s[3];snprintf(s,3,"%02x",data[i]);result+=s;}return result;};
        auto before=hex(target,16);
        if(DobbyHook(target,replacement,original)!=0) throw std::runtime_error(std::string("Inline hook failed: ")+name);
#if defined(__arm__)
        if((reinterpret_cast<uintptr_t>(target)^reinterpret_cast<uintptr_t>(*original))&1) {
            DobbyDestroy(target);
            throw std::runtime_error(std::string("Hook changed the ARM instruction mode: ")+name);
        }
#endif
        event("hook",{{"symbol",name},{"method","inline"},{"target",address(target)},{"trampoline",address(*original)},
                     {"original_bytes",before},{"trampoline_bytes",hex(*original,32)}});
        return;
    }
    size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
    for(void** slot:found->second) {
        void* start=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot)&~(page-1));
        int old=protection(slot);
        if(mprotect(start,page,old|PROT_WRITE)) throw std::runtime_error("Cannot write relocation page");
        __atomic_store_n(slot,replacement,__ATOMIC_RELEASE);
        if(mprotect(start,page,old)) throw std::runtime_error("Cannot restore relocation page permissions");
    }
    event("hook",{{"symbol",name},{"method","relocation"},{"slots",found->second.size()}});
}
std::string address(void* p) { char s[40];snprintf(s,sizeof(s),"%p",p);return s; }
std::string digest(const void* data,size_t bytes) {
    uint32_t hash=2166136261u;auto* p=static_cast<const unsigned char*>(data);
    if(!p&&bytes)return "unavailable";
    for(size_t i=0;i<bytes;++i)hash=(hash^p[i])*16777619u;
    char value[9];snprintf(value,sizeof(value),"%08x",hash);return value;
}
Vec3 vector(const Json& j,float limit) {
    if(!j.is_array()||j.size()!=3) throw std::runtime_error("Expected three coordinates");
    Vec3 v{j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()};
    if(!v.finite()||std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>limit)
        throw std::runtime_error("Coordinates are outside the supported numeric range");
    return v;
}
std::string qiString(void* p) {
    if(!p) return {};
    const char* text=read<const char*>(p);
    if(!text) text=static_cast<char*>(p)+(sizeof(void*)==8?16:12);
    return std::string(text,strnlen(text,4096));
}
std::vector<void*> pointers(void* owner,size_t off,int cap) {
    int n=read<int>(owner,off);void* data=ptr(owner,off+8);
    if(n<0||n>cap||(!data&&n)) throw std::runtime_error("Unexpected native array bounds");
    std::vector<void*> result;result.reserve(n);
    for(int i=0;i<n;++i) result.push_back(ptr(data,i*sizeof(void*)));
    return result;
}
}
