#include "muduo/base/Logging.h"
#include "project/net/Client.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/EventLoop.h"
#include <cstdlib>
using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace project::net;
int main(int argc,char* argv[]){
    uint16_t port;
    if(argc>3){
        EventLoop loop;
        port = static_cast<uint16_t>(atoi(argv[2])); 
        InetAddress serverAddr(argv[1],port); 
        string path(argv[3]);
        fs::path dirPath(path);
        Client client(&loop,serverAddr,dirPath);
        client.connect();
        loop.loop();
    }
}