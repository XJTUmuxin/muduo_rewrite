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

namespace project{
namespace file{

namespace fs = std::filesystem;
using json = nlohmann::json;

struct File
{
  File(const fs::path& file_path,bool is_dir)
    : filePath(file_path),
      isDir(is_dir)
  {

  }
  fs::path filePath;
  bool isDir;
};

typedef std::vector<File> DiffSet; 

struct DiffSets
{
  DiffSet remoteAddSet;
  DiffSet localAddSet;
  DiffSet newSet;
  DiffSet oldSet;
  void printDiffSets(){
    std::cout<<remoteAddSet.size()<<" remote add files:"<<std::endl;
    for(auto &addFile:remoteAddSet){
      std::cout<<addFile.filePath<<" type: "<<(addFile.isDir ? "Dir":"File")<<std::endl;
    }
    std::cout<<std::endl;
    std::cout<<localAddSet.size()<<" local add files:"<<std::endl;
    for(auto &deleteFile:localAddSet){
      std::cout<<deleteFile.filePath<<" type: "<<(deleteFile.isDir ? "Dir":"File")<<std::endl;
    }
    std::cout<<std::endl;
    std::cout<<newSet.size()<<" new files:"<<std::endl;
    for(auto &newFile:newSet){
      std::cout<<newFile.filePath<<" type: "<<(newFile.isDir ? "Dir":"File")<<std::endl;
    }
    std::cout<<std::endl;
    std::cout<<oldSet.size()<<" old files:"<<std::endl;
    for(auto &oldFile:oldSet){
      std::cout<<oldFile.filePath<<" type: "<<(oldFile.isDir ? "Dir":"File")<<std::endl;
    }
    std::cout<<std::endl;
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
  ~FileNode() = default;

  void compare(const FileNode& fileNode,DiffSets& diffSets,fs::path currentPath);

  json serialize();

  void addFile(const fs::path& filePath,bool isDir,time_t modifyTime);

  void deleteFile();

  void moveFile();

  void print(); // for Debug
private:
  void addAllToSet(DiffSet& diffSet,fs::path currentPath);
  std::map<fs::path,std::shared_ptr<FileNode>> children_;
  bool isDirctory_;
  std::time_t modifyTime_;
  void printLevel(int level); // for Debug
};
typedef std::shared_ptr<FileNode> NodePtr;
}
}
#endif