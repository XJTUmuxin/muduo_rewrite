#include "project/file/FileNode.h"
#include <string>
#include <iostream>
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

string filePath1;
string filePath2;
int main(int argc,char* argv[]){
  if(argc>2){
    filePath1 = string(argv[1]);
    filePath2 = string(argv[2]);
    auto constructStartTime = chrono::high_resolution_clock::now();
    fs::path path1(filePath1);
    fs::path path2(filePath2);
    FileNode fileNode1(path1);
    FileNode fileNode2(path2);
    auto constructEndTime = chrono::high_resolution_clock::now();
    auto constructDurationTime = chrono::duration_cast<std::chrono::milliseconds>(constructEndTime-constructStartTime);
    
    // fileNode1.print();
    // fileNode2.print();
    fs::path root = "";
    DiffSets diffSets;
    auto compareStartTime = chrono::high_resolution_clock::now();
    fileNode1.compare(fileNode2,diffSets,root);
    auto compareEndTime = chrono::high_resolution_clock::now();
    auto compareDurationTime = chrono::duration_cast<std::chrono::milliseconds>(compareEndTime-compareStartTime);
    diffSets.printDiffSets();
    cout<<endl;
    cout<<"construct Time: "<<constructDurationTime.count()<<" ms"<<endl;
    cout<<"compare Time: "<<compareDurationTime.count()<<" ms"<<endl;
  }
  else if(argc>1){
    filePath1 = string(argv[1]);
    fs::path path1(filePath1);
    addFileTest(path1);
  }

  return 0;
}