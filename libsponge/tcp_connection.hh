#ifndef SPONGE_LIBSPONGE_TCP_FACTORED_HH
#define SPONGE_LIBSPONGE_TCP_FACTORED_HH

#include "tcp_config.hh"
#include "tcp_receiver.hh"
#include "tcp_sender.hh"
#include "tcp_state.hh"

//! \brief A complete endpoint of a TCP connection
class TCPConnection {
  private:
    TCPConfig _cfg;
    // 初始化TCPReceiver
    TCPReceiver _receiver{_cfg.recv_capacity};
    // 初始化TCPSender
    TCPSender _sender{_cfg.send_capacity, _cfg.rt_timeout, _cfg.fixed_isn};
    // TCPConnection准备发送的TCPsegment的输出队列
    // TCPSender中 _segments_out 中的segment不包括ackno和window size，不是TCPConnection真正发送出去的TCPsegment,
    // 需要与TCPReceiver的ackno()和window_size()的返回值结合在一起，再放入到TCPConnection 中的 _segments_out 中。
    // 我们可以认为放入此队列的segment已经发送出去了，但是实际上需要由拥有者或OS将该队列中的segment pop出，然后一一
    // 封装进更底层的IP数据报或UDP，此时才真正发送出去了
    std::queue<TCPSegment> _segments_out{};

    // TCPConnection是否alive
    bool _alive{true};
    // TCPConnection是否应该在两个流结束后保持alive 10 * _cfg.rt_timeout的时间
    bool _linger_after_streams_finish{true};

  public:
    //! 提供给应用层writer的输入接口

    // 通过发送SYN segment 初始化连接
    void connect();

    // 应用层向输出流写入数据，如果有可能的话通过TCP发送
    // 返回实际写入的数据的字节数
    size_t write(const std::string &data);

    // 返回应用层可以向ByteStream写入的字节数,即ByteStream的空闲空间大小
    //! \returns the number of `bytes` that can be written right now.
    size_t remaining_outbound_capacity() const;

    // 结束向TCPConnection中写入，也就是关闭输出流（仍然允许读取输入的数据）
    //! \brief Shut down the outbound byte stream (still allows reading incoming data)
    void end_input_stream();

    // 提供给应用层reader的输出接口
    //! \name "Output" interface for the reader
    //!@{

    // 已经从对等端收到的输入字节流
    //! \brief The inbound byte stream received from the peer
    ByteStream &inbound_stream() { return _receiver.stream_out(); }
    //!@}

    //! \name Accessors used for testing

    //!@{
    //! \brief number of bytes sent and not yet acknowledged, counting SYN/FIN each as one byte
    size_t bytes_in_flight() const;
    //! \brief number of bytes not yet reassembled
    size_t unassembled_bytes() const;
    //! \brief Number of milliseconds since the last segment was received
    size_t time_since_last_segment_received() const;
    //!< \brief summarize the state of the sender, receiver, and the connection
    TCPState state() const { return {_sender, _receiver, active(), _linger_after_streams_finish}; };
    //!@}

    // 由拥有者或操作系统调用的方法
    //! \name Methods for the owner or operating system to call
    //!@{

    // 当从网络中接收到一个segment时调用
    //! Called when a new segment has been received from the network
    void segment_received(const TCPSegment &seg);

    // 当时间流逝时，周期性调用
    //! Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    // 返回TCPConnection想要输出的TCPSegment队列。
    // 拥有者或OS会将此队列中的segment全部出队, 然后将每一个TCPSegment
    // 放入低层次的数据报中（通常是IP数据包，也可以是UDP）,并真正地发送出去
    std::queue<TCPSegment> &segments_out() { return _segments_out; }

    // 连接是否alive,返回true如果流还在运行，或者流结束后TCPConnection在linger
    bool active() const;
    //!@}

    // 从_cfg配置构造一个TCPConnection
    explicit TCPConnection(const TCPConfig &cfg) : _cfg{cfg} {}

    //! \name 构造函数和析构函数
    // 允许移动，不允许拷贝，不允许默认构造

    //!@{
    // 如果连接还存活析构函数就发送一个RST
    ~TCPConnection();  
    TCPConnection() = delete;
    TCPConnection(TCPConnection &&other) = default;
    TCPConnection &operator=(TCPConnection &&other) = default;
    TCPConnection(const TCPConnection &other) = delete;
    TCPConnection &operator=(const TCPConnection &other) = delete;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_FACTORED_HH
