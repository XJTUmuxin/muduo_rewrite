# **EventLoop类详解**

muduo网络库是one loop per thread的，也就是每个线程只执行一个loop，而EventLoop类就是对loop的一个封装。

muduo网络库是Reactor模式的，也就是事件驱动的，因此每个进程中执行的loop被称为eventloop，用来不断循环监听事件、处理事件。

首先来看EventLoop类的定义：

```cpp
class EventLoop : noncopyable
{
 public:
  typedef std::function<void()> Functor;

  EventLoop();
  ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  ///
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.
  void runInLoop(Functor cb);
  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  void queueInLoop(Functor cb);

  size_t queueSize() const;

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  TimerId runAt(Timestamp time, TimerCallback cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  TimerId runAfter(double delay, TimerCallback cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  TimerId runEvery(double interval, TimerCallback cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  void cancel(TimerId timerId);

  // internal usage
  void wakeup();
  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);
  bool hasChannel(Channel* channel);

  // pid_t threadId() const { return threadId_; }
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  bool eventHandling() const { return eventHandling_; }

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  static EventLoop* getEventLoopOfCurrentThread();

 private:
  void abortNotInLoopThread();
  void handleRead();  // waked up
  void doPendingFunctors();

  void printActiveChannels() const; // DEBUG

  typedef std::vector<Channel*> ChannelList;

  bool looping_; /* atomic */
  std::atomic<bool> quit_;
  bool eventHandling_; /* atomic */
  bool callingPendingFunctors_; /* atomic */
  int64_t iteration_;
  const pid_t threadId_;
  Timestamp pollReturnTime_;
  std::unique_ptr<Poller> poller_;
  std::unique_ptr<TimerQueue> timerQueue_;
  int wakeupFd_;
  // unlike in TimerQueue, which is an internal class,
  // we don't expose Channel to client.
  std::unique_ptr<Channel> wakeupChannel_;
  boost::any context_;

  // scratch variables
  ChannelList activeChannels_;
  Channel* currentActiveChannel_;

  mutable MutexLock mutex_;
  std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_);
};
```

EventLoop类的定义看起来有些庞杂，实际上EventLoop的功能一共有四个，所有的成员变量和函数都为这四个功能服务：
1) 提供运行循环
2) 运行定时任务
3) 处理激活通道事件
4) 保证线程安全

对于1)，loop()提供运行循环，quit()退出循环，iterator()查询循环次数，wakeup()用于唤醒loop线程，handleRead()读取唤醒消息。

对于2)，runInLoop()在loop线程中“立即”运行一次用户任务，runAt()/runAfter()添加一次性定时任务，runEvery()添加周期定时任务，doPendingFunctors()回调所有的pending函数，vector pendingFunctors_用于排队待处理函数到loop线程执行，queueSize()获取该vector大小；cancel()取消定时任务。

对于3)，updateChannel()/removeChannel()/hasChannel()用于通道更新/移除/判断，vector activeChannels_存储当前所有激活的通道，currentActiveChannel_存储当前正在处理的激活通道；

对于4)，isInLoopThread()/assertInLoopThread()判断/断言 当前线程是创建当前EventLoop对象的线程，互斥锁mutex_用来做互斥访问需要保护数据。

## **保证one loop per thread**
之前提到了muduo网络库是one loop per thread的，那么如何保证这一点。在muduo里，EventLoop是对loop的封装，那么只需要保证每个线程里只有一个EventLoop对象就可以。
```cpp
__thread EventLoop* t_loopInThisThread = 0;

EventLoop::EventLoop()
//...
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  //...
}
```
EventLoop.cc里用__thread声明了一个线程局部的变量`t_loopInThisThread`，因此，每个线程都有一个独立的`t_loopInThisThread`变量，该EventLoop指针指向该线程拥有的EventLoop对象。在调用EventLoop的构造函数时，也会检查该指针，如果该指针不为空，则说明此线程已经拥有一个EventLoop对象了，因此报错，否则的话，把当前EventLoop对象的this指针赋给`t_loopInThisThread`。

## **线程检查**
在muduo网络库的设计中，EventLoop的大部分成员函数都只能在所属线程中执行，因此，提供了isInLoopThread()函数和assertInLoopThread()函数用来判断和断言当前线程是否为EventLoop对象所属的线程。
```cpp
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
```

## **loop循环**
EventLoop最重要的功能就是实现loop循环，不断监听事件，处理事件，实现在loop成员函数中。
```cpp
void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_;
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true;
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    doPendingFunctors();
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}
```

loop()函数会执行while循环，在每次循环中，首先清空activateChannels_，然后执行poller_的poll成员函数，来阻塞线程，监听所有的通道，直到有事件发生，poller_会将激活的通道填入activateChannels_。

之后依次对activateChannels_中的激活通道进行handleEvent，所有通道处理完后，再执行doPendingFunctors()，这个函数会执行其他线程为本线程添加的一些任务，后面会详细介绍。

## **Channels管理**
EventLoop提供了updateChannel函数和removeChannel函数来管理channel。

这两个成员函数实际上是通过调用poller_的updateChannel和removechannle来完成实际的update操作和remove操作。

