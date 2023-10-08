#include "project/net/Server.h"
#include "project/net/Context.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <utime.h>

using namespace std;
using namespace project;
using namespace project::file;
using namespace project::net;
using namespace muduo;
using namespace muduo::net;

typedef std::shared_ptr<FILE> FilePtr;




Server::Server(EventLoop* loop,const InetAddress& listenAddr,const fs::path& dirPath)
  : server_(loop,listenAddr,"Server"),
    codec_(std::bind(&Server::onStringMessage, this, _1,_2,_3)),
    localDir_(new FileNode(dirPath)),
    dirPath_(dirPath)
{
  server_.setConnectionCallback(
    std::bind(&Server::onConnection,this,_1));
  server_.setMessageCallback(
    std::bind(&LengthHeaderCodec::onMessage,&codec_,_1,_2,_3));
  server_.setWriteCompleteCallback(
    std::bind(&Server::onWriteComplete, this, _1));
}

void Server::setThreadNum(int numThreads){
  server_.setThreadNum(numThreads);
}

void Server::start(){
  server_.start();
}

void Server::onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO  << conn->peerAddress().toIpPort() << " -> "
            << conn->localAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  MutexLockGuard lock(connMutex_);
  if (conn->connected())
  {
    connections_.insert(conn);
    conn->setContext(ContextPtr(new Context()));
  }
  else
  {
    connections_.erase(conn);
  }
}

void Server::onStringMessage( const TcpConnectionPtr& conn,
                      const string& message,
                      Timestamp)
{
  json jsonData = json::parse(message);
  if(jsonData["type"] == "command"){
    onCommandMessage(conn, jsonData);
  }
  else if(jsonData["type"] == "data"){
    onDataMessage(conn, jsonData);
  }
}

void Server::onCommandMessage(const TcpConnectionPtr& conn,const json& jsonData){
  int command = jsonData["command"];
  switch (command)
  {
  case REQUESTSYN:
    handleRequestSyn(conn, jsonData["content"]);
    break;
  case DELETE:
    break;
  case MOVE:
    break;
  case POST:
    handlePost(conn,jsonData["content"]);
    break;
  default:
    LOG_INFO<<"invaild command: "<<command;
    break;
  }
}

void Server::onWriteComplete(const TcpConnectionPtr& conn)
{
  continueTransferFile(conn,codec_);
}

void Server::onDataMessage(const TcpConnectionPtr& conn, const json& jsonData){
  fs::path filePath(jsonData["path"]);
  size_t fileSize = jsonData["size"];
  string content = std::move(base64_decode(jsonData["content"]));

  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
  
  ReceiveContextPtr& receiveContextPtr = contextPtr->receiveContextMap[dirPath_ / filePath];

  receiveContextPtr->write(content.data(),content.size());

  LOG_INFO<<"Receiving file "<<filePath<<" package no "<<receiveContextPtr->getPackNo();

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
    {
      MutexLockGuard lock(connMutex_);
      for(auto &connPtr:connections_){
        if(connPtr == conn){
          continue;
        }
        File file(filePath,false);
        EventLoop::Functor f = std::bind(&Server::postLocalFile, this, connPtr, file);
        EventLoop* loop =  connPtr->getLoop();
        loop->queueInLoop(f);
      }
    }
  }
}

void Server::handleRequestSyn(const TcpConnectionPtr& conn, const json& jsonData){
  LOG_INFO <<"Remote request syn";

  FileNode remoteDir(jsonData);
  DiffSets diffSets;
  {
    MutexLockGuard lock(dirMutex_);
    localDir_->compare(remoteDir,diffSets,"");
  }
  diffSets.printDiffSets();
  for(auto &remoteAddFile:diffSets.remoteAddSet){
    getRemoteFile(conn, remoteAddFile);
  }
  for(auto &localAddFile:diffSets.localAddSet){
    postLocalFile(conn, localAddFile);
  }
  for(auto &newFile:diffSets.newSet){
    getRemoteFile(conn, newFile);
  }
  for(auto &oldFile:diffSets.oldSet){
    postLocalFile(conn, oldFile);
  }
}

void Server::getRemoteFile(const TcpConnectionPtr& conn, const File& file){
  if(file.isDir){
    try{
      fs::create_directory(dirPath_ / file.filePath);
      LOG_INFO << "Folder " << (dirPath_ / file.filePath) << " created successfully";
      {
        MutexLockGuard lock(dirMutex_);
        localDir_->addFile(file.filePath,true,std::time(nullptr));
      }
      {
        MutexLockGuard lock(connMutex_);
        for(auto &connPtr:connections_){
          if(connPtr == conn){
            continue;
          }
          EventLoop::Functor f = std::bind(&Server::postLocalFile, this, connPtr, file);
          EventLoop* loop =  connPtr->getLoop();
          loop->queueInLoop(f);
        }
      }
    }
    catch(const exception& e){
      LOG_ERROR << "Error creating folder " << (dirPath_ / file.filePath); 
    } 
  }
  else{
    json jsonData;
    jsonData["type"] = "command";
    jsonData["command"] = GET;
    jsonData["content"]["path"] = file.filePath.string();
    string message = jsonData.dump();
    if(conn){
      codec_.send(get_pointer(conn),message);
    }
    
    const std::any& context = conn->getContext();
    assert(context.has_value() && context.type() == typeid(ContextPtr));
    const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);

    fs::path tempFilePath = dirPath_ /  fs::path(file.filePath.string()+".downtemp");

    ReceiveContextPtr receiveContextPtr(new ReceiveContext(tempFilePath));
    if(receiveContextPtr->isOpen()){
      contextPtr->receiveContextMap[(dirPath_ / file.filePath).string()] = receiveContextPtr;
      LOG_INFO << "Receiving file "<<file.filePath;
    }
    else{
      LOG_ERROR << "Creating "<<file.filePath<<" error";
    }
  }
}

void Server::postLocalFile(const TcpConnectionPtr& conn, const File& file){
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = POST;
  jsonData["content"]["path"] = file.filePath.string();
  jsonData["content"]["isDir"] = file.isDir;
  string message = jsonData.dump();
  if(conn){
    codec_.send(get_pointer(conn),message);
  } 

  if(!file.isDir){
    fs::path filePath = file.filePath;
    LOG_INFO << "Local Post file " << filePath;
    transferFile(conn,dirPath_,filePath,codec_);
  }
}

void Server::handlePost(const TcpConnectionPtr& conn,const json& jsonData)
{
  bool isDir = jsonData["isDir"];
  fs::path filePath = jsonData["path"];
  fs::path realPath = dirPath_ / filePath;
  if(isDir){
    try{
      fs::create_directory(dirPath_ / filePath);
      LOG_INFO << "Folder " << (dirPath_ / filePath) << " created successfully";
      {
        MutexLockGuard lock(dirMutex_);
        localDir_->addFile(filePath,true,std::time(nullptr)); 
      }
      {
        MutexLockGuard lock(connMutex_);
        for(auto &connPtr:connections_){
          if(connPtr == conn){
            continue;
          }
          struct File file(filePath,isDir);
          EventLoop::Functor f = std::bind(&Server::postLocalFile, this, connPtr, file);
          EventLoop* loop =  connPtr->getLoop();
          loop->queueInLoop(f);
        }
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

