#include "tcp_sender.hh"
#include <math.h>

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity 初始化ByteStream，容量为capacity
//! \param[in] retx_timeout 重传超时的初始值
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _RTO(retx_timeout)
    , _stream(capacity){}
    

//已发送未确认的字节数量
uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_flight ;
}

void TCPSender::fill_window() {
    //如果SYN没有发送，则发送SYN后返回
    if(!_syn_sent){
        TCPSegment seg;
        _syn_sent = true;
        seg.header().syn = true;
        _send_segments(seg);
        return;
    }
    //如果SYN发送了但是没有被确认，则返回，等待确认
    if(!_outstanding_segments.empty() && _outstanding_segments.front().header().syn)
        return;
    //如果发送了FIN，则返回
    if(_fin_sent)return;
   
    //计算之后_receiver_free_space的值可能为负，转换为uint16_t后就溢出了
    //实在想不出什么办法解决这个问题，只有出此下策
    _receiver_free_space = _receiver_window_size <= _bytes_in_flight ? 0 : _receiver_window_size - _bytes_in_flight;
    //如果字节流结束了，没有发送FIN，且发送窗口有空余空间，则发送FIN
    //注意，发送结束的标志并不只是ByteStream为空，还需要应用层结束输入
    if(_stream.eof() && _receiver_free_space >= 1){
        TCPSegment seg;
        seg.header().fin = true;
        _fin_sent = true;
        _send_segments(seg);
        return;
    }
    //接收窗口空余空间的大小
    _receiver_free_space = _receiver_window_size <= _bytes_in_flight ? 0 : _receiver_window_size - _bytes_in_flight;
    //只要ByteStream不为空且接收窗口有空余的空间,就可以继续发送segments
    while(!_stream.buffer_empty() && _receiver_free_space > 0){
        TCPSegment seg;
        //接收窗口空闲大小、ByteStream可以读取的字节数量以及 TCPConfig::MAX_PAYLOAD_SIZE三者对载荷的大小进行限制
        size_t temp = _receiver_free_space > TCPConfig::MAX_PAYLOAD_SIZE ? TCPConfig::MAX_PAYLOAD_SIZE : _receiver_free_space;
        size_t payload_size = _stream.buffer_size() > temp ?temp :_stream.buffer_size();
        seg.payload() = _stream.read(payload_size);
        
        //当发送窗口空间充足时，要求将FIN捎带在segment中
        if(_stream.eof() && _receiver_free_space >= 1 + seg.length_in_sequence_space()){
            seg.header().fin = true;
            _fin_sent = true;
        }
        _send_segments(seg);
        _receiver_free_space = _receiver_window_size <= _bytes_in_flight ? 0 : _receiver_window_size - _bytes_in_flight;
    }
    //由于我们直接将收到为0的window size视为1，所以此处省去了特殊判断
}
void TCPSender::_send_segments(TCPSegment &seg){
    seg.header().seqno = wrap(_next_seqno,_isn);
    //length_in_sequence_space计算segment的载荷部分和SYN和FIN的长度
    _next_seqno += seg.length_in_sequence_space();
    //更新发送窗口中的字节数量
    _bytes_in_flight += seg.length_in_sequence_space();
    //将segments发送出去，加入发送窗口
    _segments_out.push(seg);
    _outstanding_segments.push(seg);
    //当发送一个segment时，如果计时器没有启动，就启动一个计时器
    if(!_timer_running){
        _timer_running = true;
        _time_elipsed = 0;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    //收到的ackno是32位的seqno，而发送窗口中的是64位的absolute seqno，
    //所以需要将ackno进行unwrap
    //TCPReceiver对seqno进行unwrap的checkpoint是first unassembled，
    //因为TCPReceiver期望收到的seqno就是这个。
    //同理，TCPsender期望收到的ackno是_next_seqno，所以
    //对ackno进行unwrap的checkpoint是_next_seqno
    uint64_t absolute_ackno = unwrap(ackno,_isn,_next_seqno);
    //只接收部分ackno.此处的判断条件比较宽松,只要ackno在发送窗口范围中就可以
    if(!valid_ack(absolute_ackno)){
        return;
    }
    //根据TCPReceiver返回的信息更新TCPsender中保存的接收窗口大小
    this->_receiver_window_size = window_size ;
    //如果返回的window size为0，将其视为1，但是当计时器超时时，不需要进行指数退避
    if(window_size == 0){
        this->_receiver_window_size = 1;
        _back_off = false;
    }else
        _back_off = true;
    //将发送窗口中被确认的segment删除
    while(!_outstanding_segments.empty()){
        TCPSegment front_outstanding_segment = _outstanding_segments.front();
        uint64_t front_abs_seqno = unwrap(front_outstanding_segment.header().seqno,_isn,_next_seqno);

        //只有能够确认发送窗口中至少一整段segment（也就是说，ackno要大于等于发送窗口中
        //第二段segment的seqno，即第一段segment的seqno 加上 第一段segment的长度）的ackno，
        //才可以执行以下操作
        if(absolute_ackno >= front_abs_seqno + front_outstanding_segment.length_in_sequence_space()){
            
            //将重传超时RTO恢复为初始值
            _RTO = _initial_retransmission_timeout;
            //将连续重传的次数变为0
            _consecutive_retransmissions = 0;
            //将被确认的segments删除
            _outstanding_segments.pop();
            //更新发送窗口中的字节数量
            _bytes_in_flight -= front_outstanding_segment.length_in_sequence_space();
            //重置计时器
            _time_elipsed = 0;
        }else 
            break;
    }
    //如果发送窗口中没有任何未确认的segments，关闭计时器
    if(_outstanding_segments.empty()){
        _timer_running = false;
        _time_elipsed = 0;
    }
    //window size更新后，就可以继续发送新的segments,填充发送窗口
    fill_window();
}
//在此lab中，我们认为只有能确认发送窗口中一整段segment的ackno才算数
//此处的判断条件更加宽松一些，只要ackno大于第一段的seqno即可
bool TCPSender::valid_ack(uint64_t abs_ack){
    //获取发送窗口中最老的segment
    TCPSegment front_outstanding_segment = _outstanding_segments.front();
    //ackno需要小于等于_next_seqno（发送窗口的最后一个字节的下一个索引）,
    //并且大于第一段的seqno
    return abs_ack <= _next_seqno && 
        abs_ack >= unwrap( front_outstanding_segment.header().seqno ,_isn,_next_seqno) ; 
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    if(_timer_running){
        _time_elipsed += ms_since_last_tick;
        //计时器超时
        if(_time_elipsed >= _RTO){
            //重传已发送未确认的最早的segments
            _segments_out.push(_outstanding_segments.front());
            //如果window size不为0，跟踪连续重传的次数，将RTO的值翻倍
            if(_receiver_window_size != 0 && _back_off){
                _consecutive_retransmissions++;
                _RTO *= 2;
            }
            //重启计时器
            _time_elipsed = 0;
        }
    }
}


unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions;}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno,_isn);
    _segments_out.push(seg);
}
