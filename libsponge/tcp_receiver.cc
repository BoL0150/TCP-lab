#include "tcp_receiver.hh"
#include "tcp_helpers/tcp_header.hh"
#include "tcp_helpers/tcp_segment.hh"
#include "wrapping_integers.hh"
// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//处理接收到的TCPsegment
void TCPReceiver::segment_received(const TCPSegment &seg) {
    // 提取出segment的头
    TCPHeader header = seg.header();
    //如果没有接收到syn，处于listen状态，只接收syn
    if(!header.syn && !_syn)return;
    //如果已经接收到了syn，就拒绝接收其他的syn
    if(header.syn && _syn) return;
    // 接收syn，并记录该段的seqno，seqno就是ISN
    if(header.syn){
        _syn = true;
        _isn = header.seqno.raw_value();
    }
    // 如果接收到了syn，就可以接收fin
    if(_syn && header.fin)
        _fin = true;
    // 将接收到的segment的32位seqno和ISN一起转换为64位的absolute seqno
    size_t absolute_seqno = unwrap(header.seqno,WrappingInt32( _isn ),_reassembler.first_unassembled());
    uint64_t stream_indices = header.syn ? 0 : absolute_seqno -1;
    // 将载荷、序列号和fin标志送入流重组器中
    _reassembler.push_substring(seg.payload().copy(),stream_indices,header.fin); 

}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    //如果没有收到syn，就返回空
    if(!_syn)return {};
    //uint64_t first_unassembled = stream_out().buffer_size();
    //如果之前收到了syn，也收到了fin，同时fin被读取进ByteStream（也就是说此时
    //StreamReassembler中未重组的字节数为0) 。则ackno等于stream indices加2（因为
    //first_unassembled前面多出了syn和fin）
    if(_fin && _reassembler.unassembled_bytes() == 0)
        return wrap(_reassembler.first_unassembled() + 2,WrappingInt32(_isn));
    // 如果之前只收到了syn，则则ackno等于stream indices加1（因为first_unassembled前面多出了syn）
    return wrap(_reassembler.first_unassembled() + 1,WrappingInt32(_isn));
}

size_t TCPReceiver::window_size() const { 
    return _capacity - stream_out().buffer_size();
}
