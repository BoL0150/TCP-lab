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

void TCPConnection::segment_received(const TCPSegment &seg) { 
    if(!_active){
        return;
    }
    _time_since_last_segment_received = 0;
    TCPHeader header = seg.header();
    // 如果设置了RST，直接中断连接
    if(header.rst){
        _receiver.stream_out()._error = false;
        _sender.stream_in()._error = false;
        _active = false;
        return;
    }
    _receiver.segment_received(seg);
    // 如果设置了ack标志位，则将ackno和window size传入TCPSender
    if(header.ack){
        _sender.ack_received(header.ackno,header.win);
    }
}

bool TCPConnection::active() const { return _alive;}

size_t TCPConnection::write(const string &data) {
    // 向TCPSender的ByteStream中写入数据
    size_t write_size = _sender.stream_out().write(data);
    _sender.fill_window();
    // 对TCPSender中的segment设置ackno和windowsize,再发送给对等端
    _set_ackno_and_winsize();
    return write_size;
}
// 对TCPSender的 _segments_out中的segment设置首部的ackno和windowsize字段
// 再加入到TCPConnection的 _segments_out，真正地将TCPsegment发送出去
void TCPConnection::_set_ackno_and_winsize(){
    // 此处必须要是引用类型，才能指向_sender中的同一个成员变量,才能对其进行操作
    std::queue<TCPSegment>&sender_segs_out = _sender.segments_out();

    // 对TCPSender的 _segments_out进行遍历，将所有的segment的头部都加上ackno和windowsize
    // 再发送出去
    while(!sender_segs_out.empty()){
        TCPSegment seg = sender_segs_out.front();
        sender_segs_out.pop();
        // 只有当ackno()的返回值非空时，才需要加上
        if(_receiver.ackno().has_value()){
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        // 将segment真正发送出去
        _segments_out.push(seg);
    }
    
}
//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { DUMMY_CODE(ms_since_last_tick); }

void TCPConnection::end_input_stream() {}

void TCPConnection::connect() {
    string empty;
    // 什么都不写入，只需要向对方发送SYN就行
    write(empty);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
