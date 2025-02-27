#include "packet.h"
#include "flow.h"

#include "../ext/factory.h"
#include "../run/params.h"
#include "topology.h"
#include <cassert>
#include <random>
#include <bitset>
#include <algorithm>

using namespace std;

extern DCExpParams params;
extern Topology *topology;
extern double get_current_time();

// #define HOST_TO_TOR 0
// #define TOR_TO_AGG 1
// #define AGG_TO_CORE 2
// #define CORE_TO_AGG 3
// #define AGG_TO_TOR 4
// #define TOR_TO_HOST 5

// #define HOST_TO_LCS 10
// #define LCS_TO_LA 11
// #define LA_TO_GCS 12
// #define GCS_TO_LA 13
// #define LA_TO_S3 14
// #define S3_TO_GA 15
// #define GA_TO_S3 16
// #define S3_TO_LA 17
// #define LA_TO_LCS 18
// #define LCS_TO_HOST 19

bool FlowComparator::operator() (Flow *a, Flow *b) {
    return a->flow_priority > b->flow_priority;
    //  if(a->flow_priority > b->flow_priority)
    //    return true;
    //  else if(a->flow_priority == b->flow_priority)
    //    return a->id > b->id;
    //  else
    //    return false;
}

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
    // cout << "👀 HeirScheduleHost " << this->id << " receive schd: slot: " << schd->slot << ", src_host_id: " << schd->src_host_id << ", dst_host_id: " << schd->dst_host_id << ", src_tor_id: " << schd->src_tor_id << ", dst_tor_id: " << schd->dst_tor_id << ", src_agg_id: " << schd->src_agg_id << ", dst_agg_id: " << schd->dst_agg_id << ", core_id: " << schd->core_id << endl;
    uint32_t slot = schd->slot;
    double time_to_send = double(slot) * params.slot_length_in_s;
    // cout << "🤖 HeirScheduleHost " << this->id << " slot: " << slot << ", time_to_send: " << time_to_send << ", current time: " << get_current_time() << endl;
    assert(time_to_send >= get_current_time());
    
    // 发送数据包
    for(int i = 0; i < params.slot_length; i++){
        // cout << "🍎" << endl;
        HeirScheduleDataPkt *data_packet = get_data_packet(schd->dst_host_id);

        data_packet->path = schd;
        data_packet->sending_time = time_to_send + double(i) * toToRQueue->get_transmission_delay(params.mss + params.hdr_size);
        add_to_event_queue(new PacketQueuingEvent(data_packet->sending_time, data_packet, toToRQueue));
        // cout << "🍋‍🟩 Host " << this->id << " send data packet " << data_packet->unique_id << " with size: " << data_packet->size << " to " << schd->dst_host_id << " @ " << data_packet->sending_time << endl;
        // cout << "💜" << endl;
    }
}



