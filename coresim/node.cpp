#include "packet.h"
#include "flow.h"

#include "../ext/factory.h"
#include "../run/params.h"
#include "topology.h"
#include <cassert>
#include <random>
#include <bitset>
#include <algorithm>
#include <chrono>

#define INTRA_POD 0
#define COME_IN 1
#define GO_OUT 2

using namespace std;
using namespace std::chrono; // 开始计时点 

extern DCExpParams params;
extern Topology *topology;
extern double get_current_time();

extern vector<vector<uint32_t>> failed_ratio;

struct Demand{
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t size;
    uint32_t Slot;
    uint32_t slot_end;
    uint32_t agg_id;
    Demand(uint32_t src_id, uint32_t dst_id, uint32_t size, uint32_t Slot, uint32_t slot_end){
        this->src_id = src_id;
        this->dst_id = dst_id;
        this->size = size;
        this->Slot = Slot;
        this->slot_end = slot_end;
    }
    Demand(uint32_t src_id, uint32_t dst_id, uint32_t size, uint32_t Slot, uint32_t slot_end, uint32_t agg_id){
        this->src_id = src_id;
        this->dst_id = dst_id;
        this->size = size;
        this->Slot = Slot;
        this->slot_end = slot_end;
        this->agg_id = agg_id;
    }

};

class Traffic{
public:
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t slot; // the slot to send at src
    uint32_t size;
    uint32_t type; // 0: inside pod, 1: come in, 2: go out
    uint32_t src_agg_id;
    uint32_t dst_agg_id;
    uint32_t last_allocated_slot = 0;
    bool aborted = false;
    Flow* flow;
    Traffic(uint32_t src_id, uint32_t dst_id, uint32_t slot, uint32_t size, uint32_t type){
        this->src_id = src_id;
        this->dst_id = dst_id;
        this->slot = slot;
        this->size = size;
        this->type = type;
    }
};

bool FlowComparator::operator() (Flow *a, Flow *b) {
    return a->flow_priority > b->flow_priority;
    //  if(a->flow_priority > b->flow_priority)
    //    return true;
    //  else if(a->flow_priority == b->flow_priority)
    //    return a->id > b->id;
    //  else
    //    return false;
}

struct Agg{
    // int k = params.num_of_ports;
    vector<bool> uplink;
    vector<bool> downlink;
    vector<vector<uint32_t>> traffic;
};

Node::Node(uint32_t id, uint32_t type) {
    this->id = id;
    this->type = type;
    // 随机初始化时间，服从正态分布，均值为0，方差为1e-6
    std::random_device rd;  // Get a random seed from the hardware
    std::default_random_engine generator(rd());  // Seed the generator
    std::normal_distribution<double> distribution(0, 1e-6);
    this->local_time_bias = distribution(generator);
    if(type == HeirSchedule_HOST){
        cout << "🤖 Host " << id << " local_time_bias: " << this->local_time_bias << endl;
    }
}


Host::Host(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type) : Node(id, host_type) {
    // queue = Factory::get_queue(id, rate, params.queue_size, queue_type, 0, 0);
    // this->host_type = host_type;
    this->type = host_type;
    this->received_bytes_all = 0;
    this->received_first_packet_time = -1;
    this->received_last_packet_time = -1;
}

HeirScheduleHost::HeirScheduleHost(uint32_t id, double rate_data, double rate_control, uint32_t queue_type) : Host(id, 0, queue_type, HeirSchedule_HOST) {
    this->type = HeirSchedule_HOST;
    toToRQueue = Factory::get_queue(0, rate_data, params.queue_size, queue_type, 0, HOST_TO_TOR);
    toLAQueue = Factory::get_queue(0, rate_control, params.queue_size_ctrl, DCTCP_QUEUE, 0, HOST_TO_LCS);
    // this->host_type = HeirSchedule_HOST;
    this->received_bytes_all = 0;
    this->received_first_packet_time = -1;
    this->received_last_packet_time = -1;

    // 初始化优先级队列
    uint32_t port_num = params.k;
    uint32_t server_num = port_num * port_num * port_num / 8;

    this->per_dst_queues.resize(3); // 一共分为三个优先级
    for(int i = 0; i < 3; i++){
        this->per_dst_queues[i].resize(server_num);
        for (int j = 0; j < int(server_num); j++){
            this->per_dst_queues[i][j].clear();
        }
    }

    for(uint32_t i = 0; i < server_num; i++){
        this->per_dst_priority_queues[i] = vector<std::queue<Flow*>>(8);
    }
}

void HeirScheduleHost::receive(Packet *packet) {
    // TODO: implement
    // cout << "🌕 HeirScheduleHost " << this->id << " receive a packet with type " << packet->type << " and id: " << packet->unique_id <<  " @ " << get_current_time() << endl;
    if (packet->type == SYNC_MSG){
        receive_sync_message(packet);
    }
    else if (packet->type == DELAY_RES_MSG){
        receive_delay_response_message(packet);
    }
    else if (packet->type == HeirScheduleSCHD){
        receive_schd_and_send_data(packet);
    }
    else if (packet->type == HeirScheduleData){
        receive_data_packet(packet);
    }
}

void HeirScheduleHost::receive_sync_message(Packet *packet){
    SyncMessage *sync_packet = (SyncMessage *)packet;
    cout << "🤖 HeirScheduleHost " << this->id << " receive sync message from LocalArbiter @ " << get_current_time() << endl;
    double T2_time = get_current_time() + local_time_bias - sync_packet->innetwork_delay;
    master_slave_diff = T2_time - sync_packet->T1_time;
    // cout << "🤖 HeirScheduleHost " << this->id << " master_slave_diff: " << master_slave_diff << endl;
    delete sync_packet;
    sync_packet = nullptr;
    DelayRequestMessage *delay_request_packet = new DelayRequestMessage(this, packet->src);
    T3_time = get_current_time() + local_time_bias;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_request_packet, toLAQueue));
}

void HeirScheduleHost::receive_delay_response_message(Packet *packet){
    DelayResponseMessage *delay_response_packet = (DelayResponseMessage *)packet;
    cout << "🤖 HeirScheduleHost " << this->id << " receive delay response message from LocalArbiter @ " << get_current_time() << endl;
    double T4_time = delay_response_packet->T4_time;
    slave_master_diff = T4_time - T3_time;
    // cout << "🤖 HeirScheduleHost " << this->id << " slave_master_diff: " << slave_master_diff << endl;
    delete delay_response_packet;
    delay_response_packet = nullptr;
    double one_way_delay = (master_slave_diff + slave_master_diff) / 2;
    double offset = master_slave_diff - one_way_delay;
    // cout << "🤖 HeirScheduleHost " << this->id << " one_way_delay: " << one_way_delay << " offset: " << offset << endl;
    local_time_bias -= offset;
    cout << "🤖 HeirScheduleHost " << this->id << " local_time_bias: " << local_time_bias << endl;

}

void HeirScheduleHost::host_send_rts(Flow* flow){
    // cout << "🤖 HeirScheduleHost " << this->id << " send RTS @ " << get_current_time() << endl;
    vector<rts> rts_vector;
    // for (auto it = this->sending_flows.begin(); it != this->sending_flows.end(); it++){
    //     Flow *f = *it;
    //     struct rts r;
    //     r.src_id = f->src->id;
    //     r.dst_id = f->dst->id;
    //     r.size = f->size;
    //     rts_vector.push_back(r);
    // }
    struct rts r;
    r.src_id = flow->src->id;
    r.dst_id = flow->dst->id;
    r.size = flow->size;
    rts_vector.push_back(r);


    HeirScheduleRTSPkt *rts_packet = new HeirScheduleRTSPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[this->id / (params.k * params.k / 4)], rts_vector);
    rts_packet->flow_to_request = flow;
    // 发送RTS
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), rts_packet, toLAQueue));
}

void HeirScheduleHost::receive_schd_and_send_data(Packet* packet){
    HeirScheduleSCHDPkt *schd_packet = (HeirScheduleSCHDPkt *)packet;
    // cout << "🍄 HeirScheduleHost " << this->id << " receive schd message from LocalArbiter @ " << get_current_time() << endl;
    
    // 根据schd发送数据包
    // 取出时间槽
    SCHD* schd = schd_packet->schd;
    // cout << "🥭schd address: " << schd << endl;
    // 输出schd信息
    // cout << "✅ HeirScheduleHost " << this->id << " receive schd: slot: " << schd->Slot << ", src_host_id: " << schd->src_host_id << ", dst_host_id: " << schd->dst_host_id << ", src_tor_id: " << schd->src_tor_id << ", dst_tor_id: " << schd->dst_tor_id << ", src_agg_id: " << schd->src_agg_id << ", dst_agg_id: " << schd->dst_agg_id << ", core_id: " << schd->core_id << endl;
    cout << "✅ HeirScheduleHost " << this->id << " receive schd: slot: " << schd->Slot << ", current slot: " << static_cast<uint32_t>(round(get_current_time() / params.slot_length_in_s)) << endl;
    uint32_t slot = schd->Slot;
    uint32_t slot_end = schd->slot_end;
    assert(slot_end >= slot);
    double time_to_send = double(slot) * params.slot_length_in_s;
    // cout << "🤖 HeirScheduleHost " << this->id << " slot: " << slot << ", time_to_send: " << time_to_send << ", current time: " << get_current_time() << endl;
    if(time_to_send < get_current_time()){
        cout << "slot: " << slot << endl;
        cout << "❌ HeirScheduleHost " << this->id << " time_to_send: " << time_to_send << ", current time: " << get_current_time() << endl;
        assert(false);
    }
    // assert(time_to_send >= get_current_time());
    
    // 发送数据包
    for(int i = 0; i < params.slot_length * (slot_end - slot + 1); i++){
        // cout << "🍎" << endl;
        // HeirScheduleDataPkt *data_packet = get_data_packet(schd->dst_host_id);
        HeirScheduleDataPkt *data_packet = get_data_packet_by_priority(schd->dst_host_id);

        data_packet->path = schd;
        data_packet->sending_time = time_to_send + double(i) * toToRQueue->get_transmission_delay(params.mss + params.hdr_size);
        add_to_event_queue(new PacketQueuingEvent(data_packet->sending_time, data_packet, toToRQueue));
        // cout << "🍋‍🟩 Host " << this->id << " send data packet " << data_packet->unique_id << " with size: " << data_packet->size << " to " << schd->dst_host_id << " @ " << data_packet->sending_time << endl;
        // cout << "💜" << endl;
    }
    // add_to_event_queue(RestoreLinkEvent(get_current_time(), schd));
}

HeirScheduleDataPkt *HeirScheduleHost::get_data_packet_by_priority(uint32_t dst){
    uint32_t cum_payload_size = 0;
    vector<Flow*> flow_list;
    vector<uint32_t> flow_segment_sizes;
    vector<uint32_t> flow_segment_begin_seq_no;
    
    // 取包    
    bool has_data = false;
    for(uint32_t pri = 0; pri < 8; pri++){
        while(1){
            if(per_dst_priority_queues[dst][pri].size() == 0){
                break;
            }
            Flow* f = per_dst_priority_queues[dst][pri].front();
            // cout << "🍒 Flow " << f->id << " sending! @ slot " << static_cast<uint32_t>(round(get_current_time() / params.slot_length_in_s)) << endl;
            // cout << "🍒 f->remaining_size_to_send: " << f->remaining_size_to_send << ", params.mss - cum_payload_size: " << params.mss - cum_payload_size << endl;
            if(f->remaining_size_to_send > params.mss - cum_payload_size){
                uint32_t send_data_size_now = params.mss - cum_payload_size;
                flow_list.push_back(f);
                flow_segment_sizes.push_back(send_data_size_now);
                flow_segment_begin_seq_no.push_back(f->next_seq_no);
                f->next_seq_no += send_data_size_now;
                f->remaining_size_to_send -= send_data_size_now;
                cum_payload_size = params.mss;
                has_data = true;
                break;
            }
            else{
                // cout << "🧲 get all bytes in this flow" << endl;
                uint32_t send_data_size_now = f->remaining_size_to_send;
                flow_list.push_back(f);
                flow_segment_sizes.push_back(send_data_size_now);
                flow_segment_begin_seq_no.push_back(f->next_seq_no);
                f->next_seq_no += send_data_size_now;
                f->remaining_size_to_send = 0;
                cum_payload_size += send_data_size_now;
                has_data = true;
                per_dst_priority_queues[dst][pri].pop();
            }
        }
    }
    // cout << "🚀 Get a Packet" << endl;
    HeirScheduleDataPkt *data_packet = new HeirScheduleDataPkt(this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[dst], params.mss, params.hdr_size, flow_list, flow_segment_sizes, flow_segment_begin_seq_no);
    return data_packet;
}

