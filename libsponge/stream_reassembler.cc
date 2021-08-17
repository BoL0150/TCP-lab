#include "stream_reassembler.hh"
#include <iostream>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    segment seg={index,data,eof};
    //如果收到的字符串为空，不需要对它进行任何处理，只需要接收它的eof信息即可
    if(data.length()==0){
        if(eof==true)_eof=eof;
    }else{
        //对收到的segment进行剪切，符合可接受的范围
        if(_cut(seg)==false) return;
        //将剪切剩下的segment去重后再加入StreamReassembler的缓冲区
        _handle_overlap(seg);
        //将StreamReassembler中与ByteStream中的子串连续的segment写入ByteStream中
        _push_into_bytestream();
    }

    if(_segs_to_be_reassembled.empty() == true && _eof == true)
        _output.end_input();

   }
void StreamReassembler::_push_into_bytestream(){
    if(_segs_to_be_reassembled.empty() == true)return;
    auto smallest_seg= _segs_to_be_reassembled.begin();
    if(smallest_seg->index > _next_index)return;
    if(smallest_seg->index == _next_index){
        _output.write(smallest_seg->data);
        _next_index += smallest_seg->data.length();
        _segs_to_be_reassembled.erase(smallest_seg);
    }
    _push_into_bytestream();
}
//传入segment，进行剪切，当segment的索引超出了范围，直接结束，返回false，剪切失败。
bool
StreamReassembler::_cut(segment &s){
    size_t index=s.index;
    size_t first_unacceptable=_next_index+_output.remaining_capacity();
    size_t length=s.data.length();
    //最后一个字符的索引位置
    size_t last_index = index + length - 1;
    

    if(index>=first_unacceptable) return false;
    else if(last_index <_next_index) return false;
    else if(index < _next_index && last_index >= first_unacceptable){
        s.data = s.data.substr(_next_index - index,first_unacceptable - _next_index);
        s.index = _next_index;
    }
    else if(index<first_unacceptable && last_index >= first_unacceptable){
        s.data=s.data.substr(0,first_unacceptable - index);
    }
    else if(index<_next_index && last_index >=_next_index){
        s.data = s.data.substr(_next_index - index,last_index - _next_index + 1);
        s.index = _next_index;
        if(s.eof==true)_eof=true;
    }
    else if(index >= _next_index && last_index < first_unacceptable)
        if(s.eof == true) _eof = true;
    return true;
}

void StreamReassembler::_handle_overlap(segment s){
    if(_segs_to_be_reassembled.empty() == true){ 
        _segs_to_be_reassembled.insert(s);
        return;
    }
    for(auto it=_segs_to_be_reassembled.begin();it != _segs_to_be_reassembled.end();){
        //S与缓冲区中的子串重叠的情况,合并S与子串，将该子串从缓冲区中移除
        if(( s.index >= it->index && s.index <= it->index + it->data.length() - 1) ||
           (it->index >= s.index && it->index <= s.index +s.data.length() - 1)){
           s = _merge_seg(s,*it); 
           it = _segs_to_be_reassembled.erase(it);
        }else it++;
    }
    _segs_to_be_reassembled.insert(s);
}
//合并重叠的子串
StreamReassembler::segment
StreamReassembler::_merge_seg(segment s,segment other){
    size_t s_left = s.index;
    size_t s_right = s_left + s.data.length() -1;
    size_t other_left = other.index;
    size_t other_right = other_left +other.data.length() -1;

    size_t overlap_left_index= s_left >other_left ?s_left :other_left;
    size_t overlap_right_index= s_right < other_right ? s_right :other_right;
    //S是other的一部分，完全重叠
    if(s_left >= other_left && s_right <= other_right){
        string other_front = other.data.substr(0,overlap_left_index - other_left);
        string other_back  = other.data.substr(overlap_right_index - other_left + 1,other_right - overlap_right_index);
        s.data = other_front + s.data + other_back;
        s.index = other.index;
    }
    //重叠的部分在other的后半部分，S的前半部分
    else if(s_left >= other_left && s_left <= other_right ){
        //切割other
        other.data = other.data.substr(0,overlap_left_index -other_left );
        s.data = other.data + s.data;
        s.index = other.index;
    }
    //重叠的部分在other的前半部分，S的后半部分
    else if(s_right >= other_left && s_right <= other_right){
        //切割other
        other.data = other.data.substr(overlap_right_index - other_left + 1,other_right - overlap_right_index);
        s.data = s.data + other.data;
    }
    //如果other是S的一部分，完全重叠，那么S不需要改变，other直接从缓冲区中移除
    return s;
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t unassembled_bytes = 0;
    for(auto it = _segs_to_be_reassembled.begin();it != _segs_to_be_reassembled.end();it++){
        unassembled_bytes += it->data.length();
    }
    return unassembled_bytes;
}

bool StreamReassembler::empty() const { 
    return unassembled_bytes() == 0;
}
