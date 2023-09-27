# Connector类详解
Connector用于主动发起连接，是对socket connect(2)的一个封装，服务于TcpClient。
```cpp
class Connector : noncopyable,
                  public std::enable_shared_from_this<Connector>
{
 public:
  typedef std::function<void (int sockfd)> NewConnectionCallback;

  Connector(EventLoop* loop, const InetAddress& serverAddr);
  ~Connector();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  void start();  // can be called in any thread
  void restart();  // must be called in loop thread
  void stop();  // can be called in any thread

  const InetAddress& serverAddress() const { return serverAddr_; }

 private:
  enum States { kDisconnected, kConnecting, kConnected };
  static const int kMaxRetryDelayMs = 30*1000;  // 最大重试延迟
  static const int kInitRetryDelayMs = 500;     // 初始重试延迟

  void setState(States s) { state_ = s; }
  void startInLoop();
  void stopInLoop();
  void connect();
  void connecting(int sockfd);
  void handleWrite();
  void handleError();
  void retry(int sockfd);
  int removeAndResetChannel();
  void resetChannel();

  EventLoop* loop_;         //所属EventLoop
  InetAddress serverAddr_;  //服务端ip地址
  bool connect_; // atomic  
  States state_;  // FIXME: use atomic variable
  std::unique_ptr<Channel> channel_;
  NewConnectionCallback newConnectionCallback_;
  int retryDelayMs_;
};
```
Connector不会让用户使用，Connector的接口只会暴露给TcpClient使用。


Connector调用start函数来开始创建连接，start会在loop线程中调用startInLoop：
```cpp
void Connector::start()
{
  connect_ = true;
  loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}
```

startInLoop调用connect函数来真正创建连接：

```cpp
void Connector::startInLoop()
{
  loop_->assertInLoopThread();
  assert(state_ == kDisconnected);
  if (connect_)
  {
    connect();
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}

```
connect函数:
```cpp
void Connector::connect()
{
  int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
  int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
  int savedErrno = (ret == 0) ? 0 : errno;
  switch (savedErrno)
  {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
      // 正在创建连接
      connecting(sockfd);
      break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
      // 重试
      retry(sockfd);
      break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
      // 出错
      LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
      sockets::close(sockfd);
      break;

    default:
      LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
      sockets::close(sockfd);
      // connectErrorCallback_();
      break;
  }
}
```

connect先调用sockets::createNonblockingOrDie创建一个socket，然后使用socket::connect来向服务端请求连接，根据是否连接成功和失败后的错误码来采取不同措施。

由于我们使用的socket是非阻塞的，因此connect(2)函数调用后会直接返回，因此可能Tcp连接创建的过程中connect(2)已经返回了并且返回值为-1，此时我们可以根据错误码知道正在创建连接，则调用connecting函数，将sockfd加入到监听中，当sockfd可写时，则说明连接已经建立成功。

connect(2)函数可能还会导致一些错误码，标示连接虽然失败但是可以尝试再连接，这些错误码下，我们调用retry函数进行再次尝试。

还有一些错误码标示连接不可能建立成功，这种情况下我们直接关闭sockfd，然后退出。

connecting函数：
```cpp
void Connector::connecting(int sockfd)
{
  setState(kConnecting);
  assert(!channel_);
  channel_.reset(new Channel(loop_, sockfd));
  channel_->setWriteCallback(
      std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
  channel_->setErrorCallback(
      std::bind(&Connector::handleError, this)); // FIXME: unsafe

  // channel_->tie(shared_from_this()); is not working,
  // as channel_ is not managed by shared_ptr
  channel_->enableWriting();
}
```
connecting函数将sockfd设置为channel_负责的fd，为channel_设置好WriteCallback和ErrorCallback，并关注可写事件，这样当sockfd可写时就会回调handleWrite。