HeirScheduleDataPkt *HeirScheduleHost::get_data_packet(uint32_t dst){
    // 从优先级队列中取出数据包，暂时不考虑PIAS
    // cout << "🍒 HeirScheduleHost " << this->id << " start getting data packet to " << dst << endl;
    uint32_t cum_payload_size = 0;
    vector<Flow*> flow_list;
    vector<uint32_t> flow_segment_sizes;
    vector<uint32_t> flow_segment_begin_seq_no;
    bool has_data = false;
    // for(auto it = this->per_dst_queues[0][dst].begin(); it != this->per_dst_queues[0][dst].end(); it++){
    while(cum_payload_size < params.mss && per_dst_queues[0][dst].size() > 0){
        has_data = true;
        flow_data_at_src flow_data = per_dst_queues[0][dst].front();
        per_dst_queues[0][dst].pop_front();
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
            per_dst_queues[0][dst].push_front(flow_data);
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
        data_packet->flows[i]->received_seqs[data_packet->flow_segment_begin_seq_no[i]] = data_packet->flow_segment_sizes[i];
        data_packet->flows[i]->received_payloads += data_packet->flow_segment_sizes[i];
        now_receiving.insert(data_packet->flows[i]);
    }

    for(auto f : now_receiving){
        f->dst->now_receiving.insert(f);
        vector<uint32_t> seq_nos;
        for(auto it = f->received_seqs.begin(); it != f->received_seqs.end(); it++){
            seq_nos.push_back(it->first);
        }
        sort(seq_nos.begin(), seq_nos.end());

        // for(auto it = seq_nos.begin(); it != seq_nos.end(); it++){
        //     cout << *it << " ";
        // }
        // cout << endl;

        // 更新recv_till
        uint32_t recv_till = 0;
        bool disorder_flag = false;
        for(int i = 0; i < int(seq_nos.size()); i++){
            f->recv_max = seq_nos[i] + f->received_seqs[seq_nos[i]]; // 更新收到的最大包序列号
            if(recv_till == seq_nos[i]){
                recv_till += f->received_seqs[seq_nos[i]];
            }
            else{
                disorder_flag = true;
                f->unordered_cell = seq_nos.size() - i;
                break;
            }
        }
        if(disorder_flag == false){
            f->unordered_cell = 0;
            f->max_out_of_order_buffer = 0;
        }
        // cout << "🫑" << endl;

        uint32_t unordered_bytes = 0;
        for(int i = int(seq_nos.size() - f->unordered_cell); i < seq_nos.size(); i++){
            unordered_bytes += f->received_seqs[seq_nos[i]];
        }
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

        f->recv_till = recv_till;
        if(f->recv_till == f->size){
            assert(f->received_payloads == f->size);
            f->finished = true;
            f->finish_time = get_current_time();
            f->flow_completion_time = f->finish_time - f->start_time;

            f->unordered_cell = 0;
            f->max_out_of_order_buffer = 0;
            f->dst->received_bytes_all += f->size;
            f->dst->received_last_packet_time = get_current_time(); // 更新最后一次收包时间
            f->dst->now_receiving.erase(f);

            cout << "✅ Flow " << f->id << " finished at " << get_current_time() << ", oracle fct is " << dynamic_cast<HeirScheduleTopology*>(topology)->get_oracle_fct(f) << "us, slowdown is " << 1e6*f->flow_completion_time / dynamic_cast<HeirScheduleTopology*>(topology)->get_oracle_fct(f) << endl;
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
        cout << "🐱 LocalArbiter " << this->id << " rts: src_id: " << src_id << " dst_id: " << dst_id << " size: " << size << ", src_dst_data_size_table[{src_id, dst_id}]: " << src_dst_data_size_table[{src_id, dst_id}] << endl;
    }
    delete rts_packet;
    // allocate_uplink();
}

void LocalArbiter::allocate_uplink(){
    // for (auto it = rts_packet->rts_vector.begin(); it != rts_packet->rts_vector.end(); it++){
    for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
        // cout << "🌶️ Allocate! @ " << get_current_time() << endl;
        uint32_t src_id = it->first.src;
        uint32_t dst_id = it->first.dst;
        uint32_t size = it->second;
        if (routing_table[{src_id, dst_id}] == nullptr){
            routing_table[{src_id, dst_id}] = new SCHD();
        }
        else{ // 如果已经在路由表中，说明已经分配过时间槽，不再分配
            cout << "🌶️ already in routing_table! src: " << src_id << ", dst: " << dst_id << endl;
            continue;
        }
        uint32_t src_toR = src_id / (params.k / 2);
        // uint32_t dst_toR = dst_id % (params.k / 2);
        // 更新流量信息
        // src_dst_data_size_table[{src_id,dst_id}] = size;
        // cout << "🐱 LocalArbiter " << this->id << " rts: src_id: " << src_id << " dst_id: " << dst_id << " size: " << size << endl;
        // cout << "🐱 LocalArbiter " << this->id << " rts: src_pod: " << src_pod << " dst_pod: " << dst_pod << " src_agg: " << src_agg << " dst_agg: " << dst_agg << " src_toR: " << src_toR << " dst_toR: " << dst_toR << endl;
        // cout << "🐱 LocalArbiter " << this->id << " rts: ToR2Agg: " << ToR2Agg[0][src_toR][src_agg] << " " << ToR2Agg[0][dst_toR][dst_agg] << " Agg2ToR: " << Agg2ToR[0][src_agg][src_toR] << " " << Agg2ToR[0][dst_agg][dst_toR] << endl;
        // cout << "🐱 LocalArbiter " << this->id << " rts: host_is_src: " << host_is_src[0][src_id] << " " << host_is_src[0][dst_id] << " host_is_dst: " << host_is
        // SCHD* schd = new SCHD();
        // double time_to_run = get_current_time() + (get_current_time() - packet->sending_time);
        // 确定给当前流分配的时间槽，暂时简化处理，后续可以计算最坏情况下的时间槽
        uint32_t Slot = ceil(get_current_time() / params.slot_length_in_s) + params.arbiter_lag;
        bool src_ToR_allocated = false;
        bool src_Agg_allocated = false;
        uint32_t cnt = 0;
        
        uint32_t agg_id = 0;
        while(cnt++ < params.T){ // slot一直增加，直到分配成功，或者超出时间槽范围
            // 分配host->ToR链路
            if(host_is_src[Slot % params.T][src_id % hosts_per_pod] == false){
                src_ToR_allocated = true;
            }
            // 分配ToR->Agg链路
            vector<uint32_t> k_2;
            for(uint32_t i = 0; i < params.k / 2; i++){
                k_2.push_back(i);
            }
            random_shuffle(k_2.begin(), k_2.end());
            
            for(int i = 0; i < params.k / 2; i++){
                agg_id = k_2[i] + params.k / 2 * this->id; // 需要加上bias
                if(ToR2Agg[Slot % params.T][src_toR % tors_per_pod][agg_id % aggs_per_pod] == false){
                    src_Agg_allocated = true;
                    break;
                }
            }
            // 如果都分配成功，退出循环
            if(src_ToR_allocated && src_Agg_allocated){
                host_is_src[Slot % params.T][src_id % hosts_per_pod] = true;
                ToR2Agg[Slot % params.T][src_toR % tors_per_pod][agg_id % aggs_per_pod] = true;
                cout << "🦄 LA " << this->id << " allocate a slot " << Slot << " for flow " << src_id << " -> " << dst_id << " @ " << get_current_time() << endl;
                break;
            }
            else{ // 如果分配失败，继续下一个slot, 还原标志位
                Slot++;
                src_ToR_allocated = false;
                src_Agg_allocated = false;
                continue;
            }
        }
        if(src_ToR_allocated && src_Agg_allocated){
            src_dst_slot_table[{src_id, dst_id}] = Slot;
            routing_table[{src_id, dst_id}]->slot = Slot;
            routing_table[{src_id, dst_id}]->src_host_id = src_id;
            routing_table[{src_id, dst_id}]->src_tor_id = src_toR;
            routing_table[{src_id, dst_id}]->src_agg_id = agg_id;
            routing_table[{src_id, dst_id}]->dst_host_id = dst_id;
            // cout << "🥚 LocalArbiter " << this->id << " allocate slot " << Slot << " for flow " << src_id << " -> " << dst_id << ", src_ToR is " << src_toR << ", src_Agg is " << agg_id << " @ " << get_current_time() << endl;
        }
        else{
            routing_table.erase({src_id, dst_id});
            src_dst_slot_table.erase({src_id, dst_id});
            continue; // 如果分配失败，继续下一个src-dst
        }

        ipr* ipr_info = new ipr(Slot, src_id, agg_id, dst_id);
        LocalArbiter *dst_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / hosts_per_pod];
        HeirScheduleIPRPkt *ipr_packet = new HeirScheduleIPRPkt(get_current_time(), this, dst_la, ipr_info);
        if (src_id / hosts_per_pod == dst_id / hosts_per_pod){
            allocate_downlink(ipr_packet);
        }
        else{
            send_request_to_la(dst_la, ipr_packet);
        }
        
        
        // cout << "😣 time_to_run: " << time_to_run << ", params.slot_length_in_s: " << params.slot_length_in_s << " slot: " << schd->slot << endl;
        // schd->src_host_id = src_id;
        // schd->dst_host_id = dst_id;
        // schd->src_tor_id = src_toR;
        // schd->dst_tor_id = dst_toR;
        // schd->src_agg_id = 0;
        // schd->dst_agg_id = 2;
        // schd->core_id = 0;
        // 输出schd信息
        // cout << "👿 LocalArbiter " << this->id << " send schd, size: " << sizeof(schd) << ", slot: " << schd->slot << ", src_host_id: " << schd->src_host_id << ", dst_host_id: " << schd->dst_host_id << ", src_tor_id: " << schd->src_tor_id << ", dst_tor_id: " << schd->dst_tor_id << ", src_agg_id: " << schd->src_agg_id << ", dst_agg_id: " << schd->dst_agg_id << ", core_id: " << schd->core_id << " @ " << get_current_time() << endl;
        // HeirScheduleSCHDPkt *schd_packet = new HeirScheduleSCHDPkt(get_current_time(), this, packet->src, schd);
        // cout << "p's address: " << schd_packet << endl;
        // SCHD* schd_packet_schd = schd_packet->schd;
        // cout << "🥭 schd's address: " << schd_packet_schd << endl;
        // cout << "🍠schd, slot: " << schd_packet_schd->slot << ", src_host_id: " << schd_packet_schd->src_host_id << ", dst_host_id: " << schd_packet_schd->dst_host_id << ", src_tor_id: " << schd_packet_schd->src_tor_id << ", dst_tor_id: " << schd_packet_schd->dst_tor_id << ", src_agg_id: " << schd_packet_schd->src_agg_id << ", dst_agg_id: " << schd_packet_schd->dst_agg_id << ", core_id: " << schd_packet_schd->core_id << endl;
        // add_to_event_queue(new PacketQueuingEvent(get_current_time(), schd_packet, toLCSQueues[(packet->src->id % hosts_per_pod) / (params.k/2)]));
        // cout << "🐱 LocalArbiter " << this->id << " send schd to Host " << packet->src->id << " @ " << get_current_time() << endl;
    }
    // if(src_dst_data_size_table.size() > 0){
    add_to_event_queue(new AllocateUplinkEvent(get_current_time() + params.slot_length_in_s, this));
    // }
    // add_to_event_queue(new AllocateUplinkEvent(get_current_time() + params.slot_length_in_s, this));
    // cout << "🐱 LocalArbiter " << this->id << " process rts from Host @ " << get_current_time() << endl;

}

