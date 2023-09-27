#include "project/net/Client.h"
#include <cassert>
using namespace std;
using namespace project;
using namespace project::file;
using namespace project::net;
using namespace muduo;
using namespace muduo::net;


Client::Client(EventLoop* loop,const InetAddress& serverAddr,const fs::path& dirPath)
  : client_(loop,serverAddr,"Client"),
    codec_(std::bind(&Client::onStringMessage, this, _1, _2, _3)),
    localDir_(new FileNode(dirPath)),
    dirPath_(dirPath)
{
  client_.setConnectionCallback(  
    std::bind(&Client::onConnection, this, _1));
  client_.setMessageCallback(
    std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  client_.setWriteCompleteCallback(
    std::bind(&Client::onWriteComplete, this, _1));
  client_.enableRetry();
}


void Client::onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO  << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  {
    MutexLockGuard lock(mutex_);
    if (conn->connected())
    {
      connection_ = conn;

      connection_->setContext(ContextPtr(new Context()));

      json jsonData;
      jsonData["type"] = "command";
      jsonData["command"] = REQUESTSYN;
      {
        MutexLockGuard lock2(dirMutex_);
        jsonData["content"] = localDir_->serialize();
      }
      string message = jsonData.dump();

      codec_.send(get_pointer(connection_),message);
    }
    else
    {
      connection_.reset();
    }
  }

}

void Client::onStringMessage(const TcpConnectionPtr& conn,
    const string& message,Timestamp timeStamp)
{
  json jsonData = json::parse(message);
  if(jsonData["type"] == "command"){
    onCommandMessage(conn, jsonData);
  }
  else if(jsonData["type"] == "data"){
    onDataMessage(conn, jsonData);
  }
}

void Client::onWriteComplete(const TcpConnectionPtr& conn)
{
  continueTransferFile(conn,codec_);
}

void Client::onDataMessage(const TcpConnectionPtr& conn, const json& jsonData){
  fs::path filePath(jsonData["path"]);
  size_t fileSize = jsonData["size"];
  string content = std::move(base64_decode(jsonData["content"]));
  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
  
  ReceiveContextPtr& receiveContextPtr = contextPtr->receiveContextMap[dirPath_/filePath];
  receiveContextPtr->write(content.data(),content.size());
  if(receiveContextPtr->isWriteComplete(fileSize)){

    contextPtr->receiveContextMap.erase(dirPath_ / filePath);

    fs::path realFilePath = dirPath_/filePath;
    fs::path tempFilePath = fs::path(realFilePath.string()+".downtemp");

    time_t modifyTime = jsonData["mTime"];

    std::chrono::system_clock::time_point sysTimePoint = std::chrono::system_clock::from_time_t(modifyTime);
    std::chrono::time_point<std::chrono::file_clock> fileTimePoint = std::chrono::file_clock::from_sys(sysTimePoint);

    fs::last_write_time(tempFilePath,fileTimePoint);

    if(fs::exists(realFilePath)){
      fs::remove(realFilePath);
    }

    fs::rename(tempFilePath,realFilePath);


    LOG_INFO << "Received file "<<filePath<<" successfully";
    {
      MutexLockGuard lock(dirMutex_);
      localDir_->addFile(filePath,false,modifyTime);
    }
  }
}

void Client::onCommandMessage(const TcpConnectionPtr& conn,const json& jsonData){
  int command = jsonData["command"];
  switch (command)
  {
  case GET:
    handleGet(conn,jsonData["content"]);
    break;
  case POST:
    handlePost(conn,jsonData["content"]);
    break;
  case DELETE:
    break;
  case MOVE:
    break;
  default:
    LOG_INFO<<"invaild command: "<<command;
    break;
  }
}

void Client::handleGet(const TcpConnectionPtr& conn, const json& jsonData)
{
  fs::path filePath = jsonData["path"];
  LOG_INFO << "Remote Get file " << filePath;
  transferFile(conn,dirPath_,filePath,codec_);
}

void Client::handlePost(const TcpConnectionPtr& conn,const json& jsonData)
{
  bool isDir = jsonData["isDir"];
  fs::path filePath = jsonData["path"];
  if(isDir){
    try{
      fs::create_directory(dirPath_ / filePath);
      LOG_INFO << "Folder " << (dirPath_ / filePath) << " created successfully";
      {
        MutexLockGuard lock(dirMutex_);
        localDir_->addFile(filePath,true,std::time(nullptr));
      }
    }
    catch(const exception& e){
      LOG_ERROR << "Error creating folder " << (dirPath_ / filePath); 
    }
  }
  else{
    const std::any& context = conn->getContext();
    assert(context.has_value() && context.type() == typeid(ContextPtr));
    const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);

    fs::path tempFilePath = dirPath_ / fs::path(filePath.string()+".downtemp");

    ReceiveContextPtr receiveContextPtr(new ReceiveContext(tempFilePath));
    if(receiveContextPtr->isOpen()){
      contextPtr->receiveContextMap[(dirPath_/filePath).string()] = receiveContextPtr;
      LOG_INFO << "Receiving file "<<filePath;
    }
    else{
      LOG_ERROR << "Creating "<<filePath<<" error";
    }
  }
}