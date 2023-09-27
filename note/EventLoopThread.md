# EventLoopThread详解
muduo一直在强调one loop per thread，为了方便线程创建并为每个线程创建相应的EventLoop对象并运行loop，muduo定义了EventLoopThread类，它的作用就是启动一个IO线程，并创建局部EventLoop对象，并在新线程中启动loop循环。
```cpp
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();

 private:
  void threadFunc();

  EventLoop* loop_ GUARDED_BY(mutex_);
  bool exiting_;
  Thread thread_;
  MutexLock mutex_;
  Condition cond_ GUARDED_BY(mutex_);
  ThreadInitCallback callback_;
};
```
私有变量中，loop_是一个EventLoop指针，指向本IO线程对应的EventLoop对象。thread_是一个Thread对象，用于创建新线程。threadFunc则是该IO线程执行的函数。mutex_和cond_则用于IO线程创建过程中的同步。callback_则是一个线程初始化回调函数，可以在创建线程前设置。

startLoop函数是最关键的函数，该函数会启动线程，并且等待线程创建和运行成功后，返回EventLoop对象的指针。

先看构造函数：
```cpp
EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),
    exiting_(false),
    thread_(std::bind(&EventLoopThread::threadFunc, this), name),
    mutex_(),
    cond_(mutex_),
    callback_(cb)
{
}
```
在构造函数中，会用threadFunc函数来初始化thread_，也就是设置好新IO线程的执行函数。除此之外，构造函数还可以传递一个回调函数设置为线程初始化回调函数，在线程启动后会执行。

关键函数startLoop：
```cpp
EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());
  thread_.start();

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      cond_.wait();
    }
    loop = loop_;
  }

  return loop;
}
```
startLoop做的事就是启动线程，然后等待线程执行并创建好EventLoop对象后，将EventLoop对象指针返回，这里需要使用条件变量cond_来等待。

threadFunc函数：
```cpp
void EventLoopThread::threadFunc()
{
  EventLoop loop;

  if (callback_)
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);
    loop_ = &loop;
    cond_.notify();
  }

  loop.loop();
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;
}
```
threadFunc就是新线程要执行的函数，首先在栈空间上创建EventLoop对象，之后，如果有初始化回调函数则执行，之后将EventLoop对象指针赋给loop_，并notify条件变量，通知startLoop函数EventLoop对象创建完成，最后执行loop函数。

# EventLoopThreadPool类详解
muduo支持多个IO线程，因此实现了EventLoopThreadPool类，可以创建和管理一个IO线程池。
```cpp
class EventLoopThreadPool : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
  ~EventLoopThreadPool();
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  EventLoop* getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
  EventLoop* getLoopForHash(size_t hashCode);

  std::vector<EventLoop*> getAllLoops();

  bool started() const
  { return started_; }

  const string& name() const
  { return name_; }

 private:

  EventLoop* baseLoop_;
  string name_;
  bool started_;
  int numThreads_;
  int next_;
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop*> loops_;
};
```
私有变量里，baseLoop_指向主线程的EventLoop对象；name_是线程池的名字，线程池内线程的名字也会依赖于name_；numThreads是线程池内线程的数量，可以在线程池启动前通过setThreadNum来设置；next_是一个下标，表示当需要为线程池分配Channel时应该选取的下一个线程；threads_存储EventLoopThread对象指针，loops_存储EventLoop对象指针。

EventLoopThreadPool的构造和析构没有太多要介绍的，私有成员都是标准库对象，无需手动释放，同时loops_内的EventLoopThread指针也无需释放，因为这些EventLoopThread对象都是定义在对应IO线程的栈空间上的。

start函数用来启动IO线程池：
```cpp
void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
  assert(!started_);
  baseLoop_->assertInLoopThread();

  started_ = true;

  for (int i = 0; i < numThreads_; ++i)
  {
    char buf[name_.size() + 32];
    snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
    EventLoopThread* t = new EventLoopThread(cb, buf);
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
    loops_.push_back(t->startLoop());
  }
  if (numThreads_ == 0 && cb)
  {
    cb(baseLoop_);
  }
}
```
start函数会根据numThreads_来创建EventLoopThread对象，存进threads_中，之后调用EventLoopThread对象的startLoop成员函数来启动线程，并获得EventLoop对象的指针，存进loops_中。

getNextLoop函数可以用来为线程池分配任务：
```cpp
EventLoop* EventLoopThreadPool::getNextLoop()
{
  baseLoop_->assertInLoopThread();
  assert(started_);
  EventLoop* loop = baseLoop_;

  if (!loops_.empty())
  {
    // round-robin
    loop = loops_[next_];
    ++next_;
    if (implicit_cast<size_t>(next_) >= loops_.size())
    {
      next_ = 0;
    }
  }
  return loop;
}
```
当需要分配任务时，例如需要将一个新的channel加入到某个线程的EventLoop中时，如何选择线程来保证各个线程的负载尽量均衡。一个很简单的做法就是round-robin，使next_下标循环增长，如果线程池大小为0，那么直接选择baseLoop_。