HeirScheduleDataPkt *HeirScheduleHost::get_data_packet(uint32_t dst){
    // cout << "🍒 HeirScheduleHost " << this->id << " start getting data packet to " << dst << endl;
    uint32_t cum_payload_size = 0;
    vector<Flow*> flow_list;
    vector<uint32_t> flow_segment_sizes;
    vector<uint32_t> flow_segment_begin_seq_no;
    // bool has_data = false;
    
    // for(auto it = this->per_dst_queues[0][dst].begin(); it != this->per_dst_queues[0][dst].end(); it++){
    while(cum_payload_size < params.mss && (per_dst_queues[0][dst].size() > 0 || per_dst_queues[1][dst].size() > 0 || per_dst_queues[2][dst].size() > 0)){
        flow_data_at_src flow_data;
        bool got_flow_flag = false;     
        for(int i = 0; i < per_dst_queues.size(); i++){ // 遍历优先级
            if(per_dst_queues[i][dst].size() > 0){
                sort(per_dst_queues[i][dst].begin(), per_dst_queues[i][dst].end(), [](flow_data_at_src a, flow_data_at_src b){
                    return a.flow->size < b.flow->size;
                });
                // cout << "per_dst_queues[i][dst].size(): " << per_dst_queues[i][dst].size() << endl;
                flow_data = per_dst_queues[i][dst].front();
                got_flow_flag = true;
                per_dst_queues[i][dst].pop_front();
                break;
            }
        }

        // if(flow_data.flow->id == 800){
        //     cout << "🍒 Flow 800 sending! @ slot " << static_cast<uint32_t>(round(get_current_time() / params.slot_length_in_s)) << endl;
        // }

        assert(got_flow_flag == true);

        // has_data = true;
        // flow_data_at_src flow_data = per_dst_queues[0][dst].front();
        // per_dst_queues[0][dst].pop_front();
        // cout << "🐭 flow_data.remaining_size: " << flow_data.remaining_size << ", params.mss - cum_payload_size: " << params.mss - cum_payload_size << endl;
        if(flow_data.remaining_size > params.mss - cum_payload_size){
            uint32_t send_data_size_now = params.mss - cum_payload_size;
            flow_list.push_back(flow_data.flow);
            flow_segment_sizes.push_back(send_data_size_now);
            flow_segment_begin_seq_no.push_back(flow_data.flow->next_seq_no);

            // 更新流信息
            flow_data.remaining_size -= send_data_size_now;
            cum_payload_size = params.mss;
            flow_data.flow->next_seq_no += send_data_size_now;
            
            // 将剩余数据重新放回队列
            per_dst_queues[flow_data.priority][dst].push_front(flow_data);
        }
        else{
            uint32_t send_data_size_now = flow_data.remaining_size;
            flow_list.push_back(flow_data.flow);
            flow_segment_sizes.push_back(send_data_size_now);
            flow_segment_begin_seq_no.push_back(flow_data.flow->next_seq_no);

            flow_data.flow->next_seq_no += send_data_size_now;
            flow_data.remaining_size = 0;
            cum_payload_size += send_data_size_now;

        }
    }
    HeirScheduleDataPkt *data_packet = new HeirScheduleDataPkt(this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[dst], params.mss, params.hdr_size, flow_list, flow_segment_sizes, flow_segment_begin_seq_no);
    // cout << "🍍 cum_payload_size: " << cum_payload_size << ", flow_list.size(): " << flow_list.size() << endl;

    // cout << "🍒 get packet " << data_packet->unique_id << endl;
    // if(has_data == false){
    //     return nullptr;
    // }
    return data_packet;
}

void HeirScheduleHost::receive_data_packet(Packet *packet){
    HeirScheduleDataPkt *data_packet = (HeirScheduleDataPkt *)packet;
    // cout << "🍎 HeirScheduleHost " << this->id << " receive data packet " << packet->unique_id << ", packet type: " << packet->type << ", src: " << packet->src->id << ", dst: " << packet->dst->id << " @ " << get_current_time() << endl;
    // data_packet->flows
    set<Flow*> now_receiving;
    // 更新收到的包信息
    for(int i = 0; i < int(data_packet->flows.size()); i++){
        // cout << "🍑 HeirScheduleHost " << this->id << " receive data packet " << packet->unique_id << ", flow id: " << data_packet->flows[i]->id << ", src: " << data_packet->flows[i]->src->id << ", dst: " << data_packet->flows[i]->dst->id << " @ " << get_current_time() << endl;
        Flow* f = data_packet->flows[i];

        if(f->dst->received_first_packet_time <= 0) f->dst->received_first_packet_time = get_current_time(); // 更新第一次收包时间

        // 统计小流包（流级别）的源端等待时延
        // double delay_in_100ns = (packet->release_time - f->start_time) * 1e7;
        double delay_in_100ns = (get_current_time() - f->start_time) * 1e7;

        int delays_in_ns = int((packet->release_time - f->start_time) * 1e9);
        // total_delay += delays_in_ns;
        // total_packets_count++;
        
        // bool write_src_flag = false;
        // if (write_src_flag == true) {
        //     double delay_in_ns = int((packet->release_time - f->start_time) * 1e9);
        //     packet_src_delay.push_back(delay_in_ns);
        // }
        int delay_in_100ns_int = int(delay_in_100ns);
        if(delay_in_100ns_int > 5999999){
            delay_in_100ns_int = 5999999;
        }


        // // 根据流大小放到合适的区间
        // packet_rough_delays[delay_in_100ns_int]++;
        // if(f->size < 1000){
        //     packet_rough_delays_0[delay_in_100ns_int]++;
        // }else if(f->size < 10000){
        //     packet_rough_delays_1[delay_in_100ns_int]++;
        // }else if (f->size < 100000){
        //     packet_rough_delays_2[delay_in_100ns_int]++;
        // }else if (f->size < 1000000){
        //     packet_rough_delays_3[delay_in_100ns_int]++;
        // }else if (f->size < 10000000){
        //     packet_rough_delays_4[delay_in_100ns_int]++;
        // }else if (f->size < 100000000){
        //     packet_rough_delays_5[delay_in_100ns_int]++;
        // }else if (f->size < 1000000000){
        //     packet_rough_delays_6[delay_in_100ns_int]++;
        // }else{
        //     packet_rough_delays_7[delay_in_100ns_int]++;
        // }


        // 更新流收包信息
        // data_packet->flows[i]->received_seqs[data_packet->flow_segment_begin_seq_no[i]] = data_packet->flow_segment_sizes[i];
        data_packet->flows[i]->received_payloads += data_packet->flow_segment_sizes[i];
        now_receiving.insert(data_packet->flows[i]);
    }
    delete data_packet;

    for(auto f : now_receiving){
        f->dst->now_receiving.insert(f);
        // vector<uint32_t> seq_nos;
        // for(auto it = f->received_seqs.begin(); it != f->received_seqs.end(); it++){
        //     seq_nos.push_back(it->first);
        // }
        // sort(seq_nos.begin(), seq_nos.end());

        // for(auto it = seq_nos.begin(); it != seq_nos.end(); it++){
        //     cout << *it << " ";
        // }
        // cout << endl;

        // 更新recv_till
        // uint32_t recv_till = 0;
        bool disorder_flag = false;
        // for(int i = 0; i < int(seq_nos.size()); i++){
        //     f->recv_max = seq_nos[i] + f->received_seqs[seq_nos[i]]; // 更新收到的最大包序列号
        //     if(recv_till == seq_nos[i]){
        //         recv_till += f->received_seqs[seq_nos[i]];
        //     }
        //     else{
        //         disorder_flag = true;
        //         f->unordered_cell = seq_nos.size() - i;
        //         break;
        //     }
        // }
        if(disorder_flag == false){
            f->unordered_cell = 0;
            f->max_out_of_order_buffer = 0;
        }
        // cout << "🫑" << endl;

        uint32_t unordered_bytes = 0;
        // for(int i = int(seq_nos.size() - f->unordered_cell); i < seq_nos.size(); i++){
        //     unordered_bytes += f->received_seqs[seq_nos[i]];
        // }
        f->max_out_of_order_buffer = max(f->max_out_of_order_buffer, unordered_bytes);

        if(f->first_byte_receive_time < 0){
            f->first_byte_receive_time = get_current_time();
        }

        // if (out_of_order_cell >= 0){
        //     cout << "flow " << f->id << " receiced seqs: " << endl;
        //     for(int i = 0; i < int(seq_nos.size()); i++){
        //         cout << seq_nos[i] << " " << f->received_seqs[seq_nos[i]] << " " << seq_nos[i] + f->received_seqs[seq_nos[i]] << endl;
        //     }
        // }
        // cout << "flow " << f->id << " out of order cell: " << f->unordered_cell << endl;
        // cout << "flow " << f->id << " receiced seqs: " << endl;
        // for(int i = 0; i < int(seq_nos.size()); i++){
        //     cout << seq_nos[i] << " " << f->received_seqs[seq_nos[i]] << " " << seq_nos[i] + f->received_seqs[seq_nos[i]] << endl;
        // }

        // f->recv_till = recv_till;
        // if(f->recv_till == f->size){
        if(f->received_payloads == f->size){
            assert(f->received_payloads == f->size);
            f->finished = true;
            f->finish_time = get_current_time();
            f->flow_completion_time = f->finish_time - f->start_time;

            f->unordered_cell = 0;
            f->max_out_of_order_buffer = 0;
            f->dst->received_bytes_all += f->size;
            f->dst->received_last_packet_time = get_current_time(); // 更新最后一次收包时间
            f->dst->now_receiving.erase(f);

            // cout << "✅ Flow " << f->id << " finished at " << get_current_time() << ", oracle fct is " << dynamic_cast<HeirScheduleTopology*>(topology)->get_oracle_fct(f) << "us, slowdown is " << 1e6*f->flow_completion_time / dynamic_cast<HeirScheduleTopology*>(topology)->get_oracle_fct(f) << endl;
        }
    }
}


//---------------------------------------------LocalArbiter---------------------------------------------


LocalArbiter::LocalArbiter(uint32_t id, double rate, uint32_t num_gcs, uint32_t queue_type) : Host(id, 0, queue_type, LOCAL_ARBITER) {
    this->type = LOCAL_ARBITER;
    this->num_gcs = num_gcs;
    for (uint32_t i = 0; i < params.k/2; i++) {
        toLCSQueues.push_back(Factory::get_queue(i, rate, params.queue_size_ctrl, DCTCP_QUEUE, 0, LA_TO_LCS));
    }
    for(uint32_t i = 0; i < num_gcs; i++){
        toGCSQueues.push_back(Factory::get_queue(i, rate, params.queue_size_ctrl, DCTCP_QUEUE, 0, LA_TO_GCS));
    }

    // 初始化路由相关矩阵
    for(uint32_t i = 0; i < params.T; i++){
        this->host_is_src.push_back(vector<bool>(params.k * params.k / 4, false));
        this->host_is_dst.push_back(vector<bool>(params.k * params.k / 4, false));
    }

    for(uint32_t i = 0; i < params.T; i++){
        vector<vector<bool>> ToR2Agg_t;
        for(uint32_t j = 0; j < params.k/2; j++){
            ToR2Agg_t.push_back(vector<bool>(params.k/2, false));
        }
        this->ToR2Agg.push_back(ToR2Agg_t);
    }
    for(uint32_t i = 0; i < params.T; i++){
        vector<vector<bool>> Agg2ToR_t;
        for(uint32_t j = 0; j < params.k/2; j++){
            Agg2ToR_t.push_back(vector<bool>(params.k/2, false));
        }
        this->Agg2ToR.push_back(Agg2ToR_t);
    }

    for(uint32_t i = 0; i < params.k * params.k / 4; i++){
        this->host_is_src_last_slot.push_back(0);
        this->host_is_dst_last_slot.push_back(0);
    }

    for(uint32_t i = 0; i < params.k/2; i++){
        vector<uint32_t> tmp1;
        vector<uint32_t> tmp2;
        for(uint32_t j = 0; j < params.k/2; j++){
            tmp1.push_back(0);
            tmp2.push_back(0);
        }
        this->ToR2Agg_last_slot.push_back(tmp1);
        this->Agg2ToR_last_slot.push_back(tmp2);
    }

    for(uint32_t i = 0; i < params.k / 2; i++){
        k_2.push_back(i);
    }
    cout << "🐱 LA: k = " << k << endl;
}

void LocalArbiter::receive(Packet *packet) {
    // TODO: implement
    // cout << "💻 LocalArbiter " << this->id << " receive packet" << endl;
    switch (packet->type)
    {
    case SYNC_MSG:
        receive_sync_message(packet);
        break;
    case DELAY_RES_MSG:
        receive_delay_response_message(packet);
        break;
    case DELAY_REQ_MSG:
        receive_delay_request_message_from_host(packet);
        break;
    case HeirScheduleRTS:
        receive_rts(packet);
        break;
    case HeirScheduleIPR:
        receive_ipr(packet);
        break;
    case HeirScheduleIPS:
        receive_ips(packet);
        break;
    case HeirScheduleIPD:
        receive_ipd(packet);
        break;
    case CORE_SCHD:
        receive_core_schd(packet);
        break;
    case CORE_DENY:
        receive_core_deny(packet);
        break;
    default:
        break;
    }
}

