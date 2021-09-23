#ifndef SPONGE_LIBSPONGE_TCP_RECEIVER_HH
#define SPONGE_LIBSPONGE_TCP_RECEIVER_HH

#include "byte_stream.hh"
#include "stream_reassembler.hh"
#include "tcp_helpers/tcp_segment.hh"
#include "wrapping_integers.hh"

#include <optional>

//! \brief The "receiver" part of a TCP implementation.

//! Receives and reassembles segments into a ByteStream, and computes
//! the acknowledgment number and window size to advertise back to the
//! remote TCPSender.
class TCPReceiver {
    //! Our data structure for re-assembling bytes.
    StreamReassembler _reassembler;

    //! The maximum number of bytes we'll store.
    size_t _capacity;
    bool _syn = false;
    bool _fin = false;
    size_t _isn = 0;
    uint64_t _checkpoint = 0;


  public:
    //构造一个 `TCPReceiver`，最多可以存储 `capacity` 字节，实际上就是STreamReassembler的大小
    TCPReceiver(const size_t capacity) : _reassembler(capacity), _capacity(capacity) {}

    	//向远程TCPsender提供反馈

    //返回包含ackno的optional<WrappingInt32>，如果ISN还没有初始化（即没有收到SYN）则返回空。 
    //ackno是接收窗口的开始，或者说接收者没有接收到的流中第一个字节的序列号
    std::optional<WrappingInt32> ackno() const;

    //应该发送给对方的接收窗口大小
    //capacity减去 TCPReceiver 在其BYteStream中保存的字节数（那些已重组但被应用层读取的字节数）。
    //也相当于first unassembled索引（即ackno）和first unacceptable索引的距离
    size_t window_size() const;

    //!已存储但尚未重组的字节数
    size_t unassembled_bytes() const { return _reassembler.unassembled_bytes(); }

    //处理接收到的TCPsegment
    void segment_received(const TCPSegment &seg);

    //返回StreamReassembler中的已重组的字节流ByteStream
    ByteStream &stream_out() { return _reassembler.stream_out(); }
    const ByteStream &stream_out() const { return _reassembler.stream_out(); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_RECEIVER_HH
