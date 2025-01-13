// Akshay Narayan
// 11 January 2016

#include "dctcpQueue.h"
#include "dctcpPacket.h"

#include "../run/params.h"
#include <iostream>

extern double get_current_time();
extern void add_to_event_queue(Event *ev);
extern DCExpParams params;
extern unordered_map<uint32_t, string> queue_type_map;

DctcpQueue::DctcpQueue(uint32_t id, double rate, uint32_t limit_bytes, int location) : Queue(id, rate, limit_bytes, location) {
    std::cout << "DCTCP Queue. Location: " << location << std::endl;
}

/**
 * ECN marking. Otherwise just a droptail queue.
 * K_min > (C (pkts/s) * RTT (s)) / 7
 * at 10 Gbps recommend K = 65 packets, at 1 Gbps K = 20
 * if queue length < params.dctcp_mark_thresh, don't mark (ECN = 0).
 * if queue length > params.dctcp_mark_thresh, mark (ECN = 1).
 */
void DctcpQueue::enque(Packet *packet) {
    // cout << "🦉 DCTCP queueing" << endl;
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (bytes_in_queue + packet->size <= limit_bytes) {
        packets.push_back(packet);
        cout << "🍔 Packet enque in DCTCP queue at " << queue_type_map[this->location] << " @ " << get_current_time() << endl; 
        // cout << "🦉 src type: " << this->src->type << " DCTCP queueing packet size: " << packet->size << endl;
        // if(packet->type == HeirScheduleSCHD){
        //     SCHD* schd = ((HeirScheduleSCHDPkt*)packet)->schd;
        //     cout << "🥭 schd's address: " << schd << endl;
        //     cout << "🍇 Enqueue queue->src->type: " << this->src->type << ", queue->src->id: " << this->src->id << ", queue->dst->type: " << this->dst->type << ", queue->dst->id: " << this->dst->id << " send schd: slot: " << schd->slot << ", src_host_id: " << schd->src_host_id << ", dst_host_id: " << schd->dst_host_id << ", src_tor_id: " << schd->src_tor_id << ", dst_tor_id: " << schd->dst_tor_id << ", src_agg_id: " << schd->src_agg_id << ", dst_agg_id: " << schd->dst_agg_id << ", core_id: " << schd->core_id << endl;
        // }
        //  packets.front()
        bytes_in_queue += packet->size;

        // if (packets.size() >= params.dctcp_mark_thresh) {
        //     ((DctcpPacket*) packet)->ecn = true;
        // }
    } 
    else {
        pkt_drop++;
        drop(packet);
    }
}

// Packet* DctcpQueue::deque(){

// }