void LocalArbiter::receive_sync_message(Packet *packet){
    SyncMessage *sync_packet = (SyncMessage *)packet;
    cout << "💻 LocalArbiter " << this->id << " receive sync message from GlobalArbiter @ " << get_current_time() << endl;
    double T2_time = get_current_time() + local_time_bias - sync_packet->innetwork_delay;
    master_slave_diff = T2_time - sync_packet->T1_time;
    // cout << "💻 LocalArbiter " << this->id << " master_slave_diff: " << master_slave_diff << endl;
    delete sync_packet;
    sync_packet = nullptr;
    DelayRequestMessage *delay_request_packet = new DelayRequestMessage(this, packet->src);
    T3_time = get_current_time() + local_time_bias;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_request_packet, toGCSQueues[rand() % num_gcs])); // 简化处理：随机选择一个GCS发送
}

void LocalArbiter::receive_delay_response_message(Packet *packet){
    DelayResponseMessage *delay_response_packet = (DelayResponseMessage *)packet;
    cout << "💻 LocalArbiter " << this->id << " receive delay response message from GlobalArbiter @ " << get_current_time() << endl;
    double T4_time = delay_response_packet->T4_time;
    slave_master_diff = T4_time - T3_time;
    // cout << "💻 LocalArbiter " << this->id << " slave_master_diff: " << slave_master_diff << endl;
    delete delay_response_packet;
    delay_response_packet = nullptr;
    double one_way_delay = (master_slave_diff + slave_master_diff) / 2;
    double offset = master_slave_diff - one_way_delay;
    // cout << "💻 LocalArbiter " << this->id << " one_way_delay: " << one_way_delay << " offset: " << offset << endl;
    local_time_bias -= offset;
    cout << "💻 LocalArbiter " << this->id << " local_time_bias: " << local_time_bias << endl;

    // 启动第二级时间同步，LA作为master向Host发SyncMessage
    send_sync_message_to_host();
}

void LocalArbiter::send_sync_message_to_host(){
    for (uint32_t i = 0; i < hosts_per_pod; i++){
        uint32_t host_id = this->id * hosts_per_pod + i;
        SyncMessage *sync_packet = new SyncMessage(this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[host_id], get_current_time() + local_time_bias);
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), sync_packet, toLCSQueues[i / (params.k/2)]));
        cout << "💻 LocalArbiter " << this->id << " send sync message to Host " << i << endl;
    }
}

void LocalArbiter::receive_delay_request_message_from_host(Packet *packet){
    cout << "💻 LocalArbiter " << this->id << " receive delay request message from Host @ " << get_current_time() << endl;
    DelayRequestMessage *delay_request_packet = (DelayRequestMessage *)packet;
    double T4_time = get_current_time() + local_time_bias - delay_request_packet->innetwork_delay;
    DelayResponseMessage *delay_response_packet = new DelayResponseMessage(this, packet->src, T4_time);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_response_packet, toLCSQueues[(packet->src->id % hosts_per_pod) / (params.k/2)]));    
}

// void LocalArbiter::send_interpod_rts(double time){
//     uint32_t port_num = params.num_of_ports;
//     uint32_t servers_per_pod = port_num * port_num / 4;
//     for (auto it = this->received_rts.begin(); it != this->received_rts.end(); it++){
//         rts r = *it;
//         if (r.src_id / servers_per_pod == r.dst_id / servers_per_pod){
//             continue;
//         }
//         struct ipr ipr;
        
//         // add_to_event_queue(new PacketQueuingEvent(time, rts_packet, GCS_switch_queue));
//     }
// }

void LocalArbiter::receive_rts(Packet* packet){
    HeirScheduleRTSPkt *rts_packet = (HeirScheduleRTSPkt *)packet;
    // cout << "🐱 LocalArbiter " << this->id << " receive rts from Host @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    for (auto it = rts_packet->rts_vector.begin(); it != rts_packet->rts_vector.end(); it++){
        // 更新流量信息
        rts r = *it;
        uint32_t src_id = r.src_id;
        uint32_t dst_id = r.dst_id;
        uint32_t size = r.size;
        src_dst_data_size_table[{src_id, dst_id}] += size;
        // cout << "🐱 LocalArbiter " << this->id << " rts: src_id: " << src_id << " dst_id: " << dst_id << " size: " << size << ", src_dst_data_size_table[{src_id, dst_id}]: " << src_dst_data_size_table[{src_id, dst_id}] << endl;
    }
    flow_size_table[rts_packet->flow_to_request] = rts_packet->flow_to_request->size;
    delete rts_packet;
    // allocate_uplink();
}

