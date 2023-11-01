#include "muduo/base/Logging.h"
#include "muduo/base/AsyncLogging.h"
#include "project/net/Client.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/EventLoop.h"
#include <cstdlib>
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
    if(argc>3){
        Logger::setLogLevel(Logger::DEBUG);
        if(argc>4){
            if(strcmp(argv[3],"filelog")==0){
                AsyncLogging log(::basename(argv[0]), kRollSize);

                g_asyncLog = &log;

                Logger::setOutput(asyncLog);

                log.start();

                LOG_INFO << "logging started";

                EventLoop loop;
                port = static_cast<uint16_t>(atoi(argv[2])); 
                InetAddress serverAddr(argv[1],port); 
                string path(argv[3]);
                fs::path dirPath(path);
                Client client(&loop,serverAddr,dirPath);
                client.connect();
                loop.loop();

                log.stop();
            }
        }
        else{
            EventLoop loop;
            port = static_cast<uint16_t>(atoi(argv[2])); 
            InetAddress serverAddr(argv[1],port); 
            string path(argv[3]);
            fs::path dirPath(path);
            Client client(&loop,serverAddr,dirPath);
            client.connect();
            loop.loop();
        }
        return 0;
    }
}