void LocalArbiter::send_request_to_la(LocalArbiter *dst, HeirScheduleIPRPkt *ipr_packet){
    // cout << "🐇 LocalArbiter " << this->id << " send interpod request to LocalArbiter " << dst->id << " @ " << get_current_time() << endl;
    // HeirScheduleIPRPkt *ipr_packet = new HeirScheduleIPRPkt(get_current_time(), this, dst, ipr_info);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), ipr_packet, toGCSQueues[rand() % num_gcs])); // 简化处理：随机选择一个GCS发送
}

void LocalArbiter::receive_ipr(Packet *packet){
    HeirScheduleIPRPkt *ipr_packet = (HeirScheduleIPRPkt *)packet;
    // cout << "🐺 LocalArbiter " << this->id << " receive interpod request from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    allocate_downlink(ipr_packet);
}

void LocalArbiter::allocate_downlink(HeirScheduleIPRPkt *ipr_packet){
    ipr* ipr_info = ipr_packet->ipr_info;
    uint32_t Slot = ipr_info->slot;
    uint32_t src_id = ipr_info->src_host_id;
    uint32_t src_agg_id = ipr_info->src_agg_id;
    uint32_t dst_id = ipr_info->dst_host_id;
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / hosts_per_pod];


    bool dst_ToR_allocated = false;
    bool dst_Agg_allocated = false;
    uint32_t dst_agg_id = 0;

    // if(src_id / hosts_per_pod == dst_id / hosts_per_pod){ // 在同一个Pod内
    if(false){
        if(host_is_dst[Slot % params.T][dst_id % hosts_per_pod] == false){
            dst_agg_id = src_agg_id;
            if(Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] == false){
                // 分配成功
                dst_ToR_allocated = true;
                dst_Agg_allocated = true;
                host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = true;
                Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = true;
            }
        }
        if (dst_ToR_allocated && dst_Agg_allocated){

        }
        else{
            ipd* ipd_info = new ipd(Slot, src_id, src_agg_id, dst_id);
            HeirScheduleIPDPkt* ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, src_la, ipd_info);
            take_back_link(ipd_packet);
        }

    }
    else{
        if(host_is_dst[Slot % params.T][dst_id % hosts_per_pod] == false){
            // host_is_dst[Slot % params.T][dst_id] = true;
            // 分配Agg->ToR链路（选dst_agg)
            if(src_id / hosts_per_pod == dst_id / hosts_per_pod){ // 在同一个Pod内
                dst_agg_id = src_agg_id;
                if(Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] == false){
                    // 分配成功
                    dst_ToR_allocated = true;
                    dst_Agg_allocated = true;
                    host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = true;
                    Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = true;
                }
                else{
                    cout << "😰 Agg2ToR link not available" << endl;
                }
            }
            else{
                vector<uint32_t> k_2;
                for(uint32_t i = 0; i < params.k / 2; i++){
                    k_2.push_back(i);
                }
                random_shuffle(k_2.begin(), k_2.end());

                for(int i = 0; i < params.k / 2; i++){
                    dst_agg_id = k_2[i] + params.k / 2 * this->id; // 需要加上bias
                    if(Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] == false){
                        // 分配成功
                        dst_ToR_allocated = true;
                        dst_Agg_allocated = true;
                        host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = true;
                        Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = true;
                        break;
                    }
                }
            }
        }
        if (dst_ToR_allocated && dst_Agg_allocated){
            // cout << "🐣 LocalArbiter " << this->id << " allocate slot " << Slot << " for flow " << src_id << " -> " << dst_id << ", dst_Agg is " << dst_agg_id << ", dst_ToR is " << dst_tor_id << " @ " << get_current_time() << endl;
            ips* ips_info = new ips(Slot, src_id, src_agg_id, dst_id, dst_agg_id);
            HeirScheduleIPSPkt *ips_packet = new HeirScheduleIPSPkt(get_current_time(), this, src_la, ips_info);
            if(src_id / hosts_per_pod == dst_id / hosts_per_pod){
                update_routing_table(ips_packet);
            }
            else{
                send_ips_to_la(src_la, ips_packet);
            }
            core_rts* core_rts_info = new core_rts(Slot, src_id, src_agg_id, dst_id, dst_agg_id);
            HeirScheduleCoreRequestPkt* core_rts_packet = new HeirScheduleCoreRequestPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter, core_rts_info);
            send_request_to_ga(core_rts_packet);
        }
        else{
            cout << "❌ allocate downlink failed! host_is_dst: " << host_is_dst[Slot % params.T][dst_id % hosts_per_pod] << ", dst_ToR_allocated: " << dst_ToR_allocated << ", dst_Agg_allocated: " << dst_Agg_allocated << endl;
            ipd* ipd_info = new ipd(Slot, src_id, src_agg_id, dst_id);
            HeirScheduleIPDPkt* ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, src_la, ipd_info);
            if(src_id / hosts_per_pod == dst_id / hosts_per_pod){
                take_back_link(ipd_packet);
            }
            else{
                send_deny_to_la(src_la, ipd_packet);
            }
        }
    }
    
    delete ipr_packet;
}

