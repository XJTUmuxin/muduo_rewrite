#include "project/net/Client.h"
#include "muduo/base/Timestamp.h"
#include <cassert>
#include <vector>
#define EVENTS_BUF_SIZE 4096
#define CONFIG_FILE_PATH ".syn_config.json"
#define HEARTBEAT_INTERVAL 10.0
#define HEARTBEAT_TIMEOUT 2*10.0
using namespace std;
using namespace project;
using namespace project::file;
using namespace project::net;
using namespace muduo;
using namespace muduo::net;


Client::Client(EventLoop* loop,const InetAddress& serverAddr,const fs::path& dirPath)
  : client_(loop,serverAddr,"Client"),
    codec_(std::bind(&Client::onStringMessage, this, _1, _2, _3)),
    dirPath_(dirPath),
    inotifyFd_(inotify_init1(IN_NONBLOCK)),
    fileWatchChannel_(new Channel(client_.getLoop(),inotifyFd_)),
    postingNum_(0),
    deviceId_(0)
{
  InotifyFileNode::setInotifyFd(inotifyFd_);
  
  localDir_ = shared_ptr<InotifyFileNode>(new InotifyFileNode(dirPath));

  InotifyFileNode::wdNodePtrMap_[localDir_->getWd()] = localDir_;
  client_.setConnectionCallback(  
    std::bind(&Client::onConnection, this, _1));
  client_.setMessageCallback(
    std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  client_.setWriteCompleteCallback(
    std::bind(&Client::onWriteComplete, this, _1));
  client_.enableRetry();

  fileWatchChannel_->setReadCallback(
    std::bind(&Client::fileWatchHandle,this,_1));

  fileWatchChannel_->enableReading();

  fs::path configFilePath = dirPath / fs::path(CONFIG_FILE_PATH);

  if(fs::exists(configFilePath)){
    std::ifstream configFile(configFilePath);
    if(!configFile.is_open()){
      LOG_ERROR << "Can't open the config file";
    }
    else{
      json jsonData;
      configFile >> jsonData;
      if(jsonData.contains("deviceId")){
        deviceId_ = jsonData["deviceId"];
        LOG_INFO << "Device Id: "<<deviceId_;
      }
      configFile.close();
    }
  }

  closeWriteFilesTransferTimer_ =  client_.getLoop()->runEvery(5.0,std::bind(&Client::transferCloseWriteFiles,this));

  clearTimeoutMovefromEventTimer_ = client_.getLoop()->runEvery(5.0,std::bind(&Client::clearTimeoutMovefromEvents,this));

  // heartBeatCheckTimer_ = client_.getLoop()->runEvery((double)HEARTBEAT_INTERVAL,std::bind(&Client::checkHeartBeat,this));
}

void Client::fileWatchHandle(Timestamp receiveTime)
{
  //LOG_INFO << "File watch handle";
  int nbytes, offset;
  char events[EVENTS_BUF_SIZE];
  struct inotify_event *event;
  memset(events, 0, sizeof(events));

  nbytes = read(fileWatchChannel_->fd(), events, sizeof(events));
  if (nbytes <= 0) {
      // LOG_INFO << "File watch handle end";
      return;
  }

  for (offset = 0; offset < nbytes; ) {
    event = (struct inotify_event *)&events[offset]; 

    int wd = event->wd;

    auto nodePtr = InotifyFileNode::wdNodePtrMap_[wd];

    char *operate;
    int mask = event->mask;

    if (mask & IN_ACCESS)        operate = "ACCESS";
    if (mask & IN_ATTRIB)        operate = "ATTRIB";
    if (mask & IN_CLOSE_WRITE)   
    {
      // operate = "CLOSE_WRITE";  
      // LOG_INFO <<operate<<" "<<event->name;
      // LOG_INFO <<nodePtr->getFilePath()<<" "<<event->name<<": "<<operate <<" ";
      fileWatchHandleCloseWrite(event,nodePtr);
    }
    if (mask & IN_CLOSE_NOWRITE) operate = "CLOSE_NOWRITE";
    if (mask & IN_CREATE)      
    {  
      // operate = "CREATE" ;
      // LOG_INFO<<operate<<" "<<event->name;
      // LOG_INFO<<nodePtr->getFilePath() <<" "<<event->name<<": "<<operate <<" " ;
      fileWatchHandleCreate(event,nodePtr);
    }
    if (mask & IN_DELETE_SELF)   operate = "DELETE_SELF";
    if (mask & IN_MODIFY)        operate = "MODIFY";
    if (mask & IN_MOVE_SELF)     operate = "MOVE_SELF";
    if (mask & IN_MOVED_FROM)
    {
      // operate = "MOVED_FROM";
      // LOG_INFO<<operate<<" "<<event->name;
      // LOG_INFO<<nodePtr->getFilePath() <<" "<<event->name<<": "<<operate <<" " ;
      fileWatchHandleMovefrom(event,nodePtr);
    }
    if (mask & IN_MOVED_TO)
    {
      // operate = "MOVED_TO";
      // LOG_INFO<<operate<<" "<<event->name;
      // LOG_INFO<<nodePtr->getFilePath() <<" "<<event->name<<": "<<operate <<" " ;
      fileWatchHandleMoveto(event,nodePtr);
    }
    if (mask & IN_OPEN)          operate = "OPEN";
    if (mask & IN_IGNORED)       operate = "IGNORED";
    if (mask & IN_DELETE)    
    {
      // operate = "DELETE";
      // LOG_INFO<<operate<<" "<<event->name;
      // LOG_INFO<<nodePtr->getFilePath() <<" "<<event->name<<": "<<operate <<" " ;
      fileWatchHandleDelete(event,nodePtr);
    }   
    if (mask & IN_UNMOUNT)       operate = "UNMOUNT";

    offset += sizeof(struct inotify_event) + event->len; 
  }
  // LOG_INFO << "File watch handle end";
}

void Client::fileWatchHandleCreate(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path createFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  fs::path fileName = fs::path(event->name);

  if(filteredFiles_.count(createFilePath) || event->name[0] == '.' ||
   fileName.extension() == ".downtemp"){
    return;
  }

  LOG_INFO<< "Create new file "<<createFilePath;

  fs::path shortPath = createFilePath.lexically_relative(dirPath_);
  bool isDir = fs::is_directory(createFilePath);
  struct File file(shortPath,isDir);
  localDir_->addFile(dirPath_,shortPath,isDir,std::time(nullptr));
  if(isDir){
    // if the created file is a dir, post it to the server immediately
    // if the created file is a regular file, post it to the server when the file is close_write 
    postLocalFile(connection_,file);
  }
}

void Client::fileWatchHandleCloseWrite(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path updateFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  fs::path fileName = fs::path(event->name);

  if(filteredFiles_.count(updateFilePath) || event->name[0] == '.' ||
  fileName.extension() == ".downtemp"){
    return;
  }

  LOG_INFO<< "Close Write file "<<updateFilePath;

  fs::path shortPath = updateFilePath.lexically_relative(dirPath_);
  try
  {
    bool isDir = fs::is_directory(updateFilePath);
    
    auto lastWriteTime = fs::last_write_time(updateFilePath);
    time_t modifyTime = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));

    localDir_->addFile(dirPath_,shortPath,isDir,modifyTime);

    assert(!isDir);
    // when the file close_write, we add the file to the close wirte files set
    closeWriteFiles_.insert(shortPath);
    //postLocalFile(connection_,file);
  }
  catch(const std::exception& e)
  {
    LOG_ERROR<<"Close Write file insert error " << e.what();
  }
  

}

