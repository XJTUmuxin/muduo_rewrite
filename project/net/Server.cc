#include "project/net/Server.h"
#include "project/net/Context.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <utime.h>

#define CONFIG_FILE_PATH ".syn_config.json"
#define TRANSH_PATH ".transh/"

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

  fs::path configFilePath = dirPath / fs::path(CONFIG_FILE_PATH);

  if(fs::exists(configFilePath)){
    std::ifstream configFile(configFilePath);
    if(!configFile.is_open()){
      LOG_ERROR << "Can't open the config file";
    }
    else{
      configFile >> config_;
      if(config_.contains("maxDeviceId")){
        int maxDeviceId= config_["maxDeviceId"];
        LOG_INFO << "MaxDevice Id: "<<maxDeviceId;
      }
      if(config_.contains("deviceIds") && config_["deviceIds"].is_array()){
        for(const auto & deviceId:config_["deviceIds"]){
          int Id = deviceId;
          offlineDeviceToFileOperationsMap_[Id] = FileOperations();
          LOG_INFO << "Offline device "<<Id;
        }
      }
      configFile.close();
    }
  }
  else{
    std::ofstream configFile(configFilePath);
    config_["maxDeviceId"] = 0;
    config_["deviceIds"] = json::array();
    configFile << config_;
    configFile.close();
  }
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
    const std::any& context = conn->getContext();
    assert(context.has_value() && context.type() == typeid(ContextPtr));
    const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
    
    int deviceId = contextPtr->deviceId;
    {
      // add offline Device
      LOG_INFO<<"Device "<< deviceId<<" offline";
      MutexLockGuard lock2(offlineDeviceMutex_);
      offlineDeviceToFileOperationsMap_[deviceId] = FileOperations();
    }
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
  case REQUESTINIT:
    handleRequestInit(conn,jsonData["content"]);
    break;
  case REQUESTSYN:
    handleRequestSyn(conn, jsonData["content"]);
    break;
  case DELETE:
    handleDelete(conn, jsonData["content"]);
    break;
  case MOVE:
    handleMove(conn, jsonData["content"]);
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

  if(contextPtr->receiveContextMap.count(dirPath_ / filePath) == 0){
    return;
  }
  
  ReceiveContextPtr& receiveContextPtr = contextPtr->receiveContextMap[dirPath_ / filePath];

  try
  {
    receiveContextPtr->write(content.data(),content.size());
  }
  catch(const std::exception& e)
  {
    contextPtr->receiveContextMap.erase(dirPath_ / filePath);
    LOG_ERROR<<"Download file "<<filePath<<" error " << e.what();
  }

  // LOG_INFO<<"Receiving file "<<filePath<<" package no "<<receiveContextPtr->getPackNo();

  if(receiveContextPtr->isWriteComplete(fileSize)){

    contextPtr->receiveContextMap.erase(dirPath_ / filePath);

    fs::path realFilePath = dirPath_/filePath;
    fs::path tempFilePath = fs::path(realFilePath.string()+".downtemp");

    time_t modifyTime = jsonData["mTime"];

    std::chrono::system_clock::time_point sysTimePoint = std::chrono::system_clock::from_time_t(modifyTime);
    std::chrono::time_point<std::chrono::file_clock> fileTimePoint = std::chrono::file_clock::from_sys(sysTimePoint);

    try
    {
      fs::last_write_time(tempFilePath,fileTimePoint);
      if(fs::exists(realFilePath)){
        fs::remove(realFilePath);
      }
      fs::rename(tempFilePath,realFilePath);
      LOG_INFO << "Received file "<<filePath<<" successfully";
    }
    catch(const std::exception& e)
    {
      LOG_ERROR<<"Download file "<<filePath<<" error " << e.what();
      return;
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
    {
      // record update operation for offline device
      FileOperation fileOperation;
      fileOperation.operation = UPDATE_FILE;
      fileOperation.updateFile = filePath;
      fileOperation.isDir = false;

      MutexLockGuard lock(offlineDeviceMutex_);
      for(auto &device:offlineDeviceToFileOperationsMap_){
        device.second.addOperations(fileOperation);
      }
    }
  }
}