void LocalArbiter::receive_ipd(Packet *packet){
    HeirScheduleIPDPkt *ipd_packet = (HeirScheduleIPDPkt *)packet;
    // cout << "🐹 LocalArbiter " << this->id << " receive interpod deny from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    take_back_link(ipd_packet);
    // delete ipd_packet;
}

void LocalArbiter::take_back_link(HeirScheduleIPDPkt *ipd_packet){
    ipd* ipd_info = ipd_packet->ipd_info;
    uint32_t Slot = ipd_info->slot;
    uint32_t src_id = ipd_info->src_host_id;
    uint32_t src_agg_id = ipd_info->src_agg_id;
    uint32_t dst_id = ipd_info->dst_host_id;
    uint32_t src_tor_id = src_id / (params.k / 2);
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / hosts_per_pod];
    // host->ToR
    assert(host_is_src[Slot % params.T][src_id % hosts_per_pod] == true);
    host_is_src[Slot % params.T][src_id % hosts_per_pod] = false;
    // ToR->Agg
    assert(ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] == true);
    ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] = false;

    // 删除src_dst_slot_table的src-dst表项
    src_dst_slot_table.erase({src_id, dst_id});
    // 删除routing_table的src-dst表项
    routing_table.erase({src_id, dst_id});
    delete ipd_packet;
}

