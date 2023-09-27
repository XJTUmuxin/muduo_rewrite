# **Poller类详解**
## **Poller**

Poller类在muduo中负责IO多路复用，本质上是对linux poll的封装。Poller是一个基类，可以根据两种IO多路复用机制poll和epoll，来继承Poller类，定义具体的Poller类。

Poller类的定义：
```cpp
class Poller : noncopyable
{
 public:
  typedef std::vector<Channel*> ChannelList;

  Poller(EventLoop* loop);
  virtual ~Poller();

  /// Polls the I/O events.
  /// Must be called in the loop thread.
  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

  /// Changes the interested I/O events.
  /// Must be called in the loop thread.
  virtual void updateChannel(Channel* channel) = 0;

  /// Remove the channel, when it destructs.
  /// Must be called in the loop thread.
  virtual void removeChannel(Channel* channel) = 0;

  virtual bool hasChannel(Channel* channel) const;

  static Poller* newDefaultPoller(EventLoop* loop);

  void assertInLoopThread() const
  {
    ownerLoop_->assertInLoopThread();
  }

 protected:
  typedef std::map<int, Channel*> ChannelMap;
  ChannelMap channels_;

 private:
  EventLoop* ownerLoop_;
};
```

Poller的私有成员为`ownerLoop_`，也就是拥有该Poller的EventLoop对象的指针。

protected成员为channels_,是map，key为fd，value为Channel*，方便根据fd查询对应的Channel。

Poller最重要的三个接口是poll，removeChannel和updateChannel。poll是最核心的函数，本质是对poll系统调用或者epoll系统调用的封装，实现对IO时间的监听。removeChannel和updateChannel则负责删除Channel和修改Channel状态。在Poller基类中，三个函数都被定义为纯虚函数。子类中会对这三个函数进行重载。

## **EPollPoller**

EPollPoller继承自Poller，采取的是epoll系统调用。

EPollPoller类的定义：
```cpp
class EPollPoller : public Poller
{
 public:
  EPollPoller(EventLoop* loop);
  ~EPollPoller() override;

  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  void updateChannel(Channel* channel) override;
  void removeChannel(Channel* channel) override;

 private:
  static const int kInitEventListSize = 16;

  static const char* operationToString(int op);

  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;
  void update(int operation, Channel* channel);

  typedef std::vector<struct epoll_event> EventList;

  int epollfd_;
  EventList events_;
};
```

EPollPoller新增了私有成员。`epollfd_`是用于存储 epoll 实例的文件描述符。`events`是一个epoll_event结构体的vector，用于存放epoll_wait监听到的触发的事件，该vector的初始容量被设置为`kInitEventListSize`。

EPollPoller的构造函数：

```cpp
EPollPoller::EPollPoller(EventLoop* loop)
  : Poller(loop),
    epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
    events_(kInitEventListSize)
{
  if (epollfd_ < 0)
  {
    LOG_SYSFATAL << "EPollPoller::EPollPoller";
  }
}
```

通过epoll_create1创建一个epoll实例，并使用返回的文件描述符来初始化epollfd_。

poll,updateChannel,removeChannel三个成员函数都被重载了。

**poll**：
```cpp
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  LOG_TRACE << "fd total count " << channels_.size();
  int numEvents = ::epoll_wait(epollfd_,
                               events_.data(),
                               static_cast<int>(events_.size()),
                               timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
    if (implicit_cast<size_t>(numEvents) == events_.size())
    {
      events_.resize(events_.size()*2);
    }
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << "nothing happened";
  }
  else
  {
    // error happens, log uncommon ones
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "EPollPoller::poll()";
    }
  }
  return now;
}

```
poll两个输入参数，timeoutMs是超时时间，activateChannels用来存储发生事件的Channels。

