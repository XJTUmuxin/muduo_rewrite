#include <sys/inotify.h>
#include "project/file/FileNode.h"

namespace project{

namespace file{

class InotifyFileNode : public FileNode{
public:
  InotifyFileNode(const fs::path& filePath)
  : isDirctory_(fs::is_directory(filePath)),
    wd_(-1)
  {
    assert(fs::is_directory(filePath) || fs::is_regular_file(filePath));
    auto lastWriteTime = fs::last_write_time(filePath);
    modifyTime_ = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(lastWriteTime));
    if(isDirctory_){
      wd_ = inotify_add_watch(inotifyFd_,filePath,IN_ALL_EVENTS);
      assert(wd_>0);
      wdNodePtrMap_[wd_] = shared_from_this();
      for(const auto& entry : fs::directory_iterator(filePath)){
        if(fs::is_directory(entry.path()) || fs::is_regular_file(entry.path())){
          children_[entry.path().filename()] = std::shared_ptr<FileNode>(new InotifyFileNode(entry.path()));
        }
      }
    }
  }
  static setInotifyFd(int inotifyFd){
    inotifyFd_ = inotifyFd;
  }
private:
  int wd_;
  static int inotifyFd_;
  static std::map<int,NodePtr> wdNodePtrMap_;
};
}
}