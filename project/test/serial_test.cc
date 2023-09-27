#include "project/file/FileNode.h"
#include <string>
#include <iostream>

namespace fs = std::filesystem;
using namespace std;
using namespace project;
using namespace project::file;
string filePath1;
int main(int argc,char* argv[]){
  if(argc>1){
    filePath1 = string(argv[1]);
    fs::path path1(filePath1);
    FileNode fileNode1(path1);
    fileNode1.print();
    json jsonData = fileNode1.serialize();
    cout<<jsonData.dump(4);
    FileNode fileNode2(jsonData);
    fileNode2.print();
  }
  return 0;
}