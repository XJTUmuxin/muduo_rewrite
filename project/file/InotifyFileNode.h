#ifndef PROJECT_FILE_INOTIFY_FILE_NODE_H
#define PROJECT_FILE_INOTIFY_FILE_NODE_H

#include <sys/inotify.h>
#include<filesystem>
#include<vector>
#include<map>
#include<memory>
#include<chrono>
#include<string>
#include<iostream>
#include<project/file/FileNode.h>
#include<nlohmann/json.hpp>
#include "muduo/base/Logging.h"

namespace project{

namespace file{

namespace fs = std::filesystem;
using json = nlohmann::json;

class InotifyFileNode : public std::enable_shared_from_this<InotifyFileNode>{
private:
  std::map<fs::path,std::shared_ptr<InotifyFileNode>> children_;
  bool isDirctory_;
  std::time_t modifyTime_;
  int wd_;
  static int inotifyFd_;
  fs::path filePath_;
  // when the dir has been moved,the path will change, and the watch will become invalid, so 
  // we need to update the filePath_ and the inotifyWd
  void updatePathAndInotifyWd()
  {
    if(isDirctory_){
      inotify_rm_watch(inotifyFd_,wd_);
      wdNodePtrMap_.erase(wd_);
      int wd = inotify_add_watch(inotifyFd_,filePath_.c_str(),IN_ALL_EVENTS);
      LOG_DEBUG << filePath_ <<"added to watch";
      wd_ = wd;
      wdNodePtrMap_[wd_] = shared_from_this();
      for(auto & pi:children_){
        pi.second->filePath_ = filePath_ / pi.first;
        pi.second->updatePathAndInotifyWd();
      }
    }
  }

public:
  static std::map<int,std::shared_ptr<InotifyFileNode>> wdNodePtrMap_;
  InotifyFileNode(const fs::path& filePath)
  : wd_(-1),
    filePath_(filePath)
  {
    LOG_DEBUG<<"FileNode of "<<filePath_<<" construct";
    isDirctory_ = fs::is_directory(filePath);
    assert(fs::is_directory(filePath) || fs::is_regular_file(filePath));
    auto lastWriteTime = fs::last_write_time(filePath);
    modifyTime_ = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));

    if(isDirctory_){
      wd_ = inotify_add_watch(inotifyFd_,filePath.c_str(),IN_ALL_EVENTS);
      LOG_DEBUG <<filePath<<" added to watch with wd "<<wd_;
      assert(wd_>0);
      for(const auto& entry : fs::directory_iterator(filePath)){
        if(entry.path().filename().string()[0] == '.' || entry.path().filename().extension() == ".downtemp"){
          // hidden file
          continue;
        }
        if(fs::is_directory(entry.path()) || fs::is_regular_file(entry.path())){
          std::shared_ptr<InotifyFileNode> newPtr(new InotifyFileNode(entry.path()));
          children_[entry.path().filename()] = newPtr;
          if(fs::is_directory(entry.path())){
            wdNodePtrMap_[newPtr->getWd()] = newPtr;
          }      
        }
      }
    }
  }
  InotifyFileNode(const fs::path& filePath, bool isDir, time_t modifyTime)
  : isDirctory_(isDir),
    modifyTime_(modifyTime),
    wd_(-1),
    filePath_(filePath)
  {
    LOG_DEBUG<<"FileNode of "<<filePath_<<" construct";
    if(isDirctory_){
      wd_ = inotify_add_watch(inotifyFd_,filePath.c_str(),IN_ALL_EVENTS);
      LOG_DEBUG <<filePath<<" added to watch with wd "<<wd_;
      assert(wd_>0);
    }
  }

  ~InotifyFileNode()
  {
    LOG_INFO<<"FileNode of "<<filePath_<<" destroy";
  }


  json serialize(){
    json jsonData;
    jsonData["isDir"] = isDirctory_;
    jsonData["mTime"] = modifyTime_;
    jsonData["children"]["size"] = children_.size();
    int i = 0;
    for(auto &pi:children_){
      jsonData["children"]["name"+std::to_string(i)] = pi.first.string();
      jsonData["children"][std::to_string(i)] = pi.second->serialize();
      ++i;
    }
    return jsonData;
  }

  int getWd(){
    return wd_;
  }
  fs::path getFilePath(){
    return filePath_;
  }
  static void setInotifyFd(int inotifyFd){
    inotifyFd_ = inotifyFd;
  }

  std::shared_ptr<InotifyFileNode> getParentNode(const fs::path& shortPath)
  {
    std::shared_ptr<InotifyFileNode> fileNodePtr = InotifyFileNode::shared_from_this();
    auto iter = shortPath.begin();
    for(;iter != (--shortPath.end());++iter){
      fs::path fileName = *iter;
      assert(fileNodePtr->children_.count(fileName));
      fileNodePtr = fileNodePtr->children_[fileName];
    }
    return fileNodePtr;
  }

  void getAllFile(std::vector<project::file::File>& files,const fs::path& currentPath)
  {
    files.emplace_back(currentPath,isDirctory_);
    for(auto &pi:children_){
      pi.second->getAllFile(files,currentPath / pi.first);
    }
  }

  void addFile(const fs::path& rootPath, const fs::path& shortPath,bool isDir,time_t modifyTime)
  {
    std::shared_ptr<InotifyFileNode> fileNodePtr = getParentNode(shortPath);
    fs::path fileName = shortPath.filename();
    std::shared_ptr<InotifyFileNode> newPtr(new InotifyFileNode(rootPath/shortPath,isDir,modifyTime));
    fileNodePtr->children_[fileName] = newPtr; 
    if(isDir){
      wdNodePtrMap_[newPtr->getWd()] = newPtr;
    }
  }

  void addNode(const fs::path& fileName, const std::shared_ptr<InotifyFileNode>& newNode)
  {
    children_[fileName] = newNode;
    if(newNode->isDirctory_){
      wdNodePtrMap_[newNode->getWd()] = newNode; 
    }
  }

  void eraseChild(const fs::path& fileName)
  {
    children_[fileName]->deleteNode();
    children_.erase(fileName);
  }

  void deleteNode()
  {
    // remove inotify watch and delete all child node
    if(isDirctory_){
      inotify_rm_watch(inotifyFd_,wd_);
      wdNodePtrMap_.erase(wd_);
      auto it = children_.begin();
      while(it!=children_.end())
      {
        it->second->deleteNode();
        it = children_.erase(it);
      }
    }
  }

  void deleteFile(const fs::path& shortPath){
    std::shared_ptr<InotifyFileNode> fileNodePtr = getParentNode(shortPath);
    fs::path fileName = shortPath.filename();
    assert(fileNodePtr->children_.count(fileName));
    fileNodePtr->children_[fileName]->deleteNode();
    fileNodePtr->children_.erase(fileName); 
  }

  void moveTo(const fs::path& oldFileName,const std::shared_ptr<InotifyFileNode>& targetParentNode,
  const fs::path& newFileName)
  {
    auto moveNode = children_[oldFileName];
    moveNode->filePath_ = targetParentNode->filePath_ / newFileName;
    moveNode->updatePathAndInotifyWd();
    children_.erase(oldFileName);
    targetParentNode->children_[newFileName] = moveNode; 
  }
};
}
}
#endif