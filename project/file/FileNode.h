#ifndef PROJECT_FILE_FILENODE_H
#define PROJECT_FILE_FILENODE_H
#include<filesystem>
#include<vector>
#include<map>
#include<memory>
#include<chrono>
#include<string>
#include<iostream>
#include<nlohmann/json.hpp>
#include"muduo/base/Logging.h"

namespace project{
namespace file{

namespace fs = std::filesystem;
using json = nlohmann::json;

struct File
{
  // File(const fs::path& file_path,bool is_dir,time_t mTime)
  //   : filePath(file_path),
  //     isDir(is_dir),
  //     modifyTime(mTime)
  // {

  // }

  File(const fs::path& file_path,bool is_dir)
    : filePath(file_path),
      isDir(is_dir)
  {

  }
  fs::path filePath;
  bool isDir;
  // time_t modifyTime;
};

typedef std::vector<File> DiffSet; 

struct DiffSets
{
  DiffSet remoteAddSet;
  DiffSet localAddSet;
  DiffSet newSet;
  DiffSet oldSet;
  void printDiffSets(){
    LOG_INFO<<remoteAddSet.size()<<" remote add files:";
    for(auto &addFile:remoteAddSet){
      LOG_INFO<<addFile.filePath<<" type: "<<(addFile.isDir ? "Dir":"File");
    }
    LOG_INFO<<localAddSet.size()<<" local add files:";
    for(auto &deleteFile:localAddSet){
      LOG_INFO<<deleteFile.filePath<<" type: "<<(deleteFile.isDir ? "Dir":"File");
    }
    LOG_INFO<<newSet.size()<<" new files:";
    for(auto &newFile:newSet){
      LOG_INFO<<newFile.filePath<<" type: "<<(newFile.isDir ? "Dir":"File");
    }
    LOG_INFO<<oldSet.size()<<" old files:";
    for(auto &oldFile:oldSet){
      LOG_INFO<<oldFile.filePath<<" type: "<<(oldFile.isDir ? "Dir":"File");
    }
  }
};


class FileNode : public std::enable_shared_from_this<FileNode>{
public:
  FileNode(const fs::path& filePath);
  FileNode(const json& jsonData);
  FileNode(bool isDir,time_t modifyTime)
    : isDirctory_(isDir),
      modifyTime_(modifyTime)
  {
    
  }
  FileNode() = default;
  
  ~FileNode() = default;

  void compare(const FileNode& fileNode,DiffSets& diffSets,fs::path currentPath);

  json serialize();

  std::shared_ptr<FileNode> getParentNode(const fs::path& filePath);

  void addFile(const fs::path& filePath,bool isDir,time_t modifyTime);

  void deleteFile(const fs::path& filePath);

  void moveTo(const fs::path& oldFileName,const std::shared_ptr<FileNode>& targetParentNode,
  const fs::path& newFileName);

  void print(); // for Debug
private:
  void addAllToSet(DiffSet& diffSet,fs::path currentPath);
  void printLevel(int level); // for Debug
  std::map<fs::path,std::shared_ptr<FileNode>> children_;
  bool isDirctory_;
  std::time_t modifyTime_;

};
typedef std::shared_ptr<FileNode> NodePtr;
}
}
#endif