void LocalArbiter::schedule(){
    unordered_map<src_dst_pair, vector<uint32_t>> traffic;
    vector<Traffic*> traffic_list;
    uint32_t current_slot = static_cast<uint32_t>(round(get_current_time() / params.slot_length_in_s));
    // 1. 收集流量信息（Collecting traffic infomation）
    // 本Pod发出的流量
    // for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
    for(auto it : flow_size_table){
        Flow* flow = it.first;
        uint32_t size = it.second;
        uint32_t src_id = flow->src->id;
        uint32_t dst_id = flow->dst->id;
        uint32_t LAS = last_allocated_slot[{src_id, dst_id}];
        if(src_id / hosts_per_pod == dst_id / hosts_per_pod){ // 在同一个pod内
            uint32_t slot = static_cast<uint32_t>(ceil((get_current_time() + params.arbiter_lag * 2) / params.slot_length_in_s));
            Traffic* traff = new Traffic(src_id, dst_id, slot, size, INTRA_POD);
            traff->last_allocated_slot = LAS;
            traffic_list.push_back(traff);
            traff->flow = flow;
            // if(size == 1688){
            //     cout << "⭕️ Flow with size 1688 @ slot " << current_slot << endl;
            // }
            // if(src_id == 11 && dst_id == 19){
            //     cout << "0️⃣ flow " << src_id << " -> " << dst_id << ", type: " << type << " slot: " << slot << ", current_slot: " << current_slot << endl;
            // }
        }
        else{ // 不在同一个pod内
            // Design choice: over schedule，不考虑正在schedule的那部分流量
            // Design choice: no over schedule，考虑正在schedule的那部分流量
            if(inschedule_slot_table[{src_id, dst_id}] >= size/(params.mss*params.slot_length) + 1){
                continue;
            }
            uint32_t slot = static_cast<uint32_t>(ceil((get_current_time() + params.arbiter_lag * 4) / params.slot_length_in_s));
            Traffic* traff = new Traffic(src_id, dst_id, slot, size, GO_OUT);
            traff->last_allocated_slot = LAS;
            traffic_list.push_back(traff);
            traff->flow = flow;
            // if(src_id == 1 && dst_id == 41){
            //     cout << "🍎 slot: " << slot << ", current_slot: " << current_slot << ", params.T: " << params.T << endl;
            // }
        }
    }
    // 本Pod接收的流量
    for(auto ipr_pkt = received_ipr_packets.begin(); ipr_pkt != received_ipr_packets.end(); ipr_pkt++){
        for (auto it = (*ipr_pkt)->ipr_info.begin(); it != (*ipr_pkt)->ipr_info.end(); it++){
            ipr* ipr = *it;
            uint32_t src_id = ipr->src_host_id;
            uint32_t dst_id = ipr->dst_host_id;
            uint32_t src_agg_id = ipr->src_agg_id;
            uint32_t size = ipr->size;
            uint32_t slot = ipr->slot;
            Flow* flow = ipr->flow;
            uint32_t LAS = last_allocated_slot[{src_id, dst_id}];
            Traffic* traff = new Traffic(src_id, dst_id, slot, size, COME_IN);
            traff->last_allocated_slot = LAS;
            traff->flow = flow;
            traffic_list.push_back(traff);
            traff->src_agg_id = src_agg_id;
            // if(src_id == 11 && dst_id == 121){
            //     cout << "🍆 flow " << src_id << " -> " << dst_id << " slot: " << slot << ", src_agg_id: " << src_agg_id << ", current_slot: " << current_slot << endl;
            // }
        }
    }

    // 2. 分配时间槽（Allocate time slots）
    // 2.1 排序
    if(params.policy == "SRF"){
        // 按照remaining size从小到大排序
        sort(traffic_list.begin(), traffic_list.end(), [](Traffic* a, Traffic* b){
            return a->size < b->size;
        });
    }
    else if(params.policy == "LRU"){
        //按照last_allocated_slot从小到大排序
        sort(traffic_list.begin(), traffic_list.end(), [](Traffic* a, Traffic* b){
            return a->last_allocated_slot < b->last_allocated_slot;
        });
    }
    else if(params.policy == "RND"){
        random_shuffle(traffic_list.begin(), traffic_list.end());
    }
    else if(params.policy == "FCFS"){
        // 按照flow的start_time从小到大排序
        sort(traffic_list.begin(), traffic_list.end(), [](Traffic* a, Traffic* b){
            return a->flow->start_time < b->flow->start_time;
        });
    }
    else{
        cout << "❌ policy error" << endl;
        assert(false);
    }
    

    // 2.2 分配时间槽
    // design choice: 单次调度中，一个host只能分配一次，为了防止：1.小流被大流积压；2.超出时间槽T（可以设想100条大流和1条小流前往同一个dst，且小流一个slot发不完，这样子的话，尽管有小流优先，但小流还是会被挤压到很后面）
    vector<bool> host_is_src_this_schedule(params.k * params.k / 4, false); 
    vector<bool> host_is_dst_this_schedule(params.k * params.k / 4, false);
    for(auto it = traffic_list.begin(); it != traffic_list.end(); it++){
        Traffic* traff = *it;
        uint32_t src_id = traff->src_id;
        uint32_t dst_id = traff->dst_id;
        uint32_t slot = traff->slot;
        uint32_t type = traff->type;
        // if(src_id == 11 && dst_id == 19){
        //     cout << "1️⃣ flow " << src_id << " -> " << dst_id << ", type: " << type << " slot: " << slot << ", current_slot: " << current_slot << endl;
        // }
        if(type == INTRA_POD){
            // if(host_is_src_this_schedule[src_id % hosts_per_pod] == true || host_is_dst_this_schedule[dst_id % hosts_per_pod] == true){
            //     traff->aborted = true; // 本次调度失败
            //     // if(src_id == 11 && dst_id == 19){
            //     //     cout << "2️⃣ flow " << src_id << " -> " << dst_id << ", type: " << type << " slot: " << slot << ", current_slot: " << current_slot << endl;
            //     // }
            //     // if(traff->size == 1688){
            //     //     cout << "❕ failed due to host is allocated this time " << host_is_src_this_schedule[src_id % hosts_per_pod] << " " << host_is_dst_this_schedule[dst_id % hosts_per_pod] << "@ slot " << current_slot << endl;
            //     // }
            //     continue;
            // }
            while(1){
                if(host_is_src[slot % params.T][src_id % hosts_per_pod] == false && host_is_dst[slot % params.T][dst_id % hosts_per_pod] == false){
                    host_is_src[slot % params.T][src_id % hosts_per_pod] = true;
                    host_is_dst[slot % params.T][dst_id % hosts_per_pod] = true;
                    host_is_src_this_schedule[src_id % hosts_per_pod] = true;
                    host_is_dst_this_schedule[dst_id % hosts_per_pod] = true;
                    traff->slot = slot;
                    // if(traff->flow->id == 800){
                    //     cout << "😻 Flow 800 get slot " << slot <<  " @ slot " << current_slot << endl; 
                    // }
                    // if(src_id == 11 && dst_id == 19){
                    //     cout << "🫐 uplink flow " << src_id << " -> " << dst_id << " slot: " << slot << ", current_slot: " << current_slot << endl;
                    // }
                    break;
                }
                else{
                    if(traff->size <= params.Threshold){
                        slot++;
                    }
                    else{
                        traff->aborted = true; // 本次调度失败
                        break;
                    }
                    // slot++;
                }
                if(slot - current_slot >= params.T){
                    traff->aborted = true; // 本次调度失败
                    // cout << "❌ slot: " << slot << ", current_slot: " << current_slot << ", traff->slot: " << traff->slot << ", params.T: " << params.T << endl;
                    // cout << "❌ src_id: " << src_id << ", dst_id: " << dst_id << endl;
                    // assert(false);
                    break;
                }
            }
        }
        else if(type == GO_OUT){
            // if(host_is_src_this_schedule[src_id % hosts_per_pod] == true){
            //     traff->aborted = true; // 本次调度失败
            //     continue;
            // }
            while(1){
                if(host_is_src[slot % params.T][src_id % hosts_per_pod] == false){
                    host_is_src[slot % params.T][src_id % hosts_per_pod] = true;
                    host_is_src_this_schedule[src_id % hosts_per_pod] = true;
                    traff->slot = slot;
                    // if(src_id == 11 && dst_id == 121){
                    //     cout << "🥕 uplink flow " << src_id << " -> " << dst_id << " slot: " << slot << ", current_slot: " << current_slot << endl;
                    // }
                    break;
                }
                else{
                    if(traff->size <= params.Threshold){
                        slot++;
                    }
                    else{
                        traff->aborted = true; // 本次调度失败
                        break;
                    }
                    // slot++;
                }
                if(slot - current_slot >= params.T){
                    traff->aborted = true; // 本次调度失败
                    // cout << "❌ slot: " << slot << ", current_slot: " << current_slot << ", traff->slot: " << traff->slot << ", params.T: " << params.T << endl;
                    // cout << "❌ src_id: " << src_id << ", dst_id: " << dst_id << endl;
                    // assert(false);
                    break;
                }
                
                assert(slot - current_slot < params.T);
            }
        }
        else if(type == COME_IN){
            // if(host_is_dst_this_schedule[dst_id % hosts_per_pod] == true){
            //     traff->aborted = true; // 本次调度失败
            //     continue;
            // }

            uint32_t slot_down = slot + static_cast<uint32_t>(round(params.propagation_delay_data * 2 / params.slot_length_in_s)); // 跨Pod流量，会晚到两个propagation delay
            if(host_is_dst[slot_down % params.T][dst_id % hosts_per_pod] == false){
                host_is_dst[slot_down % params.T][dst_id % hosts_per_pod] = true;
                host_is_dst_this_schedule[dst_id % hosts_per_pod] = true;
                // traff->slot = slot;
            }
            else{
                traff->aborted = true; // 本次调度失败
            }
            if(slot - current_slot >= params.T){
                traff->aborted = true; // 本次调度失败
                cout << static_cast<uint32_t>(round(params.propagation_delay_data * 2 / params.slot_length_in_s)) << endl;
                cout << "❌ slot: " << slot << ", current_slot: " << current_slot << ", traff->slot: " << traff->slot << ", params.T: " << params.T << endl;
                cout << "❌ src_id: " << src_id << ", dst_id: " << dst_id << endl;
                // assert(false);
                continue;
            }
        }
        if(traff->aborted == false){
            last_allocated_slot[{src_id, dst_id}] = traff->slot;
        }
    }

    uint32_t failed_cnt = 0;
    uint32_t success_cnt = 0;
    // 3. 路径选择（Path selection）(选择Agg)
    for(auto it : traffic_list){
        Traffic* traff = it;
        if(traff->aborted == true){
            continue;
        }
        uint32_t src_id = traff->src_id;
        uint32_t dst_id = traff->dst_id;
        uint32_t slot = traff->slot;
        uint32_t type = traff->type;
        random_shuffle(k_2.begin(), k_2.end());
        bool flag = false;
        if(type == INTRA_POD){
            uint32_t src_tor_id = src_id % hosts_per_pod / (k/2);
            uint32_t dst_tor_id = dst_id % hosts_per_pod / (k/2);
            for(auto agg_id : k_2){
                if(ToR2Agg[slot % params.T][src_tor_id][agg_id] == false && Agg2ToR[slot % params.T][agg_id][dst_tor_id] == false){
                    ToR2Agg[slot % params.T][src_tor_id][agg_id] = true;
                    Agg2ToR[slot % params.T][agg_id][dst_tor_id] = true;
                    traff->src_agg_id = agg_id;
                    traff->dst_agg_id = agg_id;
                    flag = true;
                    // if(src_id == 11 && dst_id == 19){
                    //     cout << "3️⃣ flow " << src_id << " -> " << dst_id << ", type: " << type << " slot: " << slot << ", current_slot: " << current_slot << endl;
                    // }
                    // if(src_id == 89 && dst_id == 83){
                    //     cout << "🍇 slot: " << slot << ", src_agg_id: " << traff->src_agg_id << " for flow " << src_id << " -> " << dst_id << " @ slot " << static_cast<uint32_t>(get_current_time()/params.slot_length_in_s) << endl;
                    // }
                    success_cnt++;
                    break;
                }
            }
            if(flag == false){
                // 撤回已分配的slot（这里也可以不用撤回，因为这已经是最近的slot了，后续也用不到）
                host_is_src[slot % params.T][src_id % hosts_per_pod] = false;
                host_is_dst[slot % params.T][dst_id % hosts_per_pod] = false;
                traff->aborted = true;
                failed_cnt++;
                // if(traff->size == 1688){
                //     cout << "❗️ failed due to no available agg @ " << current_slot << endl;
                // }
            }
        }
        else if(type == GO_OUT){
            uint32_t src_tor_id = src_id % hosts_per_pod / (k/2);
            for(auto agg_id : k_2){
                if(ToR2Agg[slot % params.T][src_tor_id][agg_id] == false){
                    ToR2Agg[slot % params.T][src_tor_id][agg_id] = true;
                    traff->src_agg_id = agg_id;
                    flag = true;
                    // if(src_id == 87 && dst_id == 3){
                    //     cout << "🍇 LA " << this->id << " allocate uplink slot: " << slot << ", src_agg_id: " << traff->src_agg_id << " for flow " << src_id << " -> " << dst_id << " @ slot " << static_cast<uint32_t>(get_current_time()/params.slot_length_in_s) << endl;
                    // }
                    success_cnt++;
                    break;
                }
            }
            if(flag == false){
                // 撤回已分配的slot
                host_is_src[slot % params.T][src_id % hosts_per_pod] = false;
                traff->aborted = true;
                failed_cnt++;
            }
        }
        else if(type == COME_IN){
            uint32_t slot_down = slot + static_cast<uint32_t>(round(params.propagation_delay_data * 2 / params.slot_length_in_s)); 
            uint32_t dst_tor_id = dst_id % hosts_per_pod / (k/2);
            for(auto agg_id : k_2){
                if(Agg2ToR[slot_down % params.T][agg_id][dst_tor_id] == false){
                    Agg2ToR[slot_down % params.T][agg_id][dst_tor_id] = true;
                    traff->dst_agg_id = agg_id;
                    flag = true;
                    success_cnt++;
                    break;
                }
            }
            if(flag == false){
                // 撤回已分配的slot
                host_is_dst[slot_down % params.T][dst_id % hosts_per_pod] = false;
                traff->aborted = true;
                failed_cnt++;
            }
        }
    }
    if(this->id == 2){
        // cout << "💚 LA " << this->id << " success_cnt: " << success_cnt << ", failed_cnt: " << failed_cnt << endl;
        failed_ratio.push_back({static_cast<uint32_t>(traffic_list.size()), success_cnt, failed_cnt});
    }

    // 4. 根据调度结果发送控制包
    HeirScheduleCoreRequestPkt* core_request_packet = new HeirScheduleCoreRequestPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter);
    unordered_map<LocalArbiter*, HeirScheduleIPRPkt*> ipr_packets; // LA-id, IPR packets
    unordered_map<LocalArbiter*, HeirScheduleIPDPkt*> ipd_packets; // LA-id, IPD packets
    for(auto la : dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters){
        if(la->id == this->id){
            continue;
        }
        HeirScheduleIPRPkt *ipr_packet = new HeirScheduleIPRPkt(get_current_time(), this, la);
        ipr_packets[la] = ipr_packet;

        HeirScheduleIPDPkt *ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, la);
        ipd_packets[la] = ipd_packet;
    }
    for(auto it : traffic_list){
        Traffic* traff = it;
        uint32_t type = traff->type;
        uint32_t src_id = traff->src_id;
        uint32_t dst_id = traff->dst_id;
        uint32_t slot = traff->slot;
        if(traff->aborted == true){
            if(type == COME_IN){
                // 还原slot
                ipd* ipd_info = new ipd(slot, slot, src_id, traff->src_agg_id, dst_id);
                LocalArbiter* src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / hosts_per_pod];
                // HeirScheduleIPDPkt* ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, src_la, ipd_info);
                ipd_packets[src_la]->ipd_info.push_back(ipd_info);
                // send_deny_to_la(src_la, ipd_packet);
            }
            continue;
        }
        
        if(type == INTRA_POD){
            SCHD* schd_info = new SCHD(slot, slot, src_id, src_id/(k/2), traff->src_agg_id + this->id * (k/2), 10000, traff->dst_agg_id + this->id * (k/2), dst_id/(k/2), dst_id);
            HeirScheduleSCHDPkt* schd_packet = new HeirScheduleSCHDPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[src_id], schd_info);
            add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, schd_packet, toLCSQueues[(src_id % hosts_per_pod) / (params.k/2)]));
            
            // if(src_dst_data_size_table[{src_id, dst_id}] <= params.mss * params.slot_length){
            //     // cout << "💊 LA " << this->id << " erase src_dst_data_size_table! src: " << src_id << ", dst: " << dst_id << ", size: " << src_dst_data_size_table[{src_id, dst_id}] << ", params.mss * params.slot_length: " << params.mss * params.slot_length << endl;
            //     // for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
            //     //     cout << "🐱 src_dst_data_size_table: src: " << it->first.src << ", dst: " << it->first.dst << ", size: " << it->second << endl;
            //     // }
            //     src_dst_data_size_table.erase({src_id, dst_id});
            // }
            // else{
            //     src_dst_data_size_table[{src_id, dst_id}] -= params.mss * params.slot_length;
            // }
            Flow* flow = traff->flow;
            if(flow_size_table[flow] <= params.mss * params.slot_length){
                flow_size_table.erase(flow);
            }
            else{
                flow_size_table[flow] -= params.mss * params.slot_length;
            }
        }
        else if(type == GO_OUT){
            ipr* _ipr_info = new ipr(slot, slot, traff->size, src_id, traff->src_agg_id + this->id * (k/2), dst_id);
            _ipr_info->flow = traff->flow;
            ipr_packets[dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / hosts_per_pod]]->ipr_info.push_back(_ipr_info);
            // if(src_id == 87 && dst_id == 3){
            //     cout << "🍠 Local Arbiter allocate uplink slot " << slot << " for flow " << src_id << " -> " << dst_id << ", src_agg_id: " << _ipr_info->src_agg_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
            // }
            inschedule_slot_table[{src_id, dst_id}]++;
        }
        else if(type == COME_IN){
            // 还原slot
            // uint original_slot = traff->slot - static_cast<uint32_t>(round(params.propagation_delay_data * 2 / params.slot_length_in_s));
            core_rts* _core_rts_info = new core_rts(slot, slot, traff->size, src_id, traff->src_agg_id, dst_id, traff->dst_agg_id + this->id * (k/2));
            _core_rts_info->flow = traff->flow;
            core_request_packet->core_rts_vector.push_back(_core_rts_info);
            // if(src_id == 87 && dst_id == 3){
            //     cout << "🫐 Local Arbiter " << this->id << " allocate downlink slot " << slot << " for flow " << src_id << " -> " << dst_id << ", src_agg_id: " << traff->src_agg_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
            // }
        }
    }

    // 发送聚合的ipr包, ipd包
    for(auto it = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.begin(); it != dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.end(); it++){
        LocalArbiter *la = *it;
        if(la->id == this->id){
            continue;
        }
        HeirScheduleIPRPkt *ipr_packet = ipr_packets[la];
        if(ipr_packet->ipr_info.size() > 0){
            ipr_packet->size += ipr_packet->ipr_info.size() * ipr::info_size; // 每个ipr_info 16B
            send_request_to_la(la, ipr_packet);
            // cout << "🍓 LA " << this->id << " send ipr to LA " << la->id << " @ slot " << static_cast<uint32_t>(get_current_time()/params.slot_length_in_s) << ", " << get_current_time() << endl;
        }

        HeirScheduleIPDPkt *ipd_packet = ipd_packets[la];
        if(ipd_packet->ipd_info.size() > 0){
            ipd_packet->size += ipd_packet->ipd_info.size() * ipd::info_size; // 每个ipd_info 16B
            send_deny_to_la(la, ipd_packet);
            // cout << "🍓 LA " << this->id << " send ipd to LA " << la->id << " @ slot " << static_cast<uint32_t>(get_current_time()/params.slot_length_in_s) << ", " << get_current_time() << endl;
        }
    }
    // 向GA发送core_rts

    core_request_packet->size += core_request_packet->core_rts_vector.size() * core_rts::info_size;
    if(core_request_packet->core_rts_vector.size() > 0){
        send_request_to_ga(core_request_packet);
    }

    // 5. 清理
    for(auto it = traffic_list.begin(); it != traffic_list.end(); it++){
        delete *it;
    }
    traffic_list.clear();
    for(auto it = received_ipr_packets.begin(); it != received_ipr_packets.end(); it++){
        delete *it;
    }
    received_ipr_packets.clear();

    // 6. 复原本slot的状态，以便将来能够使用
    for(uint32_t i = 0; i < params.k * params.k / 4; i++){
        host_is_src[current_slot % params.T][i] = false;
        host_is_dst[current_slot % params.T][i] = false;
    }
    for(uint32_t i = 0; i < params.k/2; i++){
        for(uint32_t j = 0; j < params.k/2; j++){
            ToR2Agg[current_slot % params.T][i][j] = false;
            Agg2ToR[current_slot % params.T][i][j] = false;
        }
    }

    // 7. 下一次调度
    add_to_event_queue(new LocalArbiterScheduleEvent(get_current_time() + params.slot_length_in_s, this));
}

