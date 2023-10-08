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
    fs::path shortPath("公式15.jpg");
    std::cout<<shortPath<<std::endl;
    auto iter = shortPath.begin();
    std::cout << *iter << std::endl;
    return 0;
}
