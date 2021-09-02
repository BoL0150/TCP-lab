#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;
    //TCPsender已发送未确认的segments队列（即outstanding segments）
    std::queue<TCPSegment> _segments_out{};
    //重传超时的初始值,在TCPsender被构造时会被初始化
    unsigned int _initial_retransmission_timeout;
    //重传超时,初始值等于_initial_retransmission_timeout
    uint32_t _RTO;
    //当计时器超时时，_RTO是否需要翻倍
    bool _back_off = true;
    //可以发送的字节流
    ByteStream _stream;
    //要发送的下一个字节的absolute seqno
    uint64_t _next_seqno{0};
    //记录发送窗口中的字节数量
    uint64_t _bytes_in_flight{0};
    //已发送未确认的segments
    std::queue<TCPSegment> _outstanding_segments{};
    //计时器是否启动
    bool _timer_running = false;
    //超时计时器，如果到达RTO，则重传最老的segment
    uint32_t _time_elipsed = 0;
    //连续重传的次数
    uint16_t _consecutive_retransmissions = 0;
    //保存TCPReceiver报文段携带的接收窗口大小,也就是发送窗口的最大值
    //初始值为1.如果接收到的值为0，也视为1.
    uint16_t _receiver_window_size = 1;
    //TCPReceiver接收窗口的空闲空间大小
    uint16_t _receiver_free_space = 0;
    //是否发送了syn
    bool _syn_sent = false;
    //是否发送了fin
    bool _fin_sent = false;
    //发送segment的辅助方法，封装所有发送segments都要做的操作
    void _send_segments(TCPSegment & seg);

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //!"Input" interface for the writer
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    
    //接收TCPReceiver返回的ackno和window size
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);
    //判断接收的absolute ack是否合法
    bool valid_ack(uint64_t abs_ack);

    //!生成一个负载为空的segment（用于创建空的 ACK segment）
    void send_empty_segment();

    //!创建并发送segments以尽可能多地填充接收窗口
    void fill_window();

    //!通知 TCPSender 时间的流逝
    void tick(const size_t ms_since_last_tick);
    //! 已发送未确认的字节数是多少，SYN和FIN也占一个字节
    size_t bytes_in_flight() const;

    //!连续重传次数
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,which will need to fill in the fields that are set by the TCPReceiver(ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    
    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