void LocalArbiter::allocate_uplink(){
    // if(this->id == 0){
    //     cout << "🍏 LocalArbiter " << this->id << " allocate uplink @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
    // }
    // for (auto it = rts_packet->rts_vector.begin(); it != rts_packet->rts_vector.end(); it++){
    unordered_map<LocalArbiter*, HeirScheduleIPRPkt*> ipr_packets; // LA-id, IPR packets
    HeirScheduleCoreRequestPkt* core_request_packet = new HeirScheduleCoreRequestPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter);
    for (auto it = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.begin(); it != dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.end(); it++){
        LocalArbiter* la = *it;
        if (la->id == this->id){
            continue;
        }
        HeirScheduleIPRPkt *ipr_packet = new HeirScheduleIPRPkt(get_current_time(), this, la);
        ipr_packets[la] = ipr_packet;
    }
    // 将src_dst_data_size_table按照size从小到大排序
    vector<pair<pair<uint32_t, uint32_t>, uint32_t>> src_dst_data_size_vector;
    for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
        src_dst_data_size_vector.push_back({{it->first.src, it->first.dst}, it->second});
    }
    sort(src_dst_data_size_vector.begin(), src_dst_data_size_vector.end(), [](pair<pair<uint32_t, uint32_t>, uint32_t> a, pair<pair<uint32_t, uint32_t>, uint32_t> b){
        return a.second < b.second;
    });

    // for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
    for(auto it = src_dst_data_size_vector.begin(); it != src_dst_data_size_vector.end(); it++){
        // auto start = high_resolution_clock::now();
        // cout << "🌶️ Allocate! @ " << get_current_time() << endl;
        // uint32_t src_id = it->first.src;
        // uint32_t dst_id = it->first.dst;
        // uint32_t src_tor = src_id / (params.k / 2);
        // uint32_t size = it->second;
        uint32_t src_id = it->first.first;
        uint32_t dst_id = it->first.second;
        uint32_t src_tor_id = src_id / (params.k / 2);
        uint32_t dst_tor_id = dst_id / (params.k / 2);
        uint32_t size = it->second;
        // if (routing_table[{src_id, dst_id}] == nullptr){
        //     routing_table[{src_id, dst_id}] = new SCHD();
        // }
        // else{ // 如果已经在路由表中，说明已经分配过时间槽，不再分配
        //     // cout << "🌶️ already in routing_table! src: " << src_id << ", dst: " << dst_id << endl;
        //     continue;
        // }

        if(src_id / hosts_per_pod == dst_id / hosts_per_pod){ // 在同一个pod内
            // cout << "🍌 LA start allocating" << endl;
            // if (src_id == 1 && dst_id == 5){
            //     cout << "🍒 trying to allocate slot " << Slot << " to " << slot_end << " for flow " << src_id << " -> " << dst_id << " @ " << get_current_time() << endl;
            // }
            uint32_t Slot = static_cast<uint32_t>(ceil((get_current_time() + params.arbiter_lag * 2) / params.slot_length_in_s));
            uint32_t slots_needed = static_cast<uint32_t>(ceil(double(size) / (params.mss * params.slot_length)));
            uint32_t slot_end = Slot + min(params.max_slot_to_allocate, slots_needed - inschedule_slot_table[{src_id, dst_id}]) - 1;
            uint32_t slot_down = Slot - static_cast<uint32_t>(ceil(params.slot_length_in_s / params.propagation_delay_data));
            uint32_t slot_down_end = slot_down + (slot_end - Slot) + 1; // 多分配一个slot
            // if(Slot < 700000 || Slot > 1500000){
            //     cout << "❌ LA " << this->id << " Slot: " << Slot << " @ " << get_current_time() << endl;
            //     assert(false);
            // }
            if(this->host_is_src_last_slot[src_id % hosts_per_pod] >= Slot || this->host_is_dst_last_slot[dst_id % hosts_per_pod] >= slot_down){
                continue;
            }
            if(inschedule_slot_table[{src_id, dst_id}] >= slots_needed){
                continue;
            }

            bool flag = false;
            uint32_t agg_id = 0;
            uint32_t dst_tor = dst_id / (params.k / 2);
            random_shuffle(k_2.begin(), k_2.end());
            for(int i = 0; i < params.k / 2; i++){
                agg_id = k_2[i] + params.k / 2 * this->id; // 需要加上bias
                if(ToR2Agg_last_slot[src_tor_id % (params.k / 2)][agg_id % (params.k / 2)] < Slot &&  Agg2ToR_last_slot[agg_id % (params.k / 2)][dst_tor % (params.k / 2)] < slot_down){
                    host_is_src_last_slot[src_id % hosts_per_pod] = slot_end;
                    host_is_dst_last_slot[dst_id % hosts_per_pod] = slot_down_end;
                    ToR2Agg_last_slot[src_tor_id % (params.k / 2)][agg_id % (params.k / 2)] = slot_end;
                    Agg2ToR_last_slot[agg_id % (params.k / 2)][dst_tor % (params.k / 2)] = slot_down_end;
                    
                    flag = true;
                    // cout << "🐣 LA " << this->id << " allocate a slot " << Slot << " for flow " << src_id << " -> " << dst_id << " @ " << get_current_time() << endl;
                    break;
                }
            }

            if(flag){
                // core_request_packet->core_rts_vector.push_back(new core_rts(Slot, slot_end, src_id, agg_id, dst_id, agg_id));
                //组装完整路径
                // cout << "🐥 LA " << this->id << " generate full path for flow " << src_id << " -> " << dst_id << ", from slot " << Slot << " to slot " << slot_end << ", src_tor: " << src_tor_id << ", src_agg: " << src_agg_id << ", core: " << core_id << ", dst_agg: " << dst_agg_id << ", dst_tor: " << dst_tor_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
                SCHD* schd = new SCHD(Slot, slot_end, src_id, src_tor_id, agg_id, 10000, agg_id, dst_tor_id, dst_id); // core id > k*k/4, 意思是不存在，不上core
                HeirScheduleSCHDPkt *schd_packet = new HeirScheduleSCHDPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[src_id], schd);
                add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, schd_packet, toLCSQueues[(src_id % hosts_per_pod) / (params.k/2)]));
                
                if(src_dst_data_size_table[{src_id, dst_id}] <= params.mss * params.slot_length * (slot_end - Slot + 1)){
                    // cout << "💊 LA " << this->id << " erase src_dst_data_size_table! src: " << src_id << ", dst: " << dst_id << ", size: " << src_dst_data_size_table[{src_id, dst_id}] << ", params.mss * params.slot_length: " << params.mss * params.slot_length << endl;
                    // for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
                    //     cout << "🐱 src_dst_data_size_table: src: " << it->first.src << ", dst: " << it->first.dst << ", size: " << it->second << endl;
                    // }
                    src_dst_data_size_table.erase({src_id, dst_id});
                }
                else{
                    src_dst_data_size_table[{src_id, dst_id}] -= params.mss * params.slot_length * (slot_end - Slot + 1);
                    // allocate_uplink();
                }
                // cout << "🥚 LocalArbiter " << this->id << " allocate slot " << Slot << ", to slot " << slot_end << " for flow " << src_id << " -> " << dst_id << ", src_tor is " << src_tor << ", src_Agg is " << agg_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << ", " << get_current_time() << endl;
            }
            else{
                // if(src_id == 1 && dst_id == 5){
                //     cout << "❌ LA " << this->id << " failed to allocate slot " << Slot << " to " << slot_end << " for flow " << src_id << " -> " << dst_id << " @ " << get_current_time() << endl;
                //     cout << "host_is_src_last_slot[src_id % hosts_per_pod]: " << host_is_src_last_slot[src_id % hosts_per_pod] << ", host_is_dst_last_slot[dst_id % hosts_per_pod]: " << host_is_dst_last_slot[dst_id % hosts_per_pod] << endl;
                // }
                // routing_table.erase({src_id, dst_id});
            }

        }
        else{ // 不在同一个pod内
            uint32_t Slot = static_cast<uint32_t>(ceil((get_current_time() + params.arbiter_lag * 4) / params.slot_length_in_s));
            uint32_t slots_needed = static_cast<uint32_t>(ceil(double(size) / (params.mss * params.slot_length)));
            uint32_t slot_end = Slot + min(params.max_slot_to_allocate, slots_needed - inschedule_slot_table[{src_id, dst_id}]) - 1;
            if(this->host_is_src_last_slot[src_id % hosts_per_pod] >= Slot){
                continue;
            }
            if(inschedule_slot_table[{src_id, dst_id}] >= slots_needed){
                continue;
            }
            bool src_tor_allocated = host_is_src_last_slot[src_id % hosts_per_pod] < Slot;
            if(src_tor_allocated == false){
                continue;
            }
            // 选src_agg
            bool src_Agg_allocated = false;
            
            uint32_t src_agg_id = 0;
            random_shuffle(k_2.begin(), k_2.end());
            for(int i = 0; i < params.k / 2; i++){
                src_agg_id = k_2[i] + params.k / 2 * this->id; // 需要加上bias
                if(ToR2Agg_last_slot[src_tor_id % (params.k / 2)][src_agg_id % (params.k / 2)] < Slot){
                    src_Agg_allocated = true;
                    host_is_src_last_slot[src_id % hosts_per_pod] = slot_end;
                    ToR2Agg_last_slot[src_tor_id % (params.k / 2)][src_agg_id % (params.k / 2)] = slot_end;
                    break;
                }
            }

            if(src_tor_allocated && src_Agg_allocated){
                // cout << "🥚 LocalArbiter " << this->id << " allocate slot " << Slot << ", to slot " << slot_end << " for flow " << src_id << " -> " << dst_id << ", src_tor is " << src_tor << ", src_Agg is " << src_agg_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
                ipr* ipr_info = new ipr(Slot, slot_end, size, src_id, src_agg_id, dst_id);
                LocalArbiter *dst_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / hosts_per_pod];
                inschedule_slot_table[{src_id, dst_id}] += slot_end - Slot + 1;
                ipr_packets[dst_la]->ipr_info.push_back(ipr_info);
            }
            else{
                // routing_table.erase({src_id, dst_id});
            }
        }        
        // auto end = high_resolution_clock::now(); 
        // // 计算时间差 
        // auto duration = duration_cast <microseconds> (end - start);
        // cout << "🕒 time consuming: " << duration.count() << " us" << endl;
    }
    // 结束计时点 

    // 发送聚合的ipr包
    for(auto it = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.begin(); it != dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.end(); it++){
        LocalArbiter *la = *it;
        if(la->id == this->id){
            continue;
        }
        HeirScheduleIPRPkt *ipr_packet = ipr_packets[la];
        if(ipr_packet->ipr_info.size() > 0){
            ipr_packet->size += ipr_packet->ipr_info.size() * ipr::info_size; // 每个ipr_info 16B
            send_request_to_la(la, ipr_packet);
            // cout << "🍄 LA " << this->id << " send interpod request to LA " << la->id << " @ " << get_current_time() << endl;
            // for(auto it = ipr_packet->ipr_info.begin(); it != ipr_packet->ipr_info.end(); it++){
            //     cout << (*it)->src_host_id << " -> " << (*it)->dst_host_id << endl;
            // }
        }
    }
    // 发送聚合的core_request包
    if(core_request_packet->core_rts_vector.size() > 0){
        core_request_packet->size += core_request_packet->core_rts_vector.size() * core_rts::info_size;
        send_request_to_ga(core_request_packet);
        // cout << "🍄 LA " << this->id << " send core request to GA @ " << get_current_time() << endl;
        // for(auto it = core_request_packet->core_rts_vector.begin(); it != core_request_packet->core_rts_vector.end(); it++){
        //     cout << (*it)->src_id << " -> " << (*it)->dst_id << endl;
        // }
    }
    // if(src_dst_data_size_table.size() > 0){
    // add_to_event_queue(new AllocateUplinkEvent(get_current_time() + params.arbiter_lag * params.slot_length_in_s, this));
    add_to_event_queue(new AllocateUplinkEvent(get_current_time() + params.slot_length_in_s, this));
    
    // }
    // add_to_event_queue(new AllocateUplinkEvent(get_current_time() + params.slot_length_in_s, this));
    // cout << "🐱 LocalArbiter " << this->id << " process rts from Host @ " << get_current_time() << endl;

}