void Client::fileWatchHandleDelete(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path deleteFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  fs::path fileName = fs::path(event->name);

  if(filteredFiles_.count(deleteFilePath) || event->name[0] == '.' ||
   fileName.extension() == ".downtemp"){
    return;
  }

  LOG_INFO<< "Delete file "<<deleteFilePath;

  fs::path shortPath = deleteFilePath.lexically_relative(dirPath_);
  localDir_->deleteFile(shortPath);

  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = DELETE;
  jsonData["content"] = shortPath;

  string message = jsonData.dump();

  if(connection_){
    codec_.send(get_pointer(connection_),message);
  }
}

void Client::fileWatchHandleMovefrom(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path movefromFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  fs::path fileName = fs::path(event->name);

  if(filteredFiles_.count(movefromFilePath) || event->name[0] == '.' ||
   fileName.extension() == ".downtemp"){
    return;
  }

  LOG_INFO<<"Move from file "<<movefromFilePath;
  time_t movefromTime = time(nullptr);
  movefromEvents_.emplace_back(nodePtr,movefromTime,event->cookie,fs::path(event->name));
}

void Client::fileWatchHandleMoveto(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path movetoFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  fs::path newFileName = fs::path(event->name);

  if(filteredFiles_.count(movetoFilePath) || event->name[0] == '.' ||
   newFileName.extension() == ".downtemp"){
    return;
  }

  LOG_INFO<<"Move to file "<<movetoFilePath;

  auto it = movefromEvents_.begin();
  while(it != movefromEvents_.end())
  {
    if(it->cookie_ == event->cookie){
      // find the move_from event paired with move_to events
      auto sourceParentNode = it->parentNode_;
      auto oldFileName = it->fileName_;
      movefromEvents_.erase(it);
      
      // change the node of localDir_
      // LOG_INFO<< "oldFileName: "<<oldFileName<<" newFileName: "<<newFileName;

      sourceParentNode->moveTo(oldFileName,nodePtr,newFileName);

      //send the command
      json jsonData;
      jsonData["type"] = "command";
      jsonData["command"] = MOVE;
      fs::path movefromFilePath = sourceParentNode->getFilePath() / oldFileName;
      fs::path sourcePath = movefromFilePath.lexically_relative(dirPath_);
      jsonData["content"]["source"] = sourcePath;
      fs::path targetPath = movetoFilePath.lexically_relative(dirPath_);
      jsonData["content"]["target"] = targetPath;

      string message = jsonData.dump();

      if(connection_){
        codec_.send(get_pointer(connection_),message);
      }

      return;
    }
  }

  // not find the move_from event paired with move_to events
  auto newFileNode = std::shared_ptr<InotifyFileNode>(new InotifyFileNode(nodePtr->getFilePath() / newFileName)); 
  nodePtr->addNode(newFileName,newFileNode);
  std::vector<File> files;
  fs::path targetPath = movetoFilePath.lexically_relative(dirPath_);
  newFileNode->getAllFile(files,targetPath);
  for(auto &file:files){
    postLocalFile(connection_,file);
  }
}

