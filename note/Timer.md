# Timer类详解
网络库需要负责定时任务，muduo库中定义了一系列类，Timer，TimerId，TimerQueue来实现定时功能。Timer类对应一个定时任务，保存了超时时刻以及对应的超时回调函数，并且定时任务还可以分为一次性和周期性的。
```cpp
class Timer : noncopyable
{
 public:
  Timer(TimerCallback cb, Timestamp when, double interval)
    : callback_(std::move(cb)),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(s_numCreated_.incrementAndGet())
  { }

  void run() const
  {
    callback_();
  }

  Timestamp expiration() const  { return expiration_; }
  bool repeat() const { return repeat_; }
  int64_t sequence() const { return sequence_; }

  void restart(Timestamp now);

  static int64_t numCreated() { return s_numCreated_.get(); }

 private:
  const TimerCallback callback_;
  Timestamp expiration_;
  const double interval_;
  const bool repeat_;
  const int64_t sequence_;

  static AtomicInt64 s_numCreated_;
};

```

callback_是超时回调函数，expiration_是超时时刻对应的时间戳，interval_是周期任务的间隔时间，repeat_标识该任务是否为周期性任务，sequence_是Timer的全局唯一序列号。

s_numCreated_是Timer类的静态私有变量，用来记录已经创建的Timer的数量，在创建新的Timer时，会通过它来获取序列号并自增，它是一个原子变量。

run函数会执行Timer的超时回调函数。restart函数会根据Timer是否为周期性任务来重新设置超时时刻:
```cpp
void Timer::restart(Timestamp now)
{
  if (repeat_)
  {
    expiration_ = addTime(now, interval_);
  }
  else
  {
    expiration_ = Timestamp::invalid();
  }
}
```
如果是周期性任务，那么将超时时刻设置为当前时间加上interval_，如果是一次性任务，那么将超时时刻设置为时间戳的非法值。

# TimerId类
TimerId类是用来唯一标识一个Timer的，定义很简单，成员只有一个Timer指针和一个Timer序列号。
```cpp
class TimerId : public muduo::copyable
{
 public:
  TimerId()
    : timer_(NULL),
      sequence_(0)
  {
  }

  TimerId(Timer* timer, int64_t seq)
    : timer_(timer),
      sequence_(seq)
  {
  }

  // default copy-ctor, dtor and assignment are okay

  friend class TimerQueue;

 private:
  Timer* timer_;
  int64_t sequence_;
};
```

# TimerQueue类详解
TimerQueue是muduo实现定时功能的最重要的一个类，它为用户提供外部接口，用来添加和删除Timer，并在内部高效地维护和管理Timer。
```cpp
class TimerQueue : noncopyable
{
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;
  const int timerfd_;
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  TimerList timers_;

  // for cancel()
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  ActiveTimerSet cancelingTimers_;
};
```

loop_指针指向TimerQueue所属的EventLoop；

由于整个muduo都是用poll/epoll框架来处理事件的，而linux中恰好有timerfd，可以很好地融合到poll/epoll框架中，来处理超时事件。timerfd可以和socket fd、eventfd一样被poller监听，因此，我们可以像之前一样，创建一个Channel来负责该timerfd，并交给poller来监听感兴趣事件，对于timerfd，当定时器到期时，会产生可读事件，此时poller便可以知道定时器到期，因此我们只需要关注timerfd的可读事件。

timerfd_成员便是TimerQueue中的timerfd，timerfdChannel_则是对应的Channel。

## Timer集合

TimerQueue有三个Timer集合，timers_,activetimers_,cancelingTimers_。其中timers_是一个pair<Timestamp,Timer*>的set，而后两个集合是pair<Timer*,int64_t>的set。

timers_中的Timer按照超时时间戳来排序，可能存在相同时间戳的Timer，因此使用pair<Timestamp,Timer*>来作为key，timers_可以以$O(logN)$的复杂度来找到超时的最近的一个Timer，以及插入删除Timer。

再来解释一下为什么已经有了timers_，还需要一个activeTimers_。在muduo库里，Timer是一个内部类，不向用户暴露，用户能看到的类是TimerId类，因此用户进行删除Timer时，传递的是TimerId，此时，TimerQueue需要根据TimerId，也就是Timer指针和序列号在activetimers_中查找到Timer，然后在timers_中根据时间戳和Timer指针进行删除。总而言之，activeTimers_的存在是为了帮助从timers_中手动删除Timers，从始至终，activeTimers_和timers_中的Timer数保持一致，Timer对象也保持一致，一起插入，一起删除。

cancelingTimers_的存在也是为了帮助手动删除Timer，具体原因在后面介绍。

## 构造和析构
```cpp
TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();
}
```
构造函数较为简单，初始化过程会通过createTimerfd来创建一个timerfd，其实是调用了timerfd_create库函数。timerfdChannel_则是用loop和timerfd_进行初始化。之后，会为timerfdChannel设置ReadCallback，并且关注可读事件。

```cpp
TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (const Entry& timer : timers_)
  {
    delete timer.second;
  }
}
```
析构函数主要是关闭timerfdChannel关注事件，并移除，同时关闭掉timerfd_，最后将所有Timer手动释放。

## 超时处理函数handleRead
构造函数将handleRead设为timerfd的ReadCallback，也就是timerfd_超时时，会触发读事件，然后会调用该回调函数。

