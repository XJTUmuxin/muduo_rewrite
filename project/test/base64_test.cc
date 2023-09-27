#include "project/net/Context.h"
#include <iostream>
using namespace project::net;
int main(){
  char s[] = "base64test";
  std::cout<<"original content: "<<s<<std::endl;
  std::string encodeResult = base64_encode(s);
  std::cout<<"encode result: "<<encodeResult<<std::endl;
  std::string decodeResult = base64_decode(encodeResult);
  std::cout<<"decode result: "<<decodeResult<<std::endl;
  return 0;
}