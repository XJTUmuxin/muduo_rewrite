#include "muduo/base/Logging.h"
#include "muduo/net/TcpServer.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/EventLoop.h"
#include "project/net/Server.h"
#include <cstdlib>
#include <iostream>
using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace project::net;
int main(int argc,char* argv[]){
    uint16_t port;
    if(argc>2){
        port = static_cast<uint16_t>(atoi(argv[1])); 
        EventLoop loop;
        InetAddress serverAddr(port); 
        string path(argv[2]);
        fs::path dirPath(path);
        Server server(&loop,serverAddr,dirPath);
        server.setThreadNum(5);
        server.start();
        loop.loop();
    }
}