void LocalArbiter::send_request_to_la(LocalArbiter *dst, HeirScheduleIPRPkt *ipr_packet){
    // cout << "🐇 LocalArbiter " << this->id << " send interpod request to LocalArbiter " << dst->id << " @ " << get_current_time() << endl;
    // HeirScheduleIPRPkt *ipr_packet = new HeirScheduleIPRPkt(get_current_time(), this, dst, ipr_info);
    // 先简化处理，src_la中的时延为2个slot
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, ipr_packet, toGCSQueues[rand() % num_gcs]));
}

void LocalArbiter::receive_ipr(Packet *packet){
    HeirScheduleIPRPkt *ipr_packet = (HeirScheduleIPRPkt *)packet;
    received_ipr_packets.push_back(ipr_packet);
    // cout << "🐺 LocalArbiter " << this->id << " receive interpod request from LocalArbiter " << packet->src->id << " @ slot " << static_cast<uint32_t>(get_current_time()/params.slot_length_in_s) << ", " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    
    // allocate_downlink(ipr_packet);
}

void LocalArbiter::allocate_downlink_inpod(ipr *ipr_info, HeirScheduleCoreRequestPkt* core_rts_packet){
    uint32_t Slot = ipr_info->slot;
    uint32_t slot_end = ipr_info->slot_end;
    uint32_t size = ipr_info->size;
    uint32_t src_id = ipr_info->src_host_id;
    uint32_t src_agg_id = ipr_info->src_agg_id;
    uint32_t dst_id = ipr_info->dst_host_id;
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    
    bool dst_ToR_allocated = false;
    bool dst_Agg_allocated = false;
    uint32_t dst_agg_id = src_agg_id;
    
    if(host_is_dst_last_slot[dst_id % hosts_per_pod] < Slot && Agg2ToR_last_slot[dst_agg_id % (params.k / 2)][dst_tor_id % (params.k / 2)] < Slot){
        dst_ToR_allocated = true;
        dst_Agg_allocated = true;
        host_is_dst_last_slot[dst_id % hosts_per_pod] = slot_end;
        Agg2ToR_last_slot[dst_agg_id % (params.k / 2)][dst_tor_id % (params.k / 2)] = slot_end;
    }
    // if(host_is_dst[Slot % params.T][dst_id % hosts_per_pod] == false){
    //     // host_is_dst[Slot % params.T][dst_id] = true;
    //     // 分配Agg->ToR链路（选dst_agg)
    //     if(src_id / hosts_per_pod == dst_id / hosts_per_pod){ // 在同一个Pod内
    //         dst_agg_id = src_agg_id;
    //         if(Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] == false){
    //             // 分配成功
    //             dst_ToR_allocated = true;
    //             dst_Agg_allocated = true;
    //             host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = true;
    //             Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = true;
    //         }
    //         else{
    //             // cout << "😰 Agg2ToR link not available" << endl;
    //         }
    //     }
    //     else{
    //         assert(false);
    //     }
    // }

    if (dst_ToR_allocated && dst_Agg_allocated){
        // cout << "🐣 LocalArbiter " << this->id << " allocate slot " << Slot << " for flow " << src_id << " -> " << dst_id << ", dst_Agg is " << dst_agg_id << ", dst_ToR is " << dst_tor_id << " @ " << get_current_time() << endl;
        core_rts_packet->core_rts_vector.push_back(new core_rts(Slot, slot_end, size, src_id, src_agg_id, dst_id, dst_agg_id));
    }
    else{
        // cout << "❌ allocate downlink failed! host_is_dst: " << host_is_dst[Slot % params.T][dst_id % hosts_per_pod] << ", dst_ToR_allocated: " << dst_ToR_allocated << ", dst_Agg_allocated: " << dst_Agg_allocated << endl;
        ipd* ipd_info = new ipd(Slot, slot_end, src_id, src_agg_id, dst_id);
        // HeirScheduleIPDPkt* ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, this, ipd_info);
        // take_back_link(ipd_packet);
    }

}

void LocalArbiter::allocate_downlink_crosspod(){
    HeirScheduleCoreRequestPkt* core_rts_packet = new HeirScheduleCoreRequestPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter);
    // 将流量按照size从小到大排序
    vector<pair<ipr*, uint32_t>> src_dst_data_size_vector;
    for(auto ipr_packet : received_ipr_packets){
        for(auto it = ipr_packet->ipr_info.begin(); it != ipr_packet->ipr_info.end(); it++){
            ipr* ipr_info = *it;
            // allocate_downlink_crosspod(ipr_info, core_rts_packet);
            // ipr* ipr_info = ipr_packet->ipr_info;
            uint32_t Slot = ipr_info->slot;
            uint32_t slot_end = ipr_info->slot_end;
            src_dst_data_size_vector.push_back({ipr_info, slot_end - Slot + 1});
        }
    }
    sort(src_dst_data_size_vector.begin(), src_dst_data_size_vector.end(), [](pair<ipr*, uint32_t> a, pair<ipr*, uint32_t> b){
        return a.second < b.second;
    });
    for(auto it = src_dst_data_size_vector.begin(); it != src_dst_data_size_vector.end(); it++){
        ipr* ipr_info = it->first;
        // allocate_downlink_crosspod(ipr_info, core_rts_packet);
        // ipr* ipr_info = ipr_packet->ipr_info;
        uint32_t Slot = ipr_info->slot;
        uint32_t slot_end = ipr_info->slot_end;
        uint32_t size = ipr_info->size;
        uint32_t src_id = ipr_info->src_host_id;
        uint32_t src_agg_id = ipr_info->src_agg_id;
        uint32_t dst_id = ipr_info->dst_host_id;
        uint32_t dst_tor_id = dst_id / (params.k / 2);
        LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / hosts_per_pod];

        bool dst_ToR_allocated = false;
        bool dst_Agg_allocated = false;
        uint32_t dst_agg_id = 0;

        dst_ToR_allocated = host_is_dst_last_slot[dst_id % hosts_per_pod] < Slot;
        if(dst_ToR_allocated){
            random_shuffle(k_2.begin(), k_2.end());
            for(int i = 0; i < params.k / 2; i++){
                dst_agg_id = k_2[i] + params.k / 2 * this->id; // 需要加上bias
                if(Agg2ToR_last_slot[dst_agg_id % (params.k / 2)][dst_tor_id % (params.k / 2)] < Slot){
                    // cout << "🍟 dst_agg: " << dst_agg_id << endl;
                    dst_Agg_allocated = true;
                    host_is_dst_last_slot[dst_id % hosts_per_pod] = slot_end;
                    Agg2ToR_last_slot[dst_agg_id % (params.k / 2)][dst_tor_id % (params.k / 2)] = slot_end;
                    break;
                }
            }
        }

        if (dst_ToR_allocated && dst_Agg_allocated){
            // cout << "🐣 LocalArbiter " << this->id << " allocate slot " << Slot << " to slot " << slot_end << " for flow " << src_id << " -> " << dst_id << ", dst_Agg is " << dst_agg_id << ", dst_ToR is " << dst_tor_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
            // ips* ips_info = new ips(Slot, src_id, src_agg_id, dst_id, dst_agg_id);
            // HeirScheduleIPSPkt *ips_packet = new HeirScheduleIPSPkt(get_current_time(), this, src_la, ips_info);
            // if(src_id / hosts_per_pod == dst_id / hosts_per_pod){
            //     update_routing_table(ips_packet);
            // }
            // else{
            //     // send_ips_to_la(src_la, ips_packet);
            // }
            core_rts* core_rts_info = new core_rts(Slot, slot_end, size, src_id, src_agg_id, dst_id, dst_agg_id);
            // HeirScheduleCoreRequestPkt* core_rts_packet = new HeirScheduleCoreRequestPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter, core_rts_info);
            // send_request_to_ga(core_rts_packet);
            core_rts_packet->core_rts_vector.push_back(core_rts_info);
        }
        else{
            // cout << "❌ allocate downlink failed! host_is_dst: " << host_is_dst[Slot % params.T][dst_id % hosts_per_pod] << ", dst_ToR_allocated: " << dst_ToR_allocated << ", dst_Agg_allocated: " << dst_Agg_allocated << endl;
            ipd* ipd_info = new ipd(Slot, slot_end, src_id, src_agg_id, dst_id);
            // HeirScheduleIPDPkt* ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, src_la, ipd_info);
            // send_deny_to_la(src_la, ipd_packet);
        }
    }
    for(auto it = received_ipr_packets.begin(); it != received_ipr_packets.end(); it++){
        delete *it;
    }
    received_ipr_packets.clear();

    core_rts_packet->size += core_rts_packet->core_rts_vector.size() * core_rts::info_size;
    if(core_rts_packet->core_rts_vector.size() > 0){
        send_request_to_ga(core_rts_packet);
    }
    // send_request_to_ga(core_rts_packet);

    add_to_event_queue(new AllocateDownlinkEvent(get_current_time() + params.slot_length_in_s, this));
    
    // }
    
    // delete ipr_packet;
}

void LocalArbiter::receive_ipd(Packet *packet){
    HeirScheduleIPDPkt *ipd_packet = (HeirScheduleIPDPkt *)packet;
    // cout << "🐹 LocalArbiter " << this->id << " receive interpod deny from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    take_back_link(ipd_packet);
    // delete ipd_packet;
}

void LocalArbiter::take_back_link(HeirScheduleIPDPkt *ipd_packet){
    for(auto it : ipd_packet->ipd_info){
        ipd* ipd_info = it;
        uint32_t slot = ipd_info->slot;
        uint32_t slot_end = ipd_info->slot_end;
        uint32_t src_id = ipd_info->src_host_id;
        uint32_t src_agg_id = ipd_info->src_agg_id;
        uint32_t dst_id = ipd_info->dst_host_id;
        uint32_t src_tor_id = src_id / (params.k / 2);
        // uint32_t dst_tor_id = dst_id / (params.k / 2);
        // LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / hosts_per_pod];
        // // host->ToR
        // assert(host_is_src[Slot % params.T][src_id % hosts_per_pod] == true);
        // host_is_src[Slot % params.T][src_id % hosts_per_pod] = false;
        // // ToR->Agg
        // assert(ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] == true);
        // ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] = false;
    
        // dst->ToR
        // assert(host_is_src_last_slot[src_id % hosts_per_pod] == slot_end);
        // if(host_is_src_last_slot[src_id % hosts_per_pod] == slot_end){
        //     host_is_src_last_slot[src_id % hosts_per_pod] = Slot - 1;
        // }
        uint32_t current_slot = static_cast<uint32_t>(ceil(get_current_time() / params.slot_length_in_s));
        if(slot > current_slot){
            host_is_src[slot % params.T][src_id % hosts_per_pod] = false;
            ToR2Agg[slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] = false;
        }
        if(inschedule_slot_table[{src_id, dst_id}] == 0) assert(false);
        inschedule_slot_table[{src_id, dst_id}]--;
    }
    // 删除src_dst_slot_table的src-dst表项
    // inschedule_slot_table[{src_id, dst_id}] -= slot_end - Slot + 1;
    // if(src_id == 0 && dst_id == 29){
    //     cout << "2️⃣ Inter-Pod Deny!" << endl;
    //     cout << "flow 0 -> 29 remaining size: " << src_dst_data_size_table[{src_id, dst_id}] << endl;
    //     cout << "flow 0 -> 29 inschedule_slot: " << inschedule_slot_table[{src_id, dst_id}] << endl;
    // }
    // src_dst_slot_table.erase({src_id, dst_id});
    // 删除routing_table的src-dst表项
    // routing_table.erase({src_id, dst_id});
    delete ipd_packet;
}

// void LocalArbiter::send_ips_to_la(LocalArbiter *src, HeirScheduleIPSPkt *ips_packet){
//     // cout << "🐔 LocalArbiter " << this->id << " send interpod response to LocalArbiter " << src->id << " @ " << get_current_time() << endl;
//     add_to_event_queue(new PacketQueuingEvent(get_current_time(), ips_packet, toGCSQueues[rand() % num_gcs])); // 简化处理：随机选择一个GCS发送
// }

void LocalArbiter::receive_ips(Packet *packet){
    HeirScheduleIPSPkt *ips_packet = (HeirScheduleIPSPkt *)packet;
    // cout << "🐶 LocalArbiter " << this->id << " receive interpod response from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    
    // update_routing_table(ips_packet);
}