void LocalArbiter::send_ips_to_la(LocalArbiter *src, HeirScheduleIPSPkt *ips_packet){
    // cout << "🐔 LocalArbiter " << this->id << " send interpod response to LocalArbiter " << src->id << " @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), ips_packet, toGCSQueues[rand() % num_gcs])); // 简化处理：随机选择一个GCS发送
}

void LocalArbiter::receive_ips(Packet *packet){
    HeirScheduleIPSPkt *ips_packet = (HeirScheduleIPSPkt *)packet;
    // cout << "🐶 LocalArbiter " << this->id << " receive interpod response from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    
    update_routing_table(ips_packet);
}

void LocalArbiter::update_routing_table(HeirScheduleIPSPkt *ips_packet){
    // 装填进路由表
    ips* ips_info = ips_packet->ips_info;
    uint32_t Slot = ips_info->slot;
    uint32_t src_id = ips_info->src_host_id;
    uint32_t src_agg_id = ips_info->src_agg_id;
    uint32_t dst_id = ips_info->dst_host_id;
    uint32_t dst_agg_id = ips_info->dst_agg_id;
    // assert(routing_table[{src_id, dst_id}] != nullptr && routing_table[{src_id, dst_id}]->slot == Slot);
    assert((routing_table[{src_id, dst_id}] != nullptr && routing_table[{src_id, dst_id}]->slot == Slot));
    routing_table[{src_id, dst_id}]->dst_agg_id = dst_agg_id;
    routing_table[{src_id, dst_id}]->dst_tor_id = dst_id / (params.k / 2);
    // cout << "🐣 LA " << this->id << " update routing table for flow " << src_id << " -> " << dst_id << ", src_tor: " << routing_table[{src_id, dst_id}]->src_tor_id << ", src_agg: " << routing_table[{src_id, dst_id}]->src_agg_id << ", dst_agg: " << routing_table[{src_id, dst_id}]->dst_agg_id << ", dst_tor: " << routing_table[{src_id, dst_id}]->dst_tor_id << " @ " << get_current_time() << endl;
    delete ips_packet;
}