void Server::handleRequestInit(const TcpConnectionPtr& conn, const json& jsonData)
{
  int deviceId = jsonData;
  {
  MutexLockGuard lock(offlineDeviceMutex_);
  if(offlineDeviceToFileOperationsMap_.count(deviceId)){
    // offline devide
    auto fileOperations = offlineDeviceToFileOperationsMap_[deviceId];
    offlineDeviceToFileOperationsMap_.erase(deviceId);
    handleInit(conn,fileOperations);
  }
  else{
    // new device
    {
      MutexLockGuard lock1(configMutex_);
      int maxDeviceId = config_["maxDeviceId"];
      deviceId = ++maxDeviceId;
      config_["maxDeviceId"] = maxDeviceId;
      config_["deviceIds"].push_back(maxDeviceId);

      LOG_INFO << "New device Id "<<maxDeviceId;

      fs::path configFilePath = dirPath_/CONFIG_FILE_PATH;
      std::ofstream configFile(configFilePath);
      configFile << config_;
      configFile.close();
    }
  }
  }
  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
  contextPtr->deviceId = deviceId;

  json command;
  command["type"] = "command";
  command["command"] = INITEND;
  command["content"] = deviceId;
  string message = command.dump();
  if(conn){
    codec_.send(get_pointer(conn),message);
  }
}

void Server::handleInit(const TcpConnectionPtr& conn, FileOperations& fileOperations)
{
  while(!fileOperations.isEmpty()){
    auto fileOperation = fileOperations.getOperation();
    switch (fileOperation.operation)
    {
    case UPDATE_FILE:
    {
      File file(fileOperation.updateFile,fileOperation.isDir);
      try
      {
        postLocalFile(conn,file);
      }
      catch(const std::exception& e)
      {
        LOG_ERROR << "Post file "<<file.filePath<<" error "<<e.what();
      }
      break;
    }
    case DELETE_FILE:
      notifyDelete(conn,fileOperation.deleteFile);
      break;
    case MOVE_FILE:
      notifyMove(conn,fileOperation.moveFromFile,fileOperation.moveToFile);
      break;
    
    default:
      break;
    }
  }
}


