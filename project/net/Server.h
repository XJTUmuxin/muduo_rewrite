#ifndef PROJECT_NET_SERVER_H
#define PROJECT_NET_SERVER_H

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"
#include "project/file/FileNode.h"
#include "project/net/codec.h"
#include "project/net/command.h"
#include <nlohmann/json.hpp>
#include "project/net/FileOperations.h"

#include <set>
#include <map>

namespace project
{
namespace net
{
namespace muduo_net = muduo::net;
namespace fs = std::filesystem;
using json = nlohmann::json;
class Server
{
public:
  Server(muduo_net::EventLoop* loop,const muduo_net::InetAddress& listenAddr,const fs::path& dirPath);
  void setThreadNum(int numThreads);
  void start();

private:
  void onConnection(const muduo_net::TcpConnectionPtr& conn);

  void onStringMessage( const muduo_net::TcpConnectionPtr& conn,
                        const std::string& message,
                        muduo::Timestamp time);
  void onCommandMessage(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);
  
  void onWriteComplete(const muduo_net::TcpConnectionPtr& conn);

  void onDataMessage(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void handleRequestInit(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void handleInit(const muduo_net::TcpConnectionPtr& conn,FileOperations& fileOperations);

  void handleRequestSyn(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void getRemoteFile(const muduo_net::TcpConnectionPtr& conn,const project::file::File& file);

  void postLocalFile(const muduo_net::TcpConnectionPtr& conn, const project::file::File& file);

  void handlePost(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void handleDelete(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void handleMove(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void handleHeartBeat(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  void notifyDelete(const muduo_net::TcpConnectionPtr& conn,const fs::path& filePath);

  void notifyMove(const muduo_net::TcpConnectionPtr& conn,const fs::path& sourcePath,const fs::path tartgetPath);

  void checkHeartBeat();

  typedef std::set<muduo_net::TcpConnectionPtr> ConnectionList;
  muduo_net::TcpServer server_;
  LengthHeaderCodec codec_;
  muduo::MutexLock connMutex_;
  ConnectionList connections_ GUARDED_BY(connMutex_);
  muduo::MutexLock dirMutex_;
  std::shared_ptr<project::file::FileNode> localDir_ GUARDED_BY(dirMutex_);
  fs::path dirPath_;
  muduo::MutexLock offlineDeviceMutex_;
  std::map<int,FileOperations> offlineDeviceToFileOperationsMap_ GUARDED_BY(offlineDeviceMutex_);
  muduo::MutexLock configMutex_;
  json config_ GUARDED_BY(configMutex_);

  muduo_net::TimerId heartBeatCheckTimer_;
};

} 
}


#endif
