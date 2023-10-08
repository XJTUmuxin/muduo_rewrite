#include "project/file/InotifyFileNode.h"

using namespace project::file;

int InotifyFileNode::inotifyFd_ = -1;

std::map<int,std::shared_ptr<InotifyFileNode>> InotifyFileNode::wdNodePtrMap_ = std::map<int,std::shared_ptr<InotifyFileNode>>();
