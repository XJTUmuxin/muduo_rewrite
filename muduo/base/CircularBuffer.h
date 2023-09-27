#ifndef MUDUO_BASE_CIRCULARBUFFER
#define MUDUO_BASE_CIRCULARBUFFER

#include <cstddef>
#include <iostream>
#include<memory>
#include<assert.h>
#include<stdexcept>

namespace muduo
{
template<typename T>
class CircularBuffer
{
public:
  CircularBuffer(int _maxSize)
  : maxSize(static_cast<size_t>(_maxSize)),
    realSize(maxSize+1),
    buf_p(new T[realSize]),
    head(0),
    tail(0) 
  {
  }
  ~CircularBuffer()
  {
  }
  T& operator[](size_t index) const
  {
    if(index>=size()){
      throw std::out_of_range("Index out of range");
    }
    return buf_p[(head+index)%realSize];
  }
  T& operator[](int index) const
  {
    return operator[](static_cast<size_t>(index));
  }
  bool empty() const
  {
    return head == tail;
  }
  bool full() const
  {
    return (tail+1)%(realSize) == head;
  }
  size_t size() const
  {
    return (tail+realSize-head)%realSize;
  }
  size_t capacity() const
  {
    return maxSize;
  }
  void push_back(const T& x)
  {
    buf_p[tail] = x;
    if(full()){
      head = (head+1)%realSize;
    }
    tail = (tail+1)%realSize;
  }
  void push_back(T&& x)
  {
    buf_p[tail] = std::move(x);
    if(full()){
      head = (head+1)%realSize;
    }
    tail = (tail+1)%realSize;
  }
  void push_front(const T& x)
  {
    buf_p[(head+realSize-1)%realSize] = x;
    if(full()){
      tail = (tail+realSize-1)%realSize;
    }
    head = (head+realSize-1)%realSize;
  }
  void push_front(T&& x)
  {
    buf_p[(head+realSize-1)%realSize] = std::move(x);
    if(full()){
      tail = (tail+realSize-1)%realSize;
    }
    head = (head+realSize-1)%realSize;
  }
  T& front()
  {
    if(empty()){
      throw std::out_of_range("CircularBuffer is empty");
    }
    return buf_p[head];
  }
  T& back()
  {
    if(empty()){
      throw std::out_of_range("CircularBuffer is empty");
    }
    return buf_p[(tail+realSize-1)%realSize];
  }
  void pop_back()
  {
    if(empty()){
      throw std::out_of_range("CircularBuffer is empty");
    }
    else{
      tail = (tail+realSize-1)%realSize;
    }
  }
  void pop_front()
  {
    if(empty()){
      throw std::out_of_range("CircularBuffer is empty");
    }
    else{
      head = (head+1)%realSize;
    }
  }
  

private:
  size_t maxSize;
  size_t realSize;
  std::unique_ptr<T[]> buf_p;
  size_t head;
  size_t tail;
};

}
#endif