在进行实际的操作之前会进行一系列的断言来确保Channel确实属于该EventLoop同时该操作确实在EventLoop所属线程中执行。
对于removeChannel，还需要确定待删除的Channel不能在activateChannels_中，或者是虽然在activateChannels_中，但是该Channel就是正在处理的Channle，后一种条件对应的可能的情况是，Channel的handleEvent恰好调用了removeChannel函数。
```cpp
void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}
```
EventLoop还提供了hasChannel函数来检查当前poller_中是否还有Channel。
```cpp
bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}
```

## **管理定时任务**
EventLoop有一系列的成员函数来添加和删除定时任务。
```cpp
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}
```
runAt、runAfter和runEvery用来添加定时任务。其原理都是通过执行定时器队列timerQueue_的addTimer成员函数来添加新的Timer对象，并设置好执行时间和执行周期间隔以及对应的回调函数。

三个函数都会返回新增的Timer对象对应的TimerId，每个Timer对象由唯一的TimerId来标识。

cancel成员函数用来删除指定的Timer对象。
```cpp
void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}
```
## **保证用户任务只在本线程中执行**
muduo库的设计里，要求每个EventLoop的用户任务都在EventLoop所属的线程里执行，但是存在一种情况，另一个线程会为本线程的EventLoop分配一个用户任务去执行，如何保证这个用户任务在EventLoop所属的线程中执行。

EventLoop提供了runInLoop和queueInLoop成员函数。
```cpp
void EventLoop::runInLoop(Functor cb)
{
  if (isInLoopThread())
  {
    cb();
  }
  else
  {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}
```

当需要为EventLoop添加一个用户任务时，可以调用runInLoop成员函数，传入一个Functor。此时，先判断当前线程是不是EventLoop所属线程，如果是，那么直接执行该任务，如果不是，则会调用queueInLoop函数，将任务加入到任务队列中。

EventLoop有一个pendingFunctors_的成员变量来作为任务队列， queueInLoop会将用户任务添加到pendingFunctors_中，并通过wakeup函数唤醒EventLoop所属线程，wakeup函数后面详细介绍。

需要注意的是，此处的pendingFunctors_可能会暴露给多个线程，因此需要通过加锁来保护，保证线程安全，这也是整个EventLoop中唯一一个需要进行加锁保护的成员变量。

## **唤醒机制**
由于loop需要epoll_wait来监听事件，因此，线程在执行loop函数时很大可能是在阻塞状态，如果此时其他线程向EventLoop添加了用户任务，此时阻塞状态的线程是无法得知的，因此，需要添加唤醒机制来唤醒线程。

EventLoop提供了wake_up成员函数来唤醒线程，原理是，我们为EventLoop添加一个wakeupChannel_，它负责一个名为wakeupFd_的eventfd，eventfd是linux2.6以后的一个事件通知机制，可以通过向eventfd写来通知事件发生，eventfd也可以和其他fd一样加入epoller中，因此可以像监听其他fd一样来监听eventfd的readable事件。
```cpp
EventLoop::EventLoop()
  : //...
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this, wakeupFd_)),
    //...
{
  //...
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}
```
EventLoop的构造函数通过createEventfd来初始化wakeupFd_，之后再用wakeupFd_来初始化wakeupChannel_。

之后为wakeupChannel设置ReadCallback，并调用enableReading成员函数，关注可读事件。这里enableReading会调用Channel的update成员函数，然后update成员函数又会调用EventLoop的updateChannel成员函数，接着又会调用poller的updateChannel函数，此时会将eventfd加入到epoll实例中，开始监听eventfd。（开始一直没找到显式的将wakeupChannel_加入到poller的过程，一直以为会有个updateChannel(wakeupChannel_)的类似语句）。

当其他线程需要为EventLoop所属线程添加任务时，需要立马唤醒本线程，此时调用wake_up成员函数，其实就是向wakeupFd_写入8个字节。
```cpp
void EventLoop::wakeup()
{
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}
```
此时，poller在wakeupFd_上监听到可读事件，loop函数不再阻塞，而wakeupChannel的ReadCallback其实就是读取这8个字节，让wakeupFd_的计数归零。
```cpp
void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}
```
其他线程在为本线程添加用户任务时，会调用runInLoop成员函数，继而调用queueInLoop成员函数，继而调用wakeup函数，唤醒线程。
```cpp
void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}
```
这里解释下queueInLoop中调用wakeup的两个条件，第一个很好理解，当执行线程不是EventLoop所属线程时，唤醒线程；第二个条件callingPendingFunctors_为真，此时说明正在执行doPendingFunctors函数，也就是在执行任务队列中的任务，这些任务中可能会导致线程阻塞，此时需要唤醒线程来尽快处理新加的任务。（第二种情况没太搞懂。）

再介绍一下doPendingFunctors函数。
```cpp
void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }

  for (const Functor& functor : functors)
  {
    functor();
  }
  callingPendingFunctors_ = false;
}
```
这里，并没有直接遍历pendingFunctors_，而是先swap到functors中，再遍历functors。一方面，这样可以减少临界区的长度（临界区过长，会阻塞别的线程调用queueInLoop函数），另一方面，也可以避免死锁（如果某个functor调用了queueInLoop，那么会两次请求mutex_，造成死锁）。

此外，quit函数也会调用wakeup函数：
```cpp
void EventLoop::quit()
{
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread())
  {
    wakeup();
  }
}
```
如果是其他线程希望EventLoop退出循环，那么在将quit_置为true后，还需要调用wakeup函数唤醒线程。