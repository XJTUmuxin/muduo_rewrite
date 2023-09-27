# **Channel类详解**

Channel类有点类似对一个fd的封装，但是Channel并不直接拥有这个fd，也不管理这个fd的生命周期，而是对这个fd负责，来管理这个fd的感兴趣事件，并为各种事件设置callback。

**Channle类定义:**
```cpp
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;
  typedef std::function<void(Timestamp)> ReadEventCallback;

  Channel(EventLoop* loop, int fd);
  ~Channel();

  void handleEvent(Timestamp receiveTime);
  void setReadCallback(ReadEventCallback cb)
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void>&);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();

 private:
  static string eventsToString(int fd, int ev);

  void update();
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_;
  const int  fd_;
  int        events_;
  int        revents_; // it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_;
  bool tied_;
  bool eventHandling_;
  bool addedToLoop_;
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};
```

Channel的私有成员变量：`loop_`是Channel所属EventLoop的指针；`fd_`是Channel负责的fd；`events_`维护Channel感兴趣的事件；`revents_`维护通过poll接收到的事件；`index_`用来描述Channel的状态，Poller会使用；关于`tie_`和`tied_`后面介绍；Channel一共有四个Callback，用来处理不同的事件。

Channel的成员函数：四个setXXCallback函数用来设置Callback；enableXX和disableXX用来设置Channel感兴趣的事件，通过位运算来修改`events_`；set_revents用来设置`revents`；update函数和remove函数会调用EventLoop的updateChannel函数和removeChannle函数来更新或移除本Channel；

```cpp
void Channel::update()
{
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

void Channel::remove()
{
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}
```

最重要的成员函数是handleEvent，来对发生事件进行处理，但是实际处理过程在handleEventWithGuard中，根据`revents_`的值来选择不同的Callback进行处理。

```cpp
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
  eventHandling_ = true;
  LOG_TRACE << reventsToString();
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
  {
    if (logHup_)
    {
      LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
    }
    if (closeCallback_) closeCallback_();
  }

  if (revents_ & POLLNVAL)
  {
    LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
  }

  if (revents_ & (POLLERR | POLLNVAL))
  {
    if (errorCallback_) errorCallback_();
  }
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
  {
    if (readCallback_) readCallback_(receiveTime);
  }
  if (revents_ & POLLOUT)
  {
    if (writeCallback_) writeCallback_();
  }
  eventHandling_ = false;
}
```

**那为什么通过handleEvent调用handleEventWithGuard，首先介绍Channle的tie机制。**

考虑Channle的生命周期，一个可能出现的情况：当fd发生的事件触发了Channel的CloseCallback时，而用户提供的Callback函数中析构了Channel对象，也就是handleEvent执行到一半时，所属的Channel对象被销毁了，这会产生灾难性后果。

为了防止这种情况，muduo中给Channle提供了tie机制，具体而言：Channel有tie方法和`tie_`成员变量，`tie_`是一个weak_ptr,在调用tie方法后，会将希望延长生命周期的对象绑定到`tie_`上，这个对象可以是Channel本身，也可以是Channel的owner。

```cpp
void Channel::tie(const std::shared_ptr<void>& obj)
{
  tie_ = obj;
  tied_ = true;
}
```

在未调用handleEvent之前，tie不会影响该对象的生命周期，因为`tie_`是一个weak_ptr，并不会增加引用计数，但是当调用handleEvent时，会尝试从`tie_`获取shared_ptr，如果对象还未释放，则获得了它的shared_ptr,此时引用计数增加，延长了对象生命周期。
```cpp
void Channel::handleEvent(Timestamp receiveTime)
{
  std::shared_ptr<void> guard;
  if (tied_)
  {
    guard = tie_.lock();
    if (guard)
    {
      handleEventWithGuard(receiveTime);
    }
  }
  else
  {
    handleEventWithGuard(receiveTime);
  }
}
```