void Client::transferCloseWriteFiles()
{
  // LOG_INFO << "Posting num "<< postingNum_;
  if(connection_ && postingNum_==0){
    vector<fs::path> deletePaths;
    for(auto &path:closeWriteFiles_){
      try
      {
        fs::path realPath = dirPath_ / path;
        auto lastWriteTime = fs::last_write_time(realPath);
        time_t modifyTime = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));
        time_t currentTime = time(nullptr);
        time_t diffTime = currentTime - modifyTime;
        if(diffTime > 5){
          // last modify time was more tahn five seconds ago
          struct File file(path,false);
          localDir_->addFile(dirPath_,path,false,modifyTime);
          postLocalFile(connection_,file);
          deletePaths.push_back(path);
        }
      }
      catch(const std::exception& e)
      {
        deletePaths.push_back(path);
        LOG_ERROR<<"Transfer close write file "<<path<<"error " << e.what() << '\n';
      }
      
    }
    for(auto &path:deletePaths){
      closeWriteFiles_.erase(path);
    }
  }
}

void Client::clearTimeoutMovefromEvents()
{
  auto it = movefromEvents_.begin();
  while(it != movefromEvents_.end())
  {
    time_t timeNow = time(nullptr);
    if(timeNow - it->moveTime_ > 5)
    {
      // MOVE_FROM event timeout
      auto parentNode = it->parentNode_;
      parentNode->eraseChild(it->fileName_);
      json jsonData;
      jsonData["type"] = "command";
      jsonData["command"] = DELETE;
      jsonData["content"] = (parentNode->getFilePath() / it->fileName_).lexically_relative(dirPath_);

      string message = jsonData.dump();

      if(connection_){
        codec_.send(get_pointer(connection_),message);
      }
      it = movefromEvents_.erase(it);
    }
    else{
      it++;
    }
  }
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

      heartBeatSendTimer_ = client_.getLoop()->runEvery((double)HEARTBEAT_INTERVAL,std::bind(&Client::sendHeartBeat,this,connection_));

      requestInit(connection_);
    }
    else
    {
      connection_.reset();
      client_.getLoop()->cancel(heartBeatSendTimer_);
    }
  }
}