void LocalArbiter::send_deny_to_la(LocalArbiter *src, HeirScheduleIPDPkt* ipd_packet){
    // cout << "🐵 LocalArbiter " << this->id << " send interpod deny to LocalArbiter " << src->id << " @ " << get_current_time() << endl;
    // HeirScheduleIPDPkt *ipd_packet = new HeirScheduleIPDPkt(get_current_time(), this, dst, ipr_info);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), ipd_packet, toGCSQueues[rand() % num_gcs])); 
}

void LocalArbiter::send_request_to_ga(HeirScheduleCoreRequestPkt *core_rts_packet){
    // cout << "🐹 LocalArbiter " << this->id << " send agg-agg request to GlobalArbiter @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), core_rts_packet, toGCSQueues[rand() % num_gcs]));
}

void LocalArbiter::receive_core_schd(Packet *packet){
    HeirScheduleCoreSCHDPkt *core_schd_packet = (HeirScheduleCoreSCHDPkt *)packet;
    // cout << "🐨 LocalArbiter " << this->id << " receive core schd from GlobalArbiter @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    generate_full_path(core_schd_packet);
}

void LocalArbiter::generate_full_path(HeirScheduleCoreSCHDPkt *core_schd_packet){
    core_schd* core_schd_info = core_schd_packet->core_schd_info;
    uint32_t Slot = core_schd_info->Slot;
    uint32_t src_id = core_schd_info->src_id;
    uint32_t src_agg_id = core_schd_info->src_agg_id;
    uint32_t core_id = core_schd_info->core_id;
    uint32_t dst_id = core_schd_info->dst_id;
    uint32_t dst_agg_id = core_schd_info->dst_agg_id;

    LocalArbiter *dst_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / hosts_per_pod];
    GlobalArbiter *ga = dynamic_cast<HeirScheduleTopology*>(topology)->global_arbiter;

    // 组装完整路径
    routing_table[{src_id, dst_id}]->core_id = core_id;
    cout << "🐥 LA " << this->id << " generate full path for flow " << src_id << " -> " << dst_id << ", slot: " << Slot << ", src_tor: " << routing_table[{src_id, dst_id}]->src_tor_id << ", src_agg: " << routing_table[{src_id, dst_id}]->src_agg_id << ", core: " << routing_table[{src_id, dst_id}]->core_id << ", dst_agg: " << routing_table[{src_id, dst_id}]->dst_agg_id << ", dst_tor: " << routing_table[{src_id, dst_id}]->dst_tor_id << " @ " << get_current_time() << endl;
    core_schd* core_schd_copy = new core_schd(Slot, src_id, src_agg_id, core_id, dst_id, dst_agg_id);
    delete core_schd_packet;
    SCHD* schd = new SCHD(routing_table[{src_id, dst_id}]);
    HeirScheduleSCHDPkt *schd_packet = new HeirScheduleSCHDPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->hosts[src_id], schd);
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), schd_packet, toLCSQueues[(src_id % hosts_per_pod) / (params.k/2)]));
    
    // 更新状态变量
    routing_table.erase({src_id, dst_id});
    src_dst_slot_table.erase({src_id, dst_id});
    // host_is_src[Slot % params.T][src_id % hosts_per_pod] = false;
    // dst_la->host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = false;
    // ToR2Agg[Slot % params.T][src_id / (params.k / 2) % tors_per_pod][src_agg_id % aggs_per_pod] = false;
    // dst_la->Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_id / (params.k / 2) % tors_per_pod] = false;
    // ga->CoreOccupationIn[Slot % params.T][core_id][src_agg_id] = false;
    // ga->CoreOccupationOut[Slot % params.T][core_id][dst_agg_id] = false;
    add_to_event_queue(new RestoreLinkEvent(Slot * params.slot_length_in_s, core_schd_copy));
    if(src_dst_data_size_table[{src_id, dst_id}] <= params.mss * params.slot_length){
        cout << "💊 LA " << this->id << " erase src_dst_data_size_table! src: " << src_id << ", dst: " << dst_id << ", size: " << src_dst_data_size_table[{src_id, dst_id}] << ", params.mss * params.slot_length: " << params.mss * params.slot_length << endl;
        for(auto it = src_dst_data_size_table.begin(); it != src_dst_data_size_table.end(); it++){
            cout << "🐱 src_dst_data_size_table: src: " << it->first.src << ", dst: " << it->first.dst << ", size: " << it->second << endl;
        }
        src_dst_data_size_table.erase({src_id, dst_id});
    }
    else{
        src_dst_data_size_table[{src_id, dst_id}] -= params.mss * params.slot_length;
        // allocate_uplink();
    }

}