// void LocalArbiter::update_routing_table(HeirScheduleIPSPkt *ips_packet){
//     // 装填进路由表
//     ips* ips_info = ips_packet->ips_info;
//     uint32_t Slot = ips_info->slot;
//     uint32_t src_id = ips_info->src_host_id;
//     uint32_t src_agg_id = ips_info->src_agg_id;
//     uint32_t dst_id = ips_info->dst_host_id;
//     uint32_t dst_agg_id = ips_info->dst_agg_id;
//     // assert(routing_table[{src_id, dst_id}] != nullptr && routing_table[{src_id, dst_id}]->slot == Slot);
//     // assert((routing_table[{src_id, dst_id}] != nullptr && routing_table[{src_id, dst_id}]->slot == Slot));
//     // routing_table[{src_id, dst_id}]->dst_agg_id = dst_agg_id;
//     // routing_table[{src_id, dst_id}]->dst_tor_id = dst_id / (params.k / 2);
//     // cout << "🐣 LA " << this->id << " update routing table for flow " << src_id << " -> " << dst_id << ", src_tor: " << routing_table[{src_id, dst_id}]->src_tor_id << ", src_agg: " << routing_table[{src_id, dst_id}]->src_agg_id << ", dst_agg: " << routing_table[{src_id, dst_id}]->dst_agg_id << ", dst_tor: " << routing_table[{src_id, dst_id}]->dst_tor_id << " @ " << get_current_time() << endl;
//     delete ips_packet;
// }

void LocalArbiter::send_deny_to_la(LocalArbiter *src, HeirScheduleIPDPkt* ipd_packet){
    // cout << "🐵 LocalArbiter " << this->id << " send interpod deny to LocalArbiter " << src->id << " @ " << get_current_time() << endl;
    // HeirScheduleIPDPkt *ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, dst, ipr_info);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, ipd_packet, toGCSQueues[rand() % num_gcs])); 
}

void LocalArbiter::send_request_to_ga(HeirScheduleCoreRequestPkt *core_rts_packet){
    // cout << "🐹 LocalArbiter " << this->id << " send agg-agg request to GlobalArbiter @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, core_rts_packet, toGCSQueues[rand() % num_gcs]));
}

void LocalArbiter::receive_core_schd(Packet *packet){
    HeirScheduleCoreSCHDPkt *core_schd_packet = (HeirScheduleCoreSCHDPkt *)packet;
    // cout << "🐨 LocalArbiter " << this->id << " receive core schd from GlobalArbiter @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    for(auto it = core_schd_packet->core_schd_vector.begin(); it != core_schd_packet->core_schd_vector.end(); it++){
        core_schd* core_schd_info = *it;
        generate_full_path(core_schd_info);
    }
    delete core_schd_packet;
    // generate_full_path(core_schd_packet);
}

void LocalArbiter::generate_full_path(core_schd *core_schd_info){
    // core_schd* core_schd_info = core_schd_packet->core_schd_info;
    uint32_t slot = core_schd_info->Slot;
    uint32_t src_id = core_schd_info->src_id;
    uint32_t src_tor_id = src_id / (params.k / 2);
    uint32_t src_agg_id = core_schd_info->src_agg_id;
    uint32_t core_id = core_schd_info->core_id;
    uint32_t dst_id = core_schd_info->dst_id;
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    uint32_t dst_agg_id = core_schd_info->dst_agg_id;
    Flow* flow = core_schd_info->flow;


    // 组装完整路径
    // routing_table[{src_id, dst_id}]->dst_agg_id = dst_agg_id;
    // routing_table[{src_id, dst_id}]->dst_tor_id = dst_id / (params.k / 2);
    // routing_table[{src_id, dst_id}]->core_id = core_id;
    // inschedule_slot_table[{src_id, dst_id}] -= slot_end - Slot + 1;
    // if(src_id == 0 && dst_id == 29){
    //     cout << "4️⃣ Generate Full Path!" << endl;
    //     cout << "flow 0 -> 29 remaining size: " << src_dst_data_size_table[{src_id, dst_id}] << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
    //     cout << "flow 0 -> 29 inschedule_slot: " << inschedule_slot_table[{src_id, dst_id}] << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
    // }

    // cout << "🐥 LA " << this->id << " generate full path for flow " << src_id << " -> " << dst_id << ", from slot " << Slot << " to slot " << slot_end << ", src_tor: " << src_tor_id << ", src_agg: " << src_agg_id << ", core: " << core_id << ", dst_agg: " << dst_agg_id << ", dst_tor: " << dst_tor_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
    // core_schd* core_schd_copy = new core_schd(Slot, slot_end, src_id, src_agg_id, core_id, dst_id, dst_agg_id);
    // delete core_schd_packet;
    SCHD* schd = new SCHD(slot, slot, src_id, src_tor_id, src_agg_id, core_id, dst_agg_id, dst_tor_id, dst_id);
    HeirScheduleSCHDPkt *schd_packet = new HeirScheduleSCHDPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[src_id], schd);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), schd_packet, toLCSQueues[(src_id % hosts_per_pod) / (params.k/2)]));
    
    // 更新状态变量
    // routing_table.erase({src_id, dst_id});
    // src_dst_slot_table.erase({src_id, dst_id});
    // host_is_src[Slot % params.T][src_id % hosts_per_pod] = false;
    // dst_la->host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = false;
    // ToR2Agg[Slot % params.T][src_id / (params.k / 2) % tors_per_pod][src_agg_id % aggs_per_pod] = false;
    // dst_la->Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_id / (params.k / 2) % tors_per_pod] = false;
    // ga->CoreOccupationIn[Slot % params.T][core_id][src_agg_id] = false;
    // ga->CoreOccupationOut[Slot % params.T][core_id][dst_agg_id] = false;
    // add_to_event_queue(new RestoreLinkEvent(Slot * params.slot_length_in_s, core_schd_copy));
    if(inschedule_slot_table[{src_id, dst_id}] == 0) assert(false);
    inschedule_slot_table[{src_id, dst_id}]--;
    // if(src_dst_data_size_table[{src_id, dst_id}] <= params.mss * params.slot_length){
    //     // cout << "💊 LA " << this->id << " erase src_dst_data_size_table! src: " << src_id << ", dst: " << dst_id << ", size: " << src_dst_data_size_table[{src_id, dst_id}] << ", params.mss * params.slot_length: " << params.mss * params.slot_length << endl;
    //     // for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
    //     //     cout << "🐱 src_dst_data_size_table: src: " << it->first.src << ", dst: " << it->first.dst << ", size: " << it->second << endl;
    //     // }
    //     src_dst_data_size_table.erase({src_id, dst_id});
    // }
    // else{
    //     src_dst_data_size_table[{src_id, dst_id}] -= params.mss * params.slot_length;
    //     // allocate_uplink();
    // }
    if(flow_size_table[flow] <= params.mss * params.slot_length){
        flow_size_table.erase(flow);
    }
    else{
        flow_size_table[flow] -= params.mss * params.slot_length;
    }
}

void LocalArbiter::receive_core_deny(Packet *packet){
    // cout << "🐼 LocalArbiter " << this->id << " receive agg-agg deny from GlobalArbiter @ " << get_current_time() << endl;
    HeirScheduleCoreDenyPkt *core_deny_packet = (HeirScheduleCoreDenyPkt *)packet;
    for(auto it = core_deny_packet->core_deny_vector.begin(); it != core_deny_packet->core_deny_vector.end(); it++){
        core_deny* core_deny_info = *it;
        take_back_link(core_deny_info);
    }
    delete core_deny_packet;
    // take_back_link(core_deny_packet);
}

void LocalArbiter::take_back_link(core_deny *core_deny_info){
    // core_deny* core_deny_info = core_deny_packet->core_deny_info;
    uint32_t slot = core_deny_info->Slot;
    uint32_t src_id = core_deny_info->src_id;
    uint32_t src_agg_id = core_deny_info->src_agg_id;
    uint32_t dst_id = core_deny_info->dst_id;
    uint32_t dst_agg_id = core_deny_info->dst_agg_id;
    uint32_t src_tor_id = src_id / (params.k / 2);
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    bool is_src_la = core_deny_info->is_src_la;
    uint32_t current_slot = static_cast<uint32_t>(ceil(get_current_time() / params.slot_length_in_s));
    if (is_src_la){
        if(current_slot < slot){
            host_is_src[slot % params.T][src_id % hosts_per_pod] = false;
            ToR2Agg[slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] = false;
        }
        if(inschedule_slot_table[{src_id, dst_id}] == 0) assert(false);
        inschedule_slot_table[{src_id, dst_id}]--;
    }
    else{
        // 注意，在dstPod中，slot需要加上一个偏置
        slot += static_cast<uint32_t>(round(params.propagation_delay_data * 2 / params.slot_length_in_s));
        if(current_slot < slot){
            host_is_dst[slot % params.T][dst_id % hosts_per_pod] = false;
            Agg2ToR[slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = false;
        }
    }
    // delete core_deny_packet;
}


// ------------------------------------------------- GlobalArbiter -------------------------------------------------
GlobalArbiter::GlobalArbiter(uint32_t id, double rate, uint32_t num_gcs, uint32_t queue_type) : Host(id, 0, queue_type, GLOBAL_ARBITER) {
    this->type = GLOBAL_ARBITER;
    this->num_gcs = num_gcs;
    for(uint32_t i = 0; i < num_gcs; i++){
        this->toGCSQueues.push_back(Factory::get_queue(i, rate, params.queue_size_ctrl, DCTCP_QUEUE, 0, GA_TO_GCS));
    }
    // toGCSQueue = Factory::get_queue(0, rate, params.queue_size_ctrl, DCTCP_QUEUE, 0, GA_TO_GCS);
    this->local_time_bias = 0.0;
    for(uint32_t i = 0; i < params.T; i++){
        vector<vector<bool>> CoreOccupationIn_t;
        vector<vector<bool>> CoreOccupationOut_t;
        for(uint32_t j = 0; j < params.k * params.k / 4; j++){ // core的数量
            CoreOccupationIn_t.push_back(vector<bool>(params.k / 2, false));
            CoreOccupationOut_t.push_back(vector<bool>(params.k / 2, false));
        }
        this->CoreOccupationIn.push_back(CoreOccupationIn_t);
        this->CoreOccupationOut.push_back(CoreOccupationOut_t);
    }
    for(uint32_t i = 0; i < params.k * params.k / 4; i++){
        this->CoreOccupationIn_last_slot.push_back(vector<uint32_t>(params.k * params.k / 4, 0));
        this->CoreOccupationOut_last_slot.push_back(vector<uint32_t>(params.k * params.k / 4, 0));
    }
}

void GlobalArbiter::send_sync_message_to_la(){
    for (uint32_t i = 0; i < params.k / 2; i++){
    // for (uint32_t i = 0; i < 1; i++){
        SyncMessage *sync_packet = new SyncMessage(this, dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[i], get_current_time() + local_time_bias);
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), sync_packet, toGCSQueues[rand() % num_gcs]));
        cout << "🧠 GlobalArbiter " << this->id << " send sync message to LocalArbiter " << i << endl;
    }
}

void GlobalArbiter::receive(Packet *packet) {
    // TODO: implement
    // cout << "🧠 GlobalArbiter " << this->id << " receive packet" << endl;
    switch (packet->type)
    {
    case SYNC_MSG:
        assert(false);
        break;
    case DELAY_REQ_MSG:
        receive_delay_request_message(packet);
        break;
    case DELAY_RES_MSG:
        assert(false);
        break;
    case CORE_RTS:
        receive_core_rts(packet);
        break;
    default:
        break;
    }
}

void GlobalArbiter::receive_delay_request_message(Packet *packet){
    cout << "🧠 GlobalArbiter " << this->id << " receive delay request message @ " << get_current_time() << endl;
    DelayRequestMessage *delay_request_packet = (DelayRequestMessage *)packet;
    double T4_time = get_current_time() + local_time_bias - delay_request_packet->innetwork_delay;
    DelayResponseMessage *delay_response_packet = new DelayResponseMessage(this, packet->src, T4_time);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_response_packet, toGCSQueues[rand() % num_gcs]));
}

void GlobalArbiter::receive_core_rts(Packet *packet){
    HeirScheduleCoreRequestPkt *core_rts_packet = (HeirScheduleCoreRequestPkt *)packet;
    received_core_rts_packets.push_back(core_rts_packet);
    // cout << "🐛 GlobalArbiter " << this->id << " receive core rts from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    // allocate_core_link();
}