handleWrite函数:
```cpp
void Connector::handleWrite()
{
  LOG_TRACE << "Connector::handleWrite " << state_;

  if (state_ == kConnecting)
  {
    // 移除和重置channel_
    int sockfd = removeAndResetChannel();
    // 即使sockfd可写，也不一定连接成功，需要通过getsockopt再次确认
    int err = sockets::getSocketError(sockfd);
    if (err)
    {
      LOG_WARN << "Connector::handleWrite - SO_ERROR = "
               << err << " " << strerror_tl(err);
      // 出错重试 
      retry(sockfd);
    }
    else if (sockets::isSelfConnect(sockfd))
    {
      // 判断是否为自连接
      LOG_WARN << "Connector::handleWrite - Self connect";
      retry(sockfd);
    }
    else
    {
      // 连接成功
      setState(kConnected);
      if (connect_)
      {
        // 用户还希望创建连接
        newConnectionCallback_(sockfd);
      }
      else
      {
        // 用户通过stop取消创建连接
        sockets::close(sockfd);
      }
    }
  }
  else
  {
    // what happened?
    assert(state_ == kDisconnected);
  }
}
```
retry函数：
```cpp
void Connector::retry(int sockfd)
{
  sockets::close(sockfd);
  setState(kDisconnected);
  if (connect_)
  {
    LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
             << " in " << retryDelayMs_ << " milliseconds. ";
    loop_->runAfter(retryDelayMs_/1000.0,
                    std::bind(&Connector::startInLoop, shared_from_this()));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}
```

当连接创建失败后会调用retry来重试创建连接，retry先将之前创建的sockfd关闭，并将状态置为kDisconnected，之后通过EventLoop::runAfter来添加一个定时任务，在一定间隔后在loop线程中再次调用startInLoop函数，重新开始创建连接。重试间隔应该逐渐增大，直到达到最大时延，也就是每次retry后，retyrDelayMs_乘2。

# TcpClient详解
TcpClient和TcpServer类似，不过一个TcpClient只管理一个TcpConnection，有了Connector，可以很方便的向server发起连接请求。

```cpp
class TcpClient : noncopyable
{
 public:
  // TcpClient(EventLoop* loop);
  // TcpClient(EventLoop* loop, const string& host, uint16_t port);
  TcpClient(EventLoop* loop,
            const InetAddress& serverAddr,
            const string& nameArg);
  ~TcpClient();  // force out-line dtor, for std::unique_ptr members.

  void connect();
  void disconnect();
  void stop();

  TcpConnectionPtr connection() const
  {
    MutexLockGuard lock(mutex_);
    return connection_;
  }

  EventLoop* getLoop() const { return loop_; }
  bool retry() const { return retry_; }
  void enableRetry() { retry_ = true; }

  const string& name() const
  { return name_; }

  /// Set connection callback.
  /// Not thread safe.
  void setConnectionCallback(ConnectionCallback cb)
  { connectionCallback_ = std::move(cb); }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(MessageCallback cb)
  { messageCallback_ = std::move(cb); }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(WriteCompleteCallback cb)
  { writeCompleteCallback_ = std::move(cb); }

 private:
  /// Not thread safe, but in loop
  void newConnection(int sockfd);
  /// Not thread safe, but in loop
  void removeConnection(const TcpConnectionPtr& conn);

  EventLoop* loop_;         // 所属EventLoop
  ConnectorPtr connector_; // avoid revealing Connector // Connector由shared_ptr管理
  const string name_;
  ConnectionCallback connectionCallback_; // 连接回调
  MessageCallback messageCallback_;       // 消息回调
  WriteCompleteCallback writeCompleteCallback_; // 写完回调
  bool retry_;   // atomic
  bool connect_; // atomic
  // always in loop thread
  int nextConnId_;
  mutable MutexLock mutex_;
  TcpConnectionPtr connection_ GUARDED_BY(mutex_); // TcpConnection对象
};
```
TcpClient中包含了一个Connector对象，用于创建连接，TcpClient只管理一个TcpConnection对象，用shared_ptr来管理。
## 构造和析构函数
```cpp
TcpClient::TcpClient(EventLoop* loop,
                     const InetAddress& serverAddr,
                     const string& nameArg)
  : loop_(CHECK_NOTNULL(loop)),
    connector_(new Connector(loop, serverAddr)),
    name_(nameArg),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    retry_(false),
    connect_(true),
    nextConnId_(1)
{
  connector_->setNewConnectionCallback(
      std::bind(&TcpClient::newConnection, this, _1));
  // FIXME setConnectFailedCallback
  LOG_INFO << "TcpClient::TcpClient[" << name_
           << "] - connector " << get_pointer(connector_);
}
```
构造函数的参数分别为loop，serverAddr和nameArg，分别用于初始化loop_，connector_和name_，并使用默认的回调函数来初始化connectionCallback_和messageCallback_，之后用户也可以用set*Callback函数来重新设置这两个回调函数，retry_选项被默认设置为false，并将connector_的新建连接回调函数设置为TcpClient::newConnection函数。

