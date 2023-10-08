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

public:
  static std::map<int,std::shared_ptr<InotifyFileNode>> wdNodePtrMap_;
  InotifyFileNode(const fs::path& filePath)
  : wd_(-1),
    filePath_(filePath)
  {
    isDirctory_ = fs::is_directory(filePath);
    assert(fs::is_directory(filePath) || fs::is_regular_file(filePath));
    auto lastWriteTime = fs::last_write_time(filePath);
    modifyTime_ = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));

    if(isDirctory_){
      wd_ = inotify_add_watch(inotifyFd_,filePath.c_str(),IN_ALL_EVENTS);
      LOG_DEBUG <<filePath<<" added to watch with wd "<<wd_;
      assert(wd_>0);
      for(const auto& entry : fs::directory_iterator(filePath)){
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
    if(isDirctory_){
      wd_ = inotify_add_watch(inotifyFd_,filePath.c_str(),IN_ALL_EVENTS);
      LOG_DEBUG <<filePath<<" added to watch with wd "<<wd_;
      assert(wd_>0);
    }
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

  void addFile(const fs::path& rootPath, const fs::path& shortPath,bool isDir,time_t modifyTime)
  {
    std::shared_ptr<InotifyFileNode> fileNodePtr = InotifyFileNode::shared_from_this();
    auto iter = shortPath.begin();
    for(;iter != (--shortPath.end());++iter){
      fs::path fileName = *iter;
      assert(fileNodePtr->children_.count(fileName));
      fileNodePtr = fileNodePtr->children_[fileName];
    }
    fs::path fileName = *iter;
    std::shared_ptr<InotifyFileNode> newPtr(new InotifyFileNode(rootPath/shortPath,isDir,modifyTime));
    fileNodePtr->children_[fileName] = newPtr; 
    if(isDir){
      wdNodePtrMap_[newPtr->getWd()] = newPtr;
    }
  }
};
}
}
#endif