void LocalArbiter::receive_core_deny(Packet *packet){
    // cout << "🐼 LocalArbiter " << this->id << " receive agg-agg deny from GlobalArbiter @ " << get_current_time() << endl;
    HeirScheduleCoreDenyPkt *core_deny_packet = (HeirScheduleCoreDenyPkt *)packet;
    take_back_link(core_deny_packet);
}

void LocalArbiter::take_back_link(HeirScheduleCoreDenyPkt *core_deny_packet){
    core_deny* core_deny_info = core_deny_packet->core_deny_info;
    uint32_t Slot = core_deny_info->Slot;
    uint32_t src_id = core_deny_info->src_id;
    uint32_t src_agg_id = core_deny_info->src_agg_id;
    uint32_t dst_id = core_deny_info->dst_id;
    uint32_t dst_agg_id = core_deny_info->dst_agg_id;
    uint32_t src_tor_id = src_id / (params.k / 2);
    uint32_t dst_tor_id = dst_id / (params.k / 2);
    bool is_src_la = core_deny_info->is_src_la;
    if (is_src_la){
        // host->ToR
        assert(host_is_src[Slot % params.T][src_id % hosts_per_pod] == true);
        host_is_src[Slot % params.T][src_id % hosts_per_pod] = false;
        // ToR->Agg
        assert(ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] == true);
        ToR2Agg[Slot % params.T][src_tor_id % tors_per_pod][src_agg_id % aggs_per_pod] = false;
    
        // 删除src_dst_slot_table的src-dst表项
        src_dst_slot_table.erase({src_id, dst_id});
        // 删除routing_table的src-dst表项
        routing_table.erase({src_id, dst_id});
    }
    else{
        // Agg->ToR
        assert(Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] == true);
        Agg2ToR[Slot % params.T][dst_agg_id % aggs_per_pod][dst_tor_id % tors_per_pod] = false;
        // ToR->host
        assert(host_is_dst[Slot % params.T][dst_id % hosts_per_pod] == true);
        host_is_dst[Slot % params.T][dst_id % hosts_per_pod] = false;
    }
    delete core_deny_packet;
}