```cpp
TcpClient::~TcpClient()
{
  LOG_INFO << "TcpClient::~TcpClient[" << name_
           << "] - connector " << get_pointer(connector_);
  TcpConnectionPtr conn;
  bool unique = false;
  {
    MutexLockGuard lock(mutex_);
    unique = connection_.unique();
    conn = connection_;
  }
  if (conn)
  {
    assert(loop_ == conn->getLoop());
    // FIXME: not 100% safe, if we are in different thread
    CloseCallback cb = std::bind(&detail::removeConnection, loop_, _1);
    loop_->runInLoop(
        std::bind(&TcpConnection::setCloseCallback, conn, cb));
    if (unique)
    {
      conn->forceClose();
    }
  }
  else
  {
    connector_->stop();
    // FIXME: HACK
    loop_->runAfter(1, std::bind(&detail::removeConnector, connector_));
  }
}
```
析构函数比较复杂，主要是TcpConnection的生命期太过模糊，需要保证在无人持有TcpConnection的情况下释放掉它。析构函数首先判断该TcpConnection是否由TcpClient单独持有（也有可能被用户持有），之后为TcpConnection设置关闭回调函数，具体为TcpConnection::connectDestroyed，然后如果TcpConnection是由TcpClient单独持有，则直接强制关闭该TcpConnection。
## 创建连接
TcpClient提供了connect函数来进行创建连接，其实就是调用connector_对象的start函数开始尝试建立连接。
```cpp
void TcpClient::connect()
{
  // FIXME: check state
  LOG_INFO << "TcpClient::connect[" << name_ << "] - connecting to "
           << connector_->serverAddress().toIpPort();
  connect_ = true;
  connector_->start();
}
```
在构造函数中，我们将newConnection设置为connector_的新建连接回调函数，因此，当connector_通过start函数成功建立连接后会调用该回调函数:
```cpp
void TcpClient::newConnection(int sockfd)
{
  loop_->assertInLoopThread();
  InetAddress peerAddr(sockets::getPeerAddr(sockfd));
  char buf[32];
  snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;

  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(loop_,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));

  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, _1)); // FIXME: unsafe
  {
    MutexLockGuard lock(mutex_);
    connection_ = conn;
  }
  conn->connectEstablished();
}
```
该函数做的事其实类似于TcpServer::newConnection，首先根据两端IP地址，sockfd等创建一个TcpConnection，并将TcpClient的各种*Callback_成员变量设置为该TcpConnection的各种回调函数，CloseCallback是固定的，为TcpClient::removeConnection，之后将TcpConnection对象交给connection_智能指针管理，并执行TcpConnection::connectEstablished，完成TcpConnection对象的创建。

removeConnection函数：

```cpp
void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  assert(loop_ == conn->getLoop());

  {
    MutexLockGuard lock(mutex_);
    assert(connection_ == conn);
    connection_.reset();
  }

  loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
  if (retry_ && connect_)
  {
    LOG_INFO << "TcpClient::connect[" << name_ << "] - Reconnecting to "
             << connector_->serverAddress().toIpPort();
    connector_->restart();
  }
}
```
removeConnection是TcpConnection的关闭回调函数，当TcpConnection关闭时会调用该函数，首先reset智能指针，放弃对该TcpConnection对象的管理，之后向loop线程中添加TcpConnection的销毁任务。

如果TcpClient设置了retry_选项，并且此时connect_为真，说明TcpClient还在尝试建立连接，那么调用connector_的restart函数，重新开始创建连接。

## 关闭连接
```cpp
void TcpClient::disconnect()
{
  connect_ = false;

  {
    MutexLockGuard lock(mutex_);
    if (connection_)
    {
      connection_->shutdown();
    }
  }
}
```
TcpClient提供了disconnect方法来关闭TcpConnection，首先将connect_置为false，之后判断是否存在TcpConnection对象，如果存在，则调用shutdown方法关闭它。

## 停止连接
```cpp
void TcpClient::stop()
{
  connect_ = false;
  connector_->stop();
}
```
TcpClient还提供了stop方法来停止创建连接，先将connect_置为false，然后调用connector_的stop方法停止尝试创建连接。
