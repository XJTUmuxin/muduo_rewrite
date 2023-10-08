#include "project/net/Context.h"
using namespace project::net;
std::string project::net::base64_encode(std::uint8_t const *data, std::size_t len) {
    std::string dest;
    dest.resize(boost::beast::detail::base64::encoded_size(len));
    dest.resize(boost::beast::detail::base64::encode(&dest[0], data, len));
    return dest;
}

std::string project::net::base64_encode(boost::string_view s) {
    return project::net::base64_encode(reinterpret_cast<
                                 std::uint8_t const*> (s.data()), s.size());
}

std::string project::net::base64_decode(boost::string_view data) {
    std::string dest;
    dest.resize(boost::beast::detail::base64::decoded_size(data.size()));
    auto const result = boost::beast::detail::base64::decode(
            &dest[0], data.data(), data.size());
    dest.resize(result.first);
    return dest;
}
int project::net::transferFile(const muduo_net::TcpConnectionPtr& conn,const fs::path& dirPath,const fs::path& filePath,LengthHeaderCodec& codec)
{
  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);

  int ret = -1;

  TransferContextPtr transferContextPtr(new TransferContext(dirPath / filePath,filePath));
  if(transferContextPtr->isOpen()){
    LOG_INFO << "File "<<filePath<<" is open";
    size_t fileSize = transferContextPtr->getFileSize();
    std::string fileData = transferContextPtr->read(BlockSize);
    size_t realBlockSize = fileData.size();
    time_t modifyTime = transferContextPtr->getModifyTime();
    json jsonData;
    jsonData["type"] = "data";
    jsonData["path"] = filePath.string();
    jsonData["size"] = fileSize;
    jsonData["mTime"] = modifyTime;
    jsonData["content"] = std::move(base64_encode(fileData));
    std::string message = std::move(jsonData.dump());
    if(conn){
      LOG_INFO << "Sending file "<<filePath<<" package no "<<transferContextPtr->getPackNo();
      if(transferContextPtr->isReadComplete()){
        LOG_INFO << "Sending end "<<filePath;
        ret = 0;
      }
      else{
        contextPtr->transferContextQue.push_back(transferContextPtr);
        ret = 1;
      }
      codec.send(get_pointer(conn),message);
    }
  }
  else{
    LOG_INFO << "No such file";
    ret = -1;
  }
  return ret;
}

int project::net::continueTransferFile(const muduo_net::TcpConnectionPtr& conn,LengthHeaderCodec& codec)
{
  const std::any& context = conn->getContext();
  assert(context.has_value() && context.type() == typeid(ContextPtr));
  const ContextPtr& contextPtr = any_cast<const ContextPtr&>(context);
  LOG_INFO <<"Transfer context num "<<contextPtr->transferContextQue.size();

  int ret = -1;

  if(!contextPtr->transferContextQue.empty()){
    TransferContextPtr& transferContextPtr = contextPtr->transferContextQue.front();
    size_t fileSize = transferContextPtr->getFileSize();
    std::string fileData = transferContextPtr->read(BlockSize);
    fs::path filePath = transferContextPtr->getShortFilePath();
    time_t modifyTime = transferContextPtr->getModifyTime();
    size_t realBlockSize = fileData.size();
    json jsonData;
    jsonData["type"] = "data";
    jsonData["path"] = filePath.string();
    jsonData["size"] = fileSize;
    jsonData["mTime"] = modifyTime;
    jsonData["content"] = std::move(base64_encode(fileData));
    std::string message = std::move(jsonData.dump());
    if(conn){
      LOG_INFO << "Sending file "<<filePath<<" package no "<<transferContextPtr->getPackNo();
      if(transferContextPtr->isReadComplete()){
        contextPtr->transferContextQue.pop_front();
        LOG_INFO << "Sending end "<<filePath;
        ret = 0;
      }
      else{
        ret = 1;
      }
      codec.send(get_pointer(conn),message);
    }
  }
  else{
    ret = -1;
  }
  return ret;
}