void Client::requestInit(const TcpConnectionPtr& conn){
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = REQUESTINIT;
  jsonData["content"] = deviceId_;

  string message = jsonData.dump();

  codec_.send(get_pointer(conn),message);
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
  // LOG_INFO << "On write complete";
  if(continueTransferFile(conn,codec_)==0){
    postingNum_--;
    // LOG_INFO<<"postingNum--";
  }
}

void Client::onDataMessage(const TcpConnectionPtr& conn, const json& jsonData){
  fs::path filePath(jsonData["path"]);
  size_t fileSize = jsonData["size"];
  string content = std::move(base64_decode(jsonData["content"]));

  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);

  if(contextPtr->receiveContextMap.count(dirPath_ / filePath) == 0){
    return;
  }
  
  ReceiveContextPtr& receiveContextPtr = contextPtr->receiveContextMap[dirPath_/filePath];
  
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
    }

    fileWatchHandle(Timestamp::now()); // clear the event of tempFilePath and realFilePath 

    filteredFiles_.erase(realFilePath);
  }
}

void Client::onCommandMessage(const TcpConnectionPtr& conn,const json& jsonData){
  int command = jsonData["command"];
  switch (command)
  {
  case INITEND:
    handleInitEnd(conn,jsonData["content"]);
    break;
  case GET:
    handleGet(conn,jsonData["content"]);
    break;
  case POST:
    handlePost(conn,jsonData["content"]);
    break;
  case DELETE:
    handleDelete(conn,jsonData["content"]);
    break;
  case MOVE:
    handleMove(conn,jsonData["content"]);
    break;
  case HEARTBEAT:
    handleHeartBeat(conn,jsonData["content"]);
    break;
  default:
    LOG_INFO<<"invaild command: "<<command;
    break;
  }
}

void Client::handleInitEnd(const TcpConnectionPtr& conn, const json& jsonData)
{
  int deviceId = jsonData;

  if(deviceId_>0){
    assert(deviceId == deviceId_);
  }
  else{
    deviceId_ = deviceId;
    fs::path configFilePath = dirPath_ / fs::path(CONFIG_FILE_PATH);
    json config;
    if(fs::exists(configFilePath)){
      ifstream configFileIfs(configFilePath);
      configFileIfs >> config;
      configFileIfs.close();
    }
    config["deviceId"] = deviceId_;
    ofstream configFileOfs(configFilePath);
    configFileOfs << config;
    configFileOfs.close();
  }
  requestSyn(conn);
}

void Client::handleGet(const TcpConnectionPtr& conn, const json& jsonData)
{
  fs::path filePath = jsonData["path"];
  LOG_INFO << "Remote Get file " << filePath;
  fs::path realPath = dirPath_ / filePath;
  bool isDir = fs::is_directory(realPath);
  File file(filePath,isDir);
  postLocalFile(conn,file);
}