```cpp
void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);

  std::vector<Entry> expired = getExpired(now);

  callingExpiredTimers_ = true;
  cancelingTimers_.clear();
  // safe to callback outside critical section
  for (const Entry& it : expired)
  {
    it.second->run();
  }
  callingExpiredTimers_ = false;

  reset(expired, now);
}

首先对timerfd_进行read，之后调用getExpired函数获取所有已经超时的Timer，之后，将`callingExpiredTimers_`置为true，代表正在处理超时Timer，并将`cancelingTimers_`清空，这样做的原因会在删除Timer部分介绍，之后依次处理每个Timer的超时回调。处理完之后将`callingExpiredTimers_`恢复，最后调用reset函数，将周期性Timer重新启动。

getExpired函数：
```cpp
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  TimerList::iterator end = timers_.lower_bound(sentry);
  assert(end == timers_.end() || now < end->first);
  std::copy(timers_.begin(), end, back_inserter(expired));
  timers_.erase(timers_.begin(), end);

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    size_t n = activeTimers_.erase(timer);
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;
}
```
该函数的功能是返回所有时间戳已经超过now的Timer，因为timers_是根据时间戳排序的，因此我们可以很方便的通过lower_bound找到大于等于now的Timer，这里注意，因为timers_的元素是pair<Timestamp,Timer*>，因此，需要利用now和reinterpret_cast<Timer*>(UINTPTR_MAX)构造出一个哨兵。

找到第一个大于等于now的Timer后，将前面所有的Timer复制到expired中，并从timers_和activeTimers_中删除对应的Timer，最后返回expired。

reset函数：

```cpp
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it.second->restart(now);
      insert(it.second);
    }
    else
    {
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);
  }
}
```
reset函数遍历expired中的Timer，对于周期性的Timer并且该Timer未被加入`cancelingTimers_`中（关于哪些Timer会被加入`cancelingTimers_`在后面删除Timer部分解释）时，我们将这些Timer进行restart，并且将Timer重新插入到TimerQueue中，对于其他的Timer，则直接进行delete。

最后将timers_中的第一个Timer的超时时间设置为timerfd_的超时时间。

## 增加Timer
TimerQueue提供的外部接口是addTimer：
```cpp
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(std::move(cb), when, interval);
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}
```
传入一个超时回调函数，以及超时时间戳，以及周期间隔。addTimer函数会创建一个新的Timer对象，然后在所属线程中执行真正的插入过程，也就是通过loop_->runInLoop执行addTimerInLoop函数，确保该函数是在loop_所属线程执行，最后返回TimerId。

addTimerInLoop函数：
```cpp
void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    resetTimerfd(timerfd_, timer->expiration());
  }
}
```
该函数会调用insert函数来向Timer集合中真正插入Timer，insert的返回值标示了该插入是否改变了最早的超时时间，如果改变了，则需要修改timerfd_的超时时间。

insert函数：
```cpp
bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  {
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  {
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}
```
insert会首先判断新增的Timer是否会改变timerfd的最早触发时间，当timers_为空时，或者添加的Timer的超时时间早于当前timers_的第一个Timer时，earliestChanged置为true。之后向timers_和activeTimers_中插入具体元素，最后返回earliestChanged。

## 删除Timer
TimerQueue提供的外部接口是cancel，传入的参数是TimerId，用来唯一标识一个Timer。

与addTimer类似，cancel也会调用loop_->runInLoop来执行真正的删除过程，确保删除过程在loop_所属线程执行。
```cpp
void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

```
cancelInLoop：
```cpp
void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)
  {
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}
```
cancelInLoop中，会先在activeTimers_中寻找到TimerId对应的Timer。如果在activeTimers_中找到了，那么从timers_和activeTimers_中删除该Timer，同时手动delete该Timer。

如果没有找到，那么存在两种情况，一种情况是TimerQueue中确实没有该Timer，可能是用户传入错误的TimerId或者是该Timer已经超时并处理过了，这种情况不需要处理；另一种情况是该Timer的expiration时间已经到了，getExpired函数已经获取过该Timer了，并且已经从Timer集合中删除掉该Timer了，但还未真正处理该超时任务。

**此时我们需要考虑一种特殊情况，该Timer是一个周期任务，那么在处理完所有的超时回调后，TimerQueue会restart该Timer，此时该Timer会重新回到Timer集合中，违背我们想删除该Timer的本意了。** 这里的做法是，将该Timer记录到`cancelingTimers_`中，再对超时Timer进行restart时会检查该Timer是否在`cancelingTimers_`中，如果在，那么即使该Timer是一个周期定时任务，我们也不对它进行restart，这也就是第三个Timer集合`cancelingTimers_`的真正作用。

那么为什么这里的判断条件是`callingExpiredTimers_`,仔细想想，**可能会产生上面所说情况的唯一可能，就是当handleRead在执行某个Timer的超时回调函数时，该超时回调函数又调用了cancleInLoop函数去删除某个已经expired的Timer。**

因为不管是handleRead还是cancleInLoop函数，都一定是在loop_所属线程中执行的，所以调用cancleInLoop函数的语句一定是在handleRead调用getExpired函数之后，在handleRead调用reset函数之前，因此一定是在Timer的run函数中。因此，muduo增添了`callingExpiredTimers_`变量，在getExpired之后将其置为true，在reset之前将其置为false，这样，只有当`callingExpiredTimers_`为true时，才可能会产生所说的特殊情况，此时需要将Timer加入到`cancelingTimers_`。