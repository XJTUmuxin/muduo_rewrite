#include "muduo/base/Logging.h"
#include "muduo/base/AsyncLogging.h"
#include "muduo/base/LogFile.h"
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

static const off_t kRollSize = 1*1024*1024;
AsyncLogging* g_asyncLog = NULL;

inline AsyncLogging* getAsyncLog()
{
    return g_asyncLog;
}

void asyncLog(const char* msg, int len)
{
    AsyncLogging* logging = getAsyncLog();
    if (logging)
    {
        logging->append(msg, len);
    }
}


int main(int argc,char* argv[]){
    uint16_t port;
    if(argc>2){
        AsyncLogging log(::basename(argv[0]), kRollSize);

        g_asyncLog = &log;

        Logger::setOutput(asyncLog);

        Logger::setLogLevel(Logger::DEBUG);

        log.start();

        LOG_INFO << "logging started";

        port = static_cast<uint16_t>(atoi(argv[1])); 
        EventLoop loop;
        InetAddress serverAddr(port); 
        string path(argv[2]);
        fs::path dirPath(path);
        Server server(&loop,serverAddr,dirPath);
        server.setThreadNum(5);
        server.start();
        loop.loop();

        log.stop();
        return 0;
    }
}