// ------------------------------------------------- GlobalArbiter -------------------------------------------------
GlobalArbiter::GlobalArbiter(uint32_t id, double rate, uint32_t queue_type) : Host(id, 0, queue_type, GLOBAL_ARBITER) {
    this->type = GLOBAL_ARBITER;
    toGCSQueue = Factory::get_queue(0, rate, params.queue_size_ctrl, DCTCP_QUEUE, 0, GA_TO_GCS);
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
}

void GlobalArbiter::send_sync_message_to_la(){
    for (uint32_t i = 0; i < params.k / 2; i++){
    // for (uint32_t i = 0; i < 1; i++){
        SyncMessage *sync_packet = new SyncMessage(this, dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[i], get_current_time() + local_time_bias);
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), sync_packet, toGCSQueue));
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
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_response_packet, toGCSQueue));
}

void GlobalArbiter::receive_core_rts(Packet *packet){
    HeirScheduleCoreRequestPkt *core_rts_packet = (HeirScheduleCoreRequestPkt *)packet;
    // cout << "🐛 GlobalArbiter " << this->id << " receive core rts from LocalArbiter " << packet->src->id << " @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
    allocate_core_link(core_rts_packet);
}

void GlobalArbiter::allocate_core_link(HeirScheduleCoreRequestPkt *core_rts_packet){
    core_rts* core_rts_info = core_rts_packet->core_rts_info;
    uint32_t Slot = core_rts_info->Slot;
    uint32_t src_id = core_rts_info->src_id;
    uint32_t src_agg_id = core_rts_info->src_agg_id;
    uint32_t dst_id = core_rts_info->dst_id;
    uint32_t dst_agg_id = core_rts_info->dst_agg_id;
    
    uint32_t core_id = dynamic_cast<HeirScheduleTopology*>(topology)->src_dst_agg_to_core_map[{src_agg_id, dst_agg_id}];
    if(CoreOccupationIn[Slot % params.T][core_id][src_agg_id] == false && CoreOccupationOut[Slot % params.T][core_id][dst_agg_id] == false){
        CoreOccupationIn[Slot % params.T][core_id][src_agg_id] = true;
        CoreOccupationOut[Slot % params.T][core_id][dst_agg_id] = true;
        core_schd* core_schd_info = new core_schd(Slot, src_id, src_agg_id, core_id, dst_id, dst_agg_id);
        LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / (params.k * params.k / 4)];
        HeirScheduleCoreSCHDPkt* core_schd_packet = new HeirScheduleCoreSCHDPkt(get_current_time(), this, src_la, core_schd_info);
        send_core_schd_to_la(core_schd_packet);
    }
    else{
        core_deny* core_deny_info_src = new core_deny(Slot, src_id, src_agg_id, dst_id, dst_agg_id);
        LocalArbiter *src_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[src_id / (params.k * params.k / 4)];
        core_deny_info_src->is_src_la = true;
        HeirScheduleCoreDenyPkt* core_deny_packet_src = new HeirScheduleCoreDenyPkt(get_current_time(), this, src_la, core_deny_info_src);
        send_core_deny_to_la(core_deny_packet_src);

        core_deny* core_deny_info_dst = new core_deny(Slot, src_id, src_agg_id, dst_id, dst_agg_id);
        LocalArbiter *dst_la = dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[dst_id / (params.k * params.k / 4)];
        core_deny_info_dst->is_src_la = false;
        HeirScheduleCoreDenyPkt* core_deny_packet_dst = new HeirScheduleCoreDenyPkt(get_current_time(), this, dst_la, core_deny_info_dst);
        send_core_deny_to_la(core_deny_packet_dst);
    }

}

void GlobalArbiter::send_core_schd_to_la(HeirScheduleCoreSCHDPkt *core_schd_packet){
    // cout << "🐔 GlobalArbiter " << this->id << " send core schd to LocalArbiter " << core_schd_packet->dst->id << " @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), core_schd_packet, toGCSQueue));
}

void GlobalArbiter::send_core_deny_to_la(HeirScheduleCoreDenyPkt *core_deny_packet){
    // cout << "🦄 GlobalArbiter " << this->id << " send core deny to LocalArbiter " << core_deny_packet->dst->id << " @ " << get_current_time() << endl;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), core_deny_packet, toGCSQueue));
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
