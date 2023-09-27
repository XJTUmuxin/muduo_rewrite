#ifndef PROJECT_NET_CLIENT_H
#define PROJECT_NET_CLIENT_H

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpClient.h"
#include "project/file/FileNode.h"
#include "project/net/codec.h"
#include "project/net/command.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <filesystem>
#include <project/net/Context.h>

namespace project
{
namespace net
{
namespace muduo_net = muduo::net;
namespace fs = std::filesystem;
using json = nlohmann::json;

class Client{
public:

  Client(muduo_net::EventLoop* loop,const muduo_net::InetAddress& serverAddr,const fs::path& dirPath);

  void connect()
  {
    client_.connect();
  }

  void disconnect()
  {
    client_.disconnect();
  }
private:
  void onConnection(const muduo_net::TcpConnectionPtr& conn);
  
  void onStringMessage( const muduo_net::TcpConnectionPtr&,
                        const std::string&,
                        muduo::Timestamp);
  void onWriteComplete(const muduo_net::TcpConnectionPtr& conn);
  void onCommandMessage(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);
  void onDataMessage(const muduo_net::TcpConnectionPtr& conn, const json& jsonData);

  void handleGet(const muduo_net::TcpConnectionPtr& conn, const json& jsonData);
  
  void handlePost(const muduo_net::TcpConnectionPtr& conn,const json& jsonData);

  muduo_net::TcpClient client_;
  LengthHeaderCodec codec_;
  muduo::MutexLock mutex_;
  muduo_net::TcpConnectionPtr connection_ GUARDED_BY(mutex_);
  muduo::MutexLock dirMutex_;
  std::shared_ptr<project::file::FileNode> localDir_ GUARDED_BY(dirMutex_);
  fs::path dirPath_;
};

}
}
#endif