void GlobalArbiter::allocate_core_link(){
    // cout << "🐛 GlobalArbiter " << this->id << " allocate core link @ " << get_current_time() << endl;
    uint32_t current_slot = static_cast<uint32_t>(ceil(get_current_time() / params.slot_length_in_s));
    unordered_map<LocalArbiter*, HeirScheduleCoreSCHDPkt*> core_schd_map;
    unordered_map<LocalArbiter*, HeirScheduleCoreDenyPkt*> core_deny_map;
    for(auto it = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.begin(); it != dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters.end(); it++){
        LocalArbiter *la = *it;
        core_schd_map[la] = new HeirScheduleCoreSCHDPkt(get_current_time(), this, la);
        core_deny_map[la] = new HeirScheduleCoreDenyPkt(get_current_time(), this, la);
    }
    vector<pair<core_rts*, uint32_t>> src_dst_data_size_vector;
    for(auto core_rts_packet : received_core_rts_packets){
        for(auto it = core_rts_packet->core_rts_vector.begin(); it != core_rts_packet->core_rts_vector.end(); it++){
            core_rts* core_rts_info = *it;
            src_dst_data_size_vector.push_back({core_rts_info, core_rts_info->size});
        }
    }
    sort(src_dst_data_size_vector.begin(), src_dst_data_size_vector.end(), [](pair<core_rts*, uint32_t> a, pair<core_rts*, uint32_t> b){
        return a.second < b.second;
    });
    for(auto it = src_dst_data_size_vector.begin(); it != src_dst_data_size_vector.end(); it++){
        core_rts* core_rts_info = it->first;
        uint32_t slot = core_rts_info->Slot;
        uint32_t src_id = core_rts_info->src_id;
        uint32_t src_agg_id = core_rts_info->src_agg_id;
        uint32_t dst_id = core_rts_info->dst_id;
        uint32_t dst_agg_id = core_rts_info->dst_agg_id;
        Flow* flow = core_rts_info->flow;
        uint32_t core_id = dynamic_cast<HeirScheduleTopology*>(topology)->src_dst_agg_to_core_map[{src_agg_id, dst_agg_id}];
        uint32_t port_In = dynamic_cast<HeirScheduleTopology*>(topology)->core_to_agg_port[core_id][src_agg_id];
        uint32_t port_Out = dynamic_cast<HeirScheduleTopology*>(topology)->core_to_agg_port[core_id][dst_agg_id];
        if(slot <= current_slot){
            cout << "❌ Global arbiter slot: " << slot << ", current_slot: " << current_slot << endl;
            cout << "❌ Global arbiter src_id: " << src_id << ", src_agg_id: " << src_agg_id << ", dst_id: " << dst_id << ", dst_agg_id: " << dst_agg_id << ", core_id: " << core_id << ", port_In: " << port_In << ", port_Out: " << port_Out << endl;
            assert(slot > current_slot);
        }
        if(CoreOccupationIn[slot % params.T][core_id][port_In] == false && CoreOccupationOut[slot % params.T][core_id][port_Out] == false){
            // 分配成功
            CoreOccupationIn[slot % params.T][core_id][port_In] = true;
            CoreOccupationOut[slot % params.T][core_id][port_Out] = true;
            core_schd* core_schd_info = new core_schd(slot, slot, src_id, src_agg_id, core_id, dst_id, dst_agg_id);
            core_schd_info->flow = flow;
            // if(src_id == 11 && dst_id == 121){
            //     cout << "🥭 Global Arbiter allocate slot " << slot << " for flow " << src_id << " -> " << dst_id << ", core_id: " << core_id << ", src_agg_id: " << src_agg_id << ", dst_agg_id: " << dst_agg_id << " @ slot " << int(get_current_time() / params.slot_length_in_s) << endl;
            // }
            LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / (params.k * params.k / 4)];
            core_schd_map[src_la]->core_schd_vector.push_back(core_schd_info);
        }
        else{
            // 分配失败
            core_deny* core_deny_info_src = new core_deny(slot, slot, src_id, src_agg_id, dst_id, dst_agg_id);
            LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / (params.k * params.k / 4)];
            core_deny_info_src->is_src_la = true;
            core_deny_info_src->flow = flow;
            core_deny_map[src_la]->core_deny_vector.push_back(core_deny_info_src);
            
            core_deny* core_deny_info_dst = new core_deny(slot, slot, src_id, src_agg_id, dst_id, dst_agg_id);
            LocalArbiter *dst_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / (params.k * params.k / 4)];
            core_deny_info_dst->is_src_la = false;
            core_deny_info_dst->flow = flow;
            core_deny_map[dst_la]->core_deny_vector.push_back(core_deny_info_dst);

            // cout << "❌ Global arbiter src_id: " << src_id << ", src_agg_id: " << src_agg_id << ", dst_id: " << dst_id << ", dst_agg_id: " << dst_agg_id << ", core_id: " << core_id << ", port_In: " << port_In << ", port_Out: " << port_Out << endl;
        }
        
    }

    // Debug
    int total_rts = src_dst_data_size_vector.size();
    int total_schd = 0;
    int total_deny = 0;
    for(auto it = core_schd_map.begin(); it != core_schd_map.end(); it++){
        HeirScheduleCoreSCHDPkt *core_schd_packet = it->second;
        total_schd += core_schd_packet->core_schd_vector.size();
    }

    for(auto it = core_deny_map.begin(); it != core_deny_map.end(); it++){
        HeirScheduleCoreDenyPkt *core_deny_packet = it->second;
        total_deny += core_deny_packet->core_deny_vector.size();
    }
    // if(total_deny > 0){
    //     cout << "📉 GlobalArbiter " << this->id << " total_rts: " << total_rts << ", total_schd: " << total_schd << ", ratio: " << double(total_schd) / double(total_rts) << ", total_deny: " << total_deny/2 << ", ratio: " << double(total_deny/2) / double(total_rts) << endl;
    // }
    if(double(total_deny/2) / double(total_rts) > 0.5){
        cout << "📈 Global arbiter rts:" << endl;
        for(auto it : src_dst_data_size_vector){
            core_rts* core_rts_info = it.first;
            uint32_t Slot = core_rts_info->Slot;
            uint32_t slot_end = core_rts_info->slot_end;
            uint32_t src_id = core_rts_info->src_id;
            uint32_t src_agg_id = core_rts_info->src_agg_id;
            uint32_t dst_id = core_rts_info->dst_id;
            uint32_t dst_agg_id = core_rts_info->dst_agg_id;
            uint32_t core_id = dynamic_cast<HeirScheduleTopology*>(topology)->src_dst_agg_to_core_map[{src_agg_id, dst_agg_id}];

            cout << "src_agg: " << src_agg_id << ", dst_agg: " << dst_agg_id << ", core_id: " << core_id << ", Slot: " << Slot << ", slot_end: " << slot_end << endl;
        }

        cout << "✅ Global arbiter schd:" << endl;
        for(auto it = core_schd_map.begin(); it != core_schd_map.end(); it++){
            HeirScheduleCoreSCHDPkt *core_schd_packet = it->second;
            LocalArbiter *la = it->first;
            for(auto it = core_schd_packet->core_schd_vector.begin(); it != core_schd_packet->core_schd_vector.end(); it++){
                core_schd* core_schd_info = *it;
                uint32_t Slot = core_schd_info->Slot;
                uint32_t slot_end = core_schd_info->slot_end;
                uint32_t src_id = core_schd_info->src_id;
                uint32_t src_agg_id = core_schd_info->src_agg_id;
                uint32_t dst_id = core_schd_info->dst_id;
                uint32_t dst_agg_id = core_schd_info->dst_agg_id;
                uint32_t core_id = core_schd_info->core_id;
                cout << "src_agg: " << src_agg_id << ", dst_agg: " << dst_agg_id << ", core_id: " << core_id << ", Slot: " << Slot << ", slot_end: " << slot_end << endl;
            }
        }
    }

    for(auto it: received_core_rts_packets){
        delete it;
    }
    received_core_rts_packets.clear();
    for(auto it = core_schd_map.begin(); it != core_schd_map.end(); it++){
        HeirScheduleCoreSCHDPkt *core_schd_packet = it->second;
        LocalArbiter *la = it->first;
        core_schd_packet->size += core_schd_packet->core_schd_vector.size() * core_schd::info_size;
        if(core_schd_packet->core_schd_vector.size() > 0){
            send_core_schd_to_la(core_schd_packet);
        }
        total_schd += core_schd_packet->core_schd_vector.size();
    }
    
    for(auto it = core_deny_map.begin(); it != core_deny_map.end(); it++){
        HeirScheduleCoreDenyPkt *core_deny_packet = it->second;
        LocalArbiter *la = it->first;
        core_deny_packet->size += core_deny_packet->core_deny_vector.size() * core_deny::info_size;
        if(core_deny_packet->core_deny_vector.size() > 0){
            send_core_deny_to_la(core_deny_packet);
        }
        total_deny += core_deny_packet->core_deny_vector.size();
    }
    
    // 复原本slot的链路状态
    for(uint32_t i = 0; i < params.k * params.k / 4; i++){
        for(uint32_t j = 0; j < params.k / 2; j++){
            CoreOccupationIn[current_slot % params.T][i][j] = false;
            CoreOccupationOut[current_slot % params.T][i][j] = false;
        }
    }

    // add_to_event_queue(new CoreAllocateLinkEvent(get_current_time() + params.arbiter_lag * params.slot_length_in_s, this));
    add_to_event_queue(new CoreAllocateLinkEvent(get_current_time() + params.slot_length_in_s, this));

}

void GlobalArbiter::send_core_schd_to_la(HeirScheduleCoreSCHDPkt *core_schd_packet){
    // cout << "🐔 GlobalArbiter " << this->id << " send core schd to LocalArbiter " << core_schd_packet->dst->id << " @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, core_schd_packet, toGCSQueues[rand() % num_gcs]));
}

void GlobalArbiter::send_core_deny_to_la(HeirScheduleCoreDenyPkt *core_deny_packet){
    // cout << "🦄 GlobalArbiter " << this->id << " send core deny to LocalArbiter " << core_deny_packet->dst->id << " @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + params.arbiter_lag, core_deny_packet, toGCSQueues[rand() % num_gcs]));
}

// ------------------------------------------------- Switch -------------------------------------------------
Switch::Switch(uint32_t id, uint32_t switch_type) : Node(id, SWITCH) {
    this->type = switch_type;
}

CoreSwitch::CoreSwitch(uint32_t id, uint32_t numQueue, double rate, uint32_t type) : Switch(id, CORE_SWITCH) {
    //向下连agg的端口数
    for (uint32_t i = 0; i < numQueue; i++) {
        toAggQueues.push_back(Factory::get_queue(i, rate, params.queue_size, type, 0, CORE_TO_AGG));
    }
}


AggSwitch::AggSwitch(
        uint32_t id, 
        uint32_t numOfQToToR, 
        double r1,
        uint32_t numOfQToCore, 
        double r2, 
        uint32_t type
        ) : Switch(id, AGG_SWITCH) {
    // 向下连ToR的端口数
    for (uint32_t i = 0; i < numOfQToToR; i++) {
        toToRQueues.push_back(Factory::get_queue(i, r1, params.queue_size, type, 0, AGG_TO_TOR));
    }
    
    // 向上连core的端口数
    for (uint32_t i = 0; i < numOfQToCore; i++) {
        toCoreQueues.push_back(Factory::get_queue(i, r2, params.queue_size, type, 0, AGG_TO_CORE));
    }
}


ToRSwitch::ToRSwitch(
        uint32_t id, 
        uint32_t numOfQToHost, 
        double r1,
        uint32_t numOfQToAgg, 
        double r2, 
        uint32_t type
        ) : Switch(id, TOR_SWITCH) {
    // 向下连host的端口数
    for (uint32_t i = 0; i < numOfQToHost; i++) {
        toHostQueues.push_back(Factory::get_queue(i, r1, params.queue_size, type, 0, TOR_TO_HOST));
    }
    // 向上连agg的端口数
    for (uint32_t i = 0; i < numOfQToAgg; i++) {
        toAggQueues.push_back(Factory::get_queue(i, r2, params.queue_size, type, 0, TOR_TO_AGG));
    }
}

LocalControlSwitch::LocalControlSwitch(uint32_t id, uint32_t numOfQToHost, double r1, uint32_t numOfQToLA, double r2, uint32_t queue_type) : Switch(id, LOCAL_CONTROL_SWITCH) {
    //向下连host的端口数
    for (uint32_t i = 0; i < numOfQToHost; i++) {
        toHostQueues.push_back(Factory::get_queue(i, r1, params.queue_size_ctrl, DCTCP_QUEUE, 0, LCS_TO_HOST));
    }
    
    // 向上连LA的端口数
    toLAQueue = Factory::get_queue(0, r2, params.queue_size_ctrl, DCTCP_QUEUE, 0, LCS_TO_LA);
}

GlobalControlSwitch::GlobalControlSwitch(uint32_t id, uint32_t numOfQToLA, double r1, uint32_t numOfQToGA, double r2, uint32_t queue_type) : Switch(id, GLOBAL_CONTROL_SWITCH) {
    //连接LA的端口数，连向k个LA
    for (uint32_t i = 0; i < numOfQToLA; i++) {
        toLAQueues.push_back(Factory::get_queue(i, r1, params.queue_size_ctrl, DCTCP_QUEUE, 0, GCS_TO_LA));
    }

    //连接GA的端口
    toGAQueue = Factory::get_queue(0, r2, params.queue_size_ctrl, DCTCP_QUEUE, 0, GCS_TO_GA);
}