void Server::handleRequestSyn(const TcpConnectionPtr& conn, const json& jsonData)
{
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
      {
        // record create operation for offline device
        FileOperation fileOperation;
        fileOperation.operation = UPDATE_FILE;
        fileOperation.updateFile = file.filePath;
        fileOperation.isDir = true;

        MutexLockGuard lock(offlineDeviceMutex_);
        for(auto &device:offlineDeviceToFileOperationsMap_){
          device.second.addOperations(fileOperation);
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
  }
}

void Server::postLocalFile(const TcpConnectionPtr& conn, const File& file){
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = POST;
  jsonData["content"]["path"] = file.filePath.string();
  jsonData["content"]["isDir"] = file.isDir;
  fs::path realPath = dirPath_ / file.filePath;
  auto lastWriteTime = fs::last_write_time(realPath);
  time_t modifyTime = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));
  jsonData["content"]["mTime"] = modifyTime;
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
  time_t modifyTime = jsonData["mTime"];
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
      {
        // record create operation for offline device
        FileOperation fileOperation;
        fileOperation.operation = UPDATE_FILE;
        fileOperation.updateFile = filePath;
        fileOperation.isDir = true;

        MutexLockGuard lock(offlineDeviceMutex_);
        for(auto &device:offlineDeviceToFileOperationsMap_){
          device.second.addOperations(fileOperation);
        }
      }
    }
    catch(const exception& e){
      LOG_ERROR << "Error creating folder " << (dirPath_ / filePath); 
    }
  }
  else{

    {
      MutexLockGuard lock(dirMutex_);
      localDir_->addFile(filePath,false,modifyTime); 
    }

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

void Server::handleDelete(const TcpConnectionPtr& conn,const json& jsonData)
{
  fs::path filePath = jsonData;
  try
  {
    // move to transh floder
    fs::path sourcePath = dirPath_ / filePath;
    fs::path targetPath = dirPath_ / TRANSH_PATH / filePath;
    if(fs::is_regular_file(sourcePath) || !(fs::is_empty(sourcePath))){
      fs::path parentDir = targetPath.parent_path();
      fs::create_directories(parentDir);
      if(fs::exists(targetPath)){
        fs::remove_all(targetPath);
      }
      fs::rename(sourcePath,targetPath);
    }
    else{
      fs::remove(sourcePath);
    }
    LOG_INFO << "Delete file "<<filePath<<" successful";
  }
  catch(const std::exception& e)
  {
    LOG_ERROR << "Delete file "<<filePath<<" error "<<e.what();
    return ;
  }
  {
    MutexLockGuard lock(dirMutex_);
    localDir_->deleteFile(filePath);
  }
  {
    // notify online diveice to delete
    MutexLockGuard lock(connMutex_);
    for(auto &connPtr:connections_){
      if(connPtr == conn){
        continue;
      }
      EventLoop::Functor f = std::bind(&Server::notifyDelete, this, connPtr, filePath);
      EventLoop* loop =  connPtr->getLoop();
      loop->queueInLoop(f);
    }
  }
  {
    // record delete operation for offline device
    FileOperation fileOperation;
    fileOperation.operation = DELETE_FILE;
    fileOperation.deleteFile = filePath;

    MutexLockGuard lock(offlineDeviceMutex_);
    for(auto &device:offlineDeviceToFileOperationsMap_){
      device.second.addOperations(fileOperation);
    }
  }
}

void Server::handleMove(const TcpConnectionPtr& conn,const json& jsonData)
{
  fs::path sourcePath = jsonData["source"];
  fs::path targetPath = jsonData["target"];
  try
  {
    // move file
    fs::path realSourcePath = dirPath_ / sourcePath;
    fs::path realTargetPath = dirPath_ / targetPath;
    fs::rename(realSourcePath,realTargetPath);
    LOG_INFO << "Move file "<<sourcePath<<" to "<<targetPath<<" successful";
  }
  catch(const std::exception& e)
  {
    LOG_ERROR <<"Move file "<<sourcePath<<" to "<<targetPath<<" error "<<e.what();
    return ;
  }
  {
    MutexLockGuard lock(dirMutex_);
    auto sourceParentNode = localDir_->getParentNode(sourcePath);
    fs::path oldFileName = sourcePath.filename();
    auto targetParentNode = localDir_->getParentNode(targetPath);
    fs::path newFileName = targetPath.filename();
    sourceParentNode->moveTo(oldFileName,targetParentNode,newFileName);
  }
  {
    // notify online diveice to delete
    MutexLockGuard lock(connMutex_);
    for(auto &connPtr:connections_){
      if(connPtr == conn){
        continue;
      }
      EventLoop::Functor f = std::bind(&Server::notifyMove, this, connPtr, sourcePath, targetPath);
      EventLoop* loop =  connPtr->getLoop();
      loop->queueInLoop(f);
    }
  }
  {
    // record delete operation for offline device
    FileOperation fileOperation;
    fileOperation.operation = MOVE_FILE;
    fileOperation.moveFromFile = sourcePath;
    fileOperation.moveToFile = targetPath;

    MutexLockGuard lock(offlineDeviceMutex_);
    for(auto &device:offlineDeviceToFileOperationsMap_){
      device.second.addOperations(fileOperation);
    }
  }
}
void Server::notifyDelete(const TcpConnectionPtr& conn,const fs::path& filePath)
{
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = DELETE;
  jsonData["content"] = filePath;

  string message = jsonData.dump();

  if(conn){
    codec_.send(get_pointer(conn),message);
  }
}
void Server::notifyMove(const TcpConnectionPtr& conn,const fs::path& sourcePath,const fs::path tartgetPath)
{
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = MOVE;
  jsonData["content"]["source"] = sourcePath;
  jsonData["content"]["target"] = tartgetPath;

  string message = jsonData.dump();

  if(conn){
    codec_.send(get_pointer(conn),message);
  }
}