void Client::handlePost(const TcpConnectionPtr& conn,const json& jsonData)
{
  bool isDir = jsonData["isDir"];
  fs::path filePath = jsonData["path"];
  fs::path realPath = dirPath_ / filePath;
  time_t modifyTime = jsonData["mTime"];
  filteredFiles_.insert(realPath);
  if(isDir){
    try{
      fs::create_directory(dirPath_ / filePath);
      LOG_INFO << "Folder " << (dirPath_ / filePath) << " created successfully";
      {
        MutexLockGuard lock(dirMutex_);
        localDir_->addFile(dirPath_,filePath,true,modifyTime);
      }
    }
    catch(const exception& e){
      LOG_ERROR << "Error creating folder " << (dirPath_ / filePath); 
    }
    fileWatchHandle(Timestamp::now());  // clear the event of realPath
    filteredFiles_.erase(realPath);
  }
  else{
    {
      MutexLockGuard lock(dirMutex_);
      localDir_->addFile(dirPath_,filePath,false,modifyTime);
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

void Client::handleDelete(const TcpConnectionPtr& conn,const json& jsonData){
  fs::path filePath = jsonData;
  LOG_INFO << "Inotify delete file "<<filePath;
  filteredFiles_.insert(dirPath_ / filePath);
  try
  {
    localDir_->deleteFile(filePath);
    fs::remove_all(dirPath_ / filePath);
    LOG_INFO << "Delete file "<<filePath<<" successful";
  }
  catch(const std::exception& e)
  {
    LOG_ERROR << "Delete file "<<filePath<<" error "<<e.what();
  }
  fileWatchHandle(Timestamp::now());  // clear the event of realPath
  filteredFiles_.erase(dirPath_ / filePath);
}

void Client::handleMove(const TcpConnectionPtr& conn,const json& jsonData)
{
  fs::path sourcePath = jsonData["source"];
  fs::path targetPath = jsonData["target"];
  LOG_INFO << "Inotify move file "<<sourcePath<<" to "<<targetPath;
  filteredFiles_.insert(dirPath_ / sourcePath);
  filteredFiles_.insert(dirPath_ / targetPath);
  try
  {
    fs::rename(dirPath_ / sourcePath, dirPath_ / targetPath);
    auto sourceNode = localDir_->getParentNode(sourcePath);
    auto targetNode = localDir_->getParentNode(targetPath);
    fs::path oldFileName = sourcePath.filename();
    fs::path newFileName = targetPath.filename();
    sourceNode->moveTo(oldFileName,targetNode,newFileName);
    LOG_INFO << "Move file "<<sourcePath<<" to "<<targetPath<<" successful";
  }
  catch(const std::exception& e)
  {
    LOG_ERROR <<"Move file "<<sourcePath<<" to "<<targetPath<<" error "<<e.what();
  }
  fileWatchHandle(Timestamp::now()); // clear the event of sourcePath and targetPath
  filteredFiles_.erase(dirPath_ / sourcePath);
  filteredFiles_.erase(dirPath_ / targetPath);
}

void Client::handleHeartBeat(const TcpConnectionPtr& conn,const json& jsonData){
  time_t sendTime = jsonData["sendTime"];
  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
  contextPtr->lastHeartBeat = sendTime;
  LOG_DEBUG <<"Server heart beat"; 
}

void Client::requestSyn(const TcpConnectionPtr& conn){
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

void Client::postLocalFile(const TcpConnectionPtr& conn, const File& file){
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
    if(transferFile(conn,dirPath_,filePath,codec_)==1){
      postingNum_++;
      // LOG_INFO<<"Posting num ++2";
    }
  }
}

void Client::sendHeartBeat(const TcpConnectionPtr& conn){
  json jsonData;
  jsonData["type"] = "command";
  jsonData["command"] = HEARTBEAT;
  jsonData["content"]["sendTime"] = time(NULL); 
  string message = jsonData.dump();
  if(conn){
    codec_.send(get_pointer(conn),message);
    // LOG_DEBUG <<"Send heart beat"; 
  }
}

void Client::checkHeartBeat(){
  if(connection_)
  {
    MutexLockGuard lock(mutex_);
    const std::any& context = connection_->getContext();
    assert(context.has_value() && context.type() == typeid(ContextPtr));
    const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
    time_t lastHeartBeat = contextPtr->lastHeartBeat;
    time_t currentTime = time(NULL);
    if(currentTime-lastHeartBeat > HEARTBEAT_TIMEOUT){
      LOG_INFO<<"Server is not alive";
      connection_->forceClose();
    }
  }
}