#ifndef PROJECT_NET_FILE_OPERATIONS_H
#define PROJECT_NET_FILE_OPERATIONS_H
#define UPDATE_FILE 1
#define DELETE_FILE 2
#define MOVE_FILE 3

#include<vector>
#include<filesystem>
#include<string>
#include<queue>

namespace project
{
namespace net
{
namespace fs = std::filesystem;
class FileOperation{
public:
  int operation;
  bool isDir;
  std::string updateFile;
  std::string deleteFile;
  std::string moveFromFile;
  std::string moveToFile;
};
class FileOperations{
public:
  void addOperations(FileOperation& fileOperation){
    fileOperations_.push(fileOperation);
  }
  FileOperation getOperation(){
    FileOperation op = fileOperations_.front();
    fileOperations_.pop();
    return op;
  }
  bool isEmpty()
  {
    return fileOperations_.empty();
  }
private:
  std::queue<FileOperation> fileOperations_;
};


}
}
#endif