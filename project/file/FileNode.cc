#include<filesystem>
#include<vector>
#include<chrono>
#include<memory>
#include<iostream>
#include<assert.h>
#include"muduo/base/Logging.h"

#include "project/file/FileNode.h"

using namespace project;
using namespace project::file; 
using namespace muduo;

namespace fs = std::filesystem;

FileNode::FileNode(const fs::path& filePath)
  : isDirctory_(fs::is_directory(filePath))
{
  assert(fs::is_directory(filePath) || fs::is_regular_file(filePath));
  auto lastWriteTime = fs::last_write_time(filePath);
  modifyTime_ = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));
  if(isDirctory_){
    for(const auto& entry : fs::directory_iterator(filePath)){
      if(fs::is_directory(entry.path()) || fs::is_regular_file(entry.path())){
        children_[entry.path().filename()] = std::shared_ptr<FileNode>(new FileNode(entry.path()));
      }
    }
  }
}

FileNode::FileNode(const json& jsonData)
  : isDirctory_(jsonData["isDir"]),
    modifyTime_(jsonData["mTime"])
{
  int childrenNum = jsonData["children"]["size"];
  for(int i=0;i<childrenNum;++i){
    fs::path filePath = jsonData["children"]["name"+std::to_string(i)];
    children_[filePath] = std::shared_ptr<FileNode>(new FileNode(jsonData["children"][std::to_string(i)]));
  }
}


void FileNode::print(){
  printLevel(1);
}

void FileNode::printLevel(int level=1)
{
  for(auto &pi:children_){
    std::cout<<std::string(level*3,' ')<<"---"<<pi.first<<std::endl
      <<std::string(level*3+3,' ')<<(pi.second->isDirctory_ ? "Dir":"File")<<std::endl
      <<std::string(level*3+3,' ')<<"last modify time: "<<std::asctime(std::localtime(&pi.second->modifyTime_))<<std::endl;
    pi.second->printLevel(level+1);
  }
}

void FileNode::compare(const FileNode& fileNode,DiffSets& diffSets,fs::path currentPath){
  auto iter1 = children_.begin();
  auto iter2 = fileNode.children_.begin();

  while(iter1!=children_.end() && iter2!=fileNode.children_.end()){    
    if(iter1->first == iter2->first){
      if(iter1->second->isDirctory_ != iter2->second->isDirctory_){
        LOG_ERROR<<"The dirctory and file name conflict: "<<currentPath/iter1->first;
      }
      else if(iter1->second->isDirctory_){
        // file1 and file2 is Dir
        iter1->second->compare(*(iter2->second),diffSets,currentPath / iter1->first);
      }
      else{
        // file1 and file2 is File
        if(iter1->second->modifyTime_ < iter2->second->modifyTime_){
          // remote file is newer than local file
          diffSets.newSet.emplace_back(currentPath / iter2->first,false);
        }

        else if(iter1->second->modifyTime_ > iter2->second->modifyTime_){
          // local file is newer than remote file
          diffSets.oldSet.emplace_back(currentPath / iter2->first,false);
        }
      }
      iter1++;
      iter2++;
    }
    else if(iter1->first < iter2->first){
      // file1 in this object, but not in fileNode
      iter1->second->addAllToSet(diffSets.localAddSet,currentPath / iter1->first);
      iter1++;
    }
    else if(iter1->first > iter2->first){
      // file2 in fileNode, but not in this object
      iter2->second->addAllToSet(diffSets.remoteAddSet,currentPath / iter2->first);
      iter2++;
    }
  }

  while(iter1 != children_.end()){
    iter1->second->addAllToSet(diffSets.localAddSet,currentPath / iter1->first);
    iter1++;
  }

  while(iter2 != fileNode.children_.end()){
    iter2->second->addAllToSet(diffSets.remoteAddSet,currentPath / iter2->first);
    iter2++;
  }

  return;
}

void FileNode::addAllToSet(DiffSet& diffSet,fs::path currentPath){
  
  if(isDirctory_){
    diffSet.emplace_back(currentPath,true);
    for(auto & pi : children_){
      pi.second->addAllToSet(diffSet,currentPath / pi.first);
    }
  }
  else{
    diffSet.emplace_back(currentPath,false);
  }
  return;
}

json FileNode::serialize(){
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

void FileNode::addFile(const fs::path& filePath,bool isDir,time_t modifyTime)
{
  NodePtr fileNodePtr = shared_from_this();
  auto iter = filePath.begin();
  for(;iter != (--filePath.end());++iter){
    fs::path fileName = *iter;
    if(fileNodePtr->children_.count(fileName)){
      fileNodePtr = fileNodePtr->children_[fileName];
    }
    else{
      fileNodePtr->children_[fileName] = NodePtr(new FileNode(true,modifyTime));
      fileNodePtr = fileNodePtr->children_[fileName];
    }
  }
  fs::path fileName = *iter;
  fileNodePtr->children_[fileName] = NodePtr(new FileNode(isDir,modifyTime));
}