poll里调用了epoll_wait来等待事件发生，epoll_wait一共有四个参数，第一个是epoll实例对应的文件描述符，第二个是存储events的buffer的起始地址，第三个是最大events数，一般是buffer的大小，第四个是超时时间。epoll_wait使用的buffer一般是数组，但是muduo为方便直接使用vector。vector内的空间是连续的，因此直接取得begin()对应的指针作为buffer的起始地址。

epoll_wait等待到事件后，调用`fillActivateChannels`来记录发生事件的Channels。之后检测到vector的容量用完，则直接给vector扩容一倍。

**fillActivateChannels:**
```cpp
void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList* activeChannels) const
{
  assert(implicit_cast<size_t>(numEvents) <= events_.size());
  for (int i = 0; i < numEvents; ++i)
  {
    Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
#ifndef NDEBUG
    int fd = channel->fd();
    ChannelMap::const_iterator it = channels_.find(fd);
    assert(it != channels_.end());
    assert(it->second == channel);
#endif
    channel->set_revents(events_[i].events);
    activeChannels->push_back(channel);
  }
}
```

通过获取events_每个epoll_event结构体的data.ptr字段来获取event对应的Channel指针，并获取Channel对应的文件描述符。将epoll监听到的事件更新到Channel的revents中，最后把Channel添加到activeChannels中。

**updateChannel:**
```cpp
void EPollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  const int index = channel->index();
  LOG_TRACE << "fd = " << channel->fd()
    << " events = " << channel->events() << " index = " << index;
  if (index == kNew || index == kDeleted)
  {
    // a new one, add with EPOLL_CTL_ADD
    int fd = channel->fd();
    if (index == kNew)
    {
      assert(channels_.find(fd) == channels_.end());
      channels_[fd] = channel;
    }
    else // index == kDeleted
    {
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
    }

    channel->set_index(kAdded);
    update(EPOLL_CTL_ADD, channel);
  }
  else
  {
    // update existing one with EPOLL_CTL_MOD/DEL
    int fd = channel->fd();
    (void)fd;
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(index == kAdded);
    if (channel->isNoneEvent())
    {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    }
    else
    {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}
```

updateChannel首先获取Channel的index，index是对Channel状态的描述（那为什么不叫state）,三个状态kNew,kDeleted和kAdded。

注意，这里的状态是指Channel对应的fd是否在epoll实例上Added，Deleted，或者是一个新的Channal，而不是指该Channel是否在`channels_`中。

根据index，如果是一个新的Channel，将其加入到`channels_`中，并根据不同情况修改Channel的index，同时调用update来对epoll实例进行操作，添加、删除或者修改event。

**update:**
```cpp
void EPollPoller::update(int operation, Channel* channel)
{
  struct epoll_event event;
  memZero(&event, sizeof event);
  event.events = channel->events();
  event.data.ptr = channel;
  int fd = channel->fd();
  LOG_TRACE << "epoll_ctl op = " << operationToString(operation)
    << " fd = " << fd << " event = { " << channel->eventsToString() << " }";
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
  {
    if (operation == EPOLL_CTL_DEL)
    {
      LOG_SYSERR << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
    }
    else
    {
      LOG_SYSFATAL << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
    }
  }
}
```

update实际上是对epoll_ctl的封装，来对epoll实例进行操作，添加、删除或者修改event。这里，会将epoll_event结构体的data.ptr字段设置为相应Channel的指针，将event和Channel关联起来。

**removeChannel:**
```cpp
void EPollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  int fd = channel->fd();
  LOG_TRACE << "fd = " << fd;
  assert(channels_.find(fd) != channels_.end());
  assert(channels_[fd] == channel);
  assert(channel->isNoneEvent());
  int index = channel->index();
  assert(index == kAdded || index == kDeleted);
  size_t n = channels_.erase(fd);
  (void)n;
  assert(n == 1);

  if (index == kAdded)
  {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->set_index(kNew);
}
```
removeChannel负责将Channel从`channels_`中移除，如果Channle对应event还未从epoll实例中移除，也顺便移除。