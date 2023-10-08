#include "project/file/InotifyFileNode.h"
#include <string>
#include <iostream>
#define EVENTS_BUF_SIZE 4096
namespace fs = std::filesystem;
using namespace std;
using namespace project;
using namespace project::file;

void addFileTest(const fs::path& filePath){
  NodePtr nodePtr(new FileNode(filePath));
  nodePtr->print();
  nodePtr->addFile("addFileTest1/addFileTest2/test.test",false,12341);
  nodePtr->print();
}
void display_event(const char *base, struct inotify_event *event)
{
    printf("display event: \n");
    char *operate;
    int mask = event->mask;

    if (mask & IN_ACCESS)        operate = "ACCESS";
    if (mask & IN_ATTRIB)        operate = "ATTRIB";
    if (mask & IN_CLOSE_WRITE)   operate = "CLOSE_WRITE";
    if (mask & IN_CLOSE_NOWRITE) operate = "CLOSE_NOWRITE";
    if (mask & IN_CREATE)        operate = "CREATE";
    if (mask & IN_DELETE_SELF)   operate = "DELETE_SELF";
    if (mask & IN_MODIFY)        operate = "MODIFY";
    if (mask & IN_MOVE_SELF)     operate = "MOVE_SELF";
    if (mask & IN_MOVED_FROM)    operate = "MOVED_FROM";
    if (mask & IN_MOVED_TO)      operate = "MOVED_TO";
    if (mask & IN_OPEN)          operate = "OPEN";
    if (mask & IN_IGNORED)       operate = "IGNORED";
    if (mask & IN_DELETE)        operate = "DELETE";
    if (mask & IN_UNMOUNT)       operate = "UNMOUNT";

    printf("%s  %s: %s\n", base, event->name, operate);
}

string filePath1;
string filePath2;
int main(int argc,char* argv[]){
  if(argc>1){
    filePath1 = string(argv[1]);
    fs::path path1(filePath1);
    int inotifyFd = inotify_init();
    InotifyFileNode::setInotifyFd(inotifyFd);
    InotifyFileNode node(path1);
    InotifyFileNode::wdNodePtrMap_[node.getWd()] = make_shared<InotifyFileNode>(node);
    json jsonData = node.serialize();
    string result = jsonData.dump(2);
    cout<<result;

    int nbytes, offset;
    char events[EVENTS_BUF_SIZE];
    struct inotify_event *event;

    for (;;) {
        memset(events, 0, sizeof(events));

        // 读取发生的事件
        nbytes = read(inotifyFd, events, sizeof(events));
        if (nbytes <= 0) {
            printf("Failed to read events\n");
            continue;
        }

        // 开始打印发生的事件
        for (offset = 0; offset < nbytes; ) {
            event = (struct inotify_event *)&events[offset]; // 获取变动事件的指针

            int wd = event->wd;

            auto nodePtr = InotifyFileNode::wdNodePtrMap_[wd];

            display_event(nodePtr->getFilePath().c_str(), event);

            offset += sizeof(struct inotify_event) + event->len; // 获取下一个变动事件的偏移量
        }
    }


  }

  return 0;
}