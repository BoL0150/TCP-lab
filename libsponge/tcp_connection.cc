#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received;}

// 由操作系统调用，接收从UDP或IP数据报中的解封装的TCPsegment
void TCPConnection::segment_received(const TCPSegment &seg) { 
    // 如果连接断开了，不接收任何segment
    if(!_active){
        return;
    }
    // 接收到一个segment，重置计数
    _time_since_last_segment_received = 0;
    // 被动建立连接的一方可能处于的状态,处于listen状态
    // 没有收到过任何segment，也没有发送过任何segment,
    if(!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0){
        // 只接收syn
        if(!seg.header().syn){
            return;
        }
        _receiver.segment_received(seg);
        // 收到对方的syn，就发送SYN与对方建立连接，处于SYN_RECV状态
        // 三次握手的阶段二
        connect();
        return;
    }
    // 主动建立连接的一方可能处于的状态,处于SYN_SENT状态，三次握手的阶段一
    // 发送出去的流没有得到确认，也没有收到过对方的segment。
    if(_sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute() && 
       !_receiver.ackno().has_value()){
        // 如果有效载荷不为0，不符合SYN，直接丢弃
        if(seg.payload().size() ){
            return;
        }
        // 如果ack等于0，则双方同时发起了建立连接
        if(!seg.header().ack){
            if(seg.header().syn){
                _receiver.segment_received(seg);
                // 发送空的segment，以返回ack
                _sender.send_empty_segment();
            }
            return;
        }
        // 如果syn=1，ack=1，rst=1，则关闭连接
        if(seg.header().rst){
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;
            return;
        }
    }
    // 如果syn=1，ack=1，rst!=1，或者其他情况
    _receiver.segment_received(seg);
    _sender.ack_received(seg.header().ackno,seg.header().win);
    // 发送确认的报文，进入ESTABLISHED状态，连接建立。处于三次握手的第三阶段
    if (_sender.stream_in().buffer_empty() && seg.length_in_sequence_space())
        _sender.send_empty_segment();
    if (seg.header().rst) {
        _sender.send_empty_segment();
        unclean_shutdown();
        return;
    }
    send_sender_segments();
}

bool TCPConnection::active() const { return _active;}

size_t TCPConnection::write(const string &data) {
    if(data.size() == 0){
        return 0;
    }
    // 向TCPSender的ByteStream中写入数据
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    // 对TCPSender中的segment设置ackno和windowsize,再发送给对等端
    send_sender_segments();
    return write_size;
}
// 对TCPSender的 _segments_out中的segment设置首部的ackno和windowsize字段，还有ACK标志位
// 再加入到TCPConnection的 _segments_out，真正地将TCPsegment发送出去
void TCPConnection::send_sender_segments(){
    // 此处必须要是引用类型，才能指向_sender中的同一个成员变量,才能对其进行操作
    // std::queue<TCPSegment>&sender_segs_out = _sender.segments_out();

    // 对TCPSender的 _segments_out进行遍历，将所有的segment的头部都加上ackno和windowsize
    // 再发送出去
    while(!_sender.segments_out().empty()){
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        // 只有当ackno()的返回值非空时，才需要加上
        if(_receiver.ackno().has_value()){
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        // 将segment真正发送出去
        _segments_out.push(seg);
    }
    // 每次发送segment后，都需要判断是否需要干净关闭连接
    clean_shutdown();
    
}
// 此方法被OS周期性调用
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if(!_active){
        return;
    }
    _time_since_last_segment_received += ms_since_last_tick;
    // 告知TCPSender过去的时间
    _sender.tick(ms_since_last_tick);
    // 如果连续重传的次数超过上限，则强制关闭连接
    if(_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS){
        unclean_shutdown();    
    }
    send_sender_segments();
}
// 不干净的关闭，直接强制关闭连接
// 将输入输出流设置为错误状态
// 将连接的active置为false，向对等方发送rst
void TCPConnection::unclean_shutdown(){
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg.header().ack = true;
    if(_receiver.ackno().has_value()){
        seg.header().ackno = _receiver.ackno().value();
    }
    seg.header().win = _receiver.window_size();
    seg.header().rst = true;
    _segments_out.push(seg);

}
// 干净关闭连接，判断能否干净地关闭连接，
// 判断是否需要在两个流结束后linger一段时间
void TCPConnection::clean_shutdown(){
    // 如果receiver已经收到了对等端的fin，StreamReassembler为空
    if(_receiver.stream_out().input_ended()){
        // 如果sender的输出流还没有结束，即ByteStream不为空，fin还没有发送出去
        // 那么需要在两个流结束后linger一段时间
        if(!_sender.stream_in().eof()){
            _linger_after_streams_finish = false;
        // 如果sender发送了fin，且得到了确认
        }else if(_sender.bytes_in_flight() == 0){
            // 那么只有不需要linger或者linger了指定时间后，才能断开连接
            if(!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout){
                _active = false;
            }
        }
    }
}
// 结束向TCPConnection中写入，也就是关闭输出流（仍然允许读取输入的数据）
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // 发送fin，不能保证这一次能将fin发送出去，因为接收窗口有可能空间不够，ByteStream无法全部发送出去
    _sender.fill_window();
    send_sender_segments();
}

// 主动连接
void TCPConnection::connect() {
    _sender.fill_window();
    send_sender_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.send_empty_segment();
            unclean_shutdown();
            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
