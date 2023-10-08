#include "project/net/Client.h"
#include "muduo/base/Timestamp.h"
#include <cassert>
#define EVENTS_BUF_SIZE 4096
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
    postingNum_(0)
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

  closeWriteFilesTransferTimer =  client_.getLoop()->runEvery(5.0,std::bind(&Client::transferCloseWriteFiles,this));

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

  LOG_INFO << "Inotify event happened";

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
      operate = "CLOSE_WRITE";  
      LOG_INFO <<operate<<" "<<event->name;
      LOG_INFO <<nodePtr->getFilePath()<<" "<<event->name<<": "<<operate <<" ";
      fileWatchHandleCloseWrite(event,nodePtr);
    }
    if (mask & IN_CLOSE_NOWRITE) operate = "CLOSE_NOWRITE";
    if (mask & IN_CREATE)      
    {  
      operate = "CREATE" ;
      LOG_INFO<<operate<<" "<<event->name;
      LOG_INFO<<nodePtr->getFilePath() <<" "<<event->name<<": "<<operate <<" " ;
      fileWatchHandleCreate(event,nodePtr);
    }
    if (mask & IN_DELETE_SELF)   operate = "DELETE_SELF";
    if (mask & IN_MODIFY)        operate = "MODIFY";
    if (mask & IN_MOVE_SELF)     operate = "MOVE_SELF";
    if (mask & IN_MOVED_FROM)    operate = "MOVED_FROM";
    if (mask & IN_MOVED_TO)      operate = "MOVED_TO";
    if (mask & IN_OPEN)          operate = "OPEN";
    if (mask & IN_IGNORED)       operate = "IGNORED";
    if (mask & IN_DELETE)        operate = "DELETE";
    if (mask & IN_UNMOUNT)       operate = "UNMOUNT";

    // LOG_INFO<<operate<<" "<<event->name;

    offset += sizeof(struct inotify_event) + event->len; 
  }
  // LOG_INFO << "File watch handle end";
}

void Client::fileWatchHandleCreate(const struct inotify_event * event,const shared_ptr<InotifyFileNode>& nodePtr)
{
  fs::path createFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  if(filteredFiles_.count(createFilePath)){
    return;
  }

  fs::path shortPath = fs::relative(createFilePath,dirPath_);
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
  fs::path createFilePath = nodePtr->getFilePath() / fs::path(event->name);
  
  if(filteredFiles_.count(createFilePath)){
    return;
  }

  fs::path shortPath = fs::relative(createFilePath,dirPath_);
  bool isDir = fs::is_directory(createFilePath);
  struct File file(shortPath,isDir);
  assert(!isDir);
  // when the file close_write, we add the file to the close wirte files set
  closeWriteFiles_.insert(shortPath);
  //postLocalFile(connection_,file);
}

void Client::transferCloseWriteFiles()
{
  // LOG_INFO << "Posting num "<< postingNum_;
  if(connection_ && postingNum_==0){
    LOG_INFO << "Close write file transfer Timer trigger";
    vector<fs::path> deletePaths;
    for(auto &path:closeWriteFiles_){
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
    for(auto &path:deletePaths){
      closeWriteFiles_.erase(path);
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
  LOG_INFO << "On write complete";
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
  
  ReceiveContextPtr& receiveContextPtr = contextPtr->receiveContextMap[dirPath_/filePath];
  
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

    fileWatchHandle(Timestamp::now()); // clear the event of tempFilePath and realFilePath 

    filteredFiles_.erase(tempFilePath);
    filteredFiles_.erase(realFilePath);

    LOG_INFO << "Received file "<<filePath<<" successfully";
    {
      MutexLockGuard lock(dirMutex_);
      localDir_->addFile(dirPath_,filePath,false,modifyTime);
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
  if(transferFile(conn,dirPath_,filePath,codec_)==1){
    postingNum_++;
    // LOG_INFO<<"Posting num ++1";
  }
}

void Client::handlePost(const TcpConnectionPtr& conn,const json& jsonData)
{
  bool isDir = jsonData["isDir"];
  fs::path filePath = jsonData["path"];
  fs::path realPath = dirPath_ / filePath;
  filteredFiles_.insert(realPath);
  if(isDir){
    try{
      fs::create_directory(dirPath_ / filePath);
      LOG_INFO << "Folder " << (dirPath_ / filePath) << " created successfully";
      {
        MutexLockGuard lock(dirMutex_);
        localDir_->addFile(dirPath_,filePath,true,std::time(nullptr));
      }
    }
    catch(const exception& e){
      LOG_ERROR << "Error creating folder " << (dirPath_ / filePath); 
    }
    fileWatchHandle(Timestamp::now());  // clear the event of realPath
    filteredFiles_.erase(realPath);
  }
  else{
    const std::any& context = conn->getContext();
    assert(context.has_value() && context.type() == typeid(ContextPtr));
    const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);

    fs::path tempFilePath = dirPath_ / fs::path(filePath.string()+".downtemp");
    filteredFiles_.insert(tempFilePath);

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

void Client::postLocalFile(const TcpConnectionPtr& conn, const File& file){
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
    if(transferFile(conn,dirPath_,filePath,codec_)==1){
      postingNum_++;
      // LOG_INFO<<"Posting num ++2";
    }
  }
}