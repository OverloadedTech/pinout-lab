#include "lab.hpp"
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
namespace mlab {
namespace {
std::atomic<int> clients{0};
void client(int fd) {
    ucred peer{};socklen_t size=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)||
       !(peer.uid==0||peer.uid==2000||peer.uid==getuid())) {close(fd);--clients;return;}
    timeval timeout{30,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    std::string pending;char buffer[4096];
    for(;;) {
        ssize_t n=recv(fd,buffer,sizeof(buffer),0);if(n<=0) break;
        pending.append(buffer,n);if(pending.size()>1024*1024) break;
        size_t end;
        while((end=pending.find('\n'))!=std::string::npos) {
            std::string request=pending.substr(0,end);pending.erase(0,end+1);
            std::string result;
            if(request=="GET") result=published();
            else {queue(request);result="{\"queued\":true}";}
            result+='\n';size_t sent=0;
            while(sent<result.size()) {
                n=send(fd,result.data()+sent,result.size()-sent,MSG_NOSIGNAL);
                if(n<=0) {close(fd);--clients;return;}sent+=n;
            }
        }
    }
    close(fd);--clients;
}
}
void startTransport() {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0) return;
    sockaddr_un a{};a.sun_family=AF_UNIX;std::string name=std::string(gameId())+"_lab";
    memcpy(a.sun_path+1,name.data(),name.size());
    if(bind(fd,reinterpret_cast<sockaddr*>(&a),offsetof(sockaddr_un,sun_path)+1+name.size())||listen(fd,4)) {close(fd);return;}
    std::thread([fd]{for(;;){int c=accept4(fd,nullptr,nullptr,SOCK_CLOEXEC);if(c<0)break;
        if(clients.fetch_add(1)>=8){--clients;close(c);}else std::thread(client,c).detach();}close(fd);}).detach();
}
}
