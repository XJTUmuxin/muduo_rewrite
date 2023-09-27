#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

void TraverseDirectory(const fs::path& directoryPath, int depth = 0) {
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        const fs::path& currentPath = entry.path();
        std::string indent(depth * 4, ' ');

        if (fs::is_directory(currentPath)) {
            std::cout << indent << "Directory: " << currentPath.filename() << std::endl;
            TraverseDirectory(currentPath, depth + 1);
        } else if (fs::is_regular_file(currentPath)) {
            std::cout << indent << "File: " << currentPath.filename() << std::endl;
        }
    }
}

int main() {
    fs::path directoryPath("/home/muxin/hdd/muduo_rewrite/muduo/net"); // 替换为你要遍历的目录路径
    if (fs::exists(directoryPath) && fs::is_directory(directoryPath)) {
        std::cout << "Directory Structure:" << std::endl;
        TraverseDirectory(directoryPath);
    } else {
        std::cerr << "Invalid directory path." << std::endl;
    }
    fs::path dirPath = "/test1/test2/test.test";
    std::cout<<dirPath<<std::endl;
    for(auto it = dirPath.begin();it!=dirPath.end();++it){
        std::cout<<*it<<std::endl;
    }
    fs::path rootPath = dirPath.root_path();
    std::cout<<rootPath<<std::endl;
    fs::path filePath = dirPath / "test.test";
    std::cout<<filePath<<std::endl;

    return 0;
}
