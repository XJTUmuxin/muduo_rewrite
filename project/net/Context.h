#ifndef PROJECT_NET_CONTEXT_H
#define PROJECT_NET_CONTEXT_H
#include <fstream>
#include <string>
#include <filesystem>
#include <memory>
#include <map>
#include <queue>
#include <nlohmann/json.hpp>
#include "muduo/net/TcpConnection.h"
#include "project/net/codec.h"
#include "muduo/base/Logging.h"
#include <boost/beast/core/detail/base64.hpp>
namespace fs = std::filesystem; 
namespace muduo_net = muduo::net;
using json = nlohmann::json;

namespace project
{
namespace net
{

const size_t BlockSize = 64*1024;

class TransferContext{
public:
  TransferContext(const fs::path& filePath,const fs::path& shortFilePath)
    : shortFilePath_(shortFilePath),
      fileSize_(fs::file_size(filePath)),
      modifyTime_(std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(
        fs::last_write_time(filePath)))),
      transferFileStream_(filePath)
  {
    
  }
  ~TransferContext(){
    transferFileStream_.close();
  }
  std::string read(size_t bytes){
    std::string fileData;
    fileData.resize(bytes);

    transferFileStream_.read(fileData.data(),bytes);

    size_t bytesRead = transferFileStream_.gcount();

    if(bytesRead < bytes){
      fileData.resize(bytesRead);
    }

    return fileData;
  }
  bool isOpen(){
    return transferFileStream_.is_open();
  }
  size_t getFileSize(){
    return fileSize_;
  }
  fs::path getShortFilePath(){
    return shortFilePath_;
  }
  time_t getModifyTime(){
    return modifyTime_;
  }
private:
  fs::path shortFilePath_;
  size_t fileSize_;
  time_t modifyTime_;
  std::ifstream transferFileStream_;
};
typedef std::shared_ptr<TransferContext> TransferContextPtr;
typedef std::deque<TransferContextPtr> TransferContextQue;

class ReceiveContext{
public:
  ReceiveContext(const fs::path& filePath)
    : receiveFileStream_(filePath),
      writeBytes_(0)
  {
    LOG_INFO << "Receive context construct";
  }
  ~ReceiveContext(){
    LOG_INFO << "Receive context destroy";
    receiveFileStream_.flush();
    receiveFileStream_.close();
  }
  bool isOpen(){
    return receiveFileStream_.is_open();
  }
  bool isWriteComplete(size_t fileSize){
    if(writeBytes_ == fileSize){
      return true;
    }
    return false;
  }
  void write(const char* buf,size_t len){
    receiveFileStream_.write(buf,len);
    writeBytes_ += len;
  }
private:
  std::ofstream receiveFileStream_;
  size_t writeBytes_;
};
typedef std::shared_ptr<ReceiveContext> ReceiveContextPtr;
typedef std::map<std::string,ReceiveContextPtr> ReceiveContextMap;

struct Context{
  TransferContextQue transferContextQue;
  ReceiveContextMap receiveContextMap;
};
typedef std::shared_ptr<Context> ContextPtr;

void transferFile(const muduo_net::TcpConnectionPtr& conn,const fs::path& dirPath,const fs::path& filePath,LengthHeaderCodec& codec);

void continueTransferFile(const muduo_net::TcpConnectionPtr& conn,LengthHeaderCodec& codec);

std::string base64_encode(std::uint8_t const *data, std::size_t len);
std::string base64_encode(boost::string_view s);

std::string base64_decode(boost::string_view data);

}
}

#endif