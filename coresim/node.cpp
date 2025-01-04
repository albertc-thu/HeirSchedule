#include "packet.h"
#include "flow.h"

#include "../ext/factory.h"
#include "../run/params.h"
#include "topology.h"
#include <cassert>
#include <random>

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
    toLAQueue = Factory::get_queue(0, rate_control, params.queue_size_ctrl, queue_type, 0, HOST_TO_LCS);
    // this->host_type = HeirSchedule_HOST;
    this->received_bytes_all = 0;
    this->received_first_packet_time = -1;
    this->received_last_packet_time = -1;

    // 初始化优先级队列
    uint32_t port_num = params.num_of_ports;
    uint32_t server_num = port_num * port_num * port_num / 4;

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
    cout << "🤖 HeirScheduleHost " << this->id << " receive packet @ " << get_current_time() << endl;
    switch (packet->type)
    {
    case SYNC_MSG:
        receive_sync_message(packet);
        break;
    case DELAY_RES_MSG:
        receive_delay_response_message(packet);
        break;
    default:
        break;
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

// void HeirScheduleHost::host_send_rts(){
//     vector<rts> rts_vector;
//     for (auto it = this->sending_flows.begin(); it != this->sending_flows.end(); it++){
//         Flow *f = *it;
//         struct rts r;
//         r.src_id = f->src->id;
//         r.dst_id = f->dst->id;
//         r.size = f->size;
//         rts_vector.push_back(r);
//     }

//     HeirScheduleRTSPkt *rts_packet = new HeirScheduleRTSPkt(get_current_time(), this, dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[this->id / (params.k * params.k / 4)], rts_vector);
    
//     // 发送RTS
//     add_to_event_queue(new PacketQueuingEvent(get_current_time(), rts_packet, toLAQueue));
// }


LocalArbiter::LocalArbiter(uint32_t id, double rate, uint32_t num_gcs, uint32_t queue_type) : Host(id, 0, queue_type, LOCAL_ARBITER) {
    this->type = LOCAL_ARBITER;
    this->num_gcs = num_gcs;
    for (uint32_t i = 0; i < params.k/2; i++) {
        toLCSQueues.push_back(Factory::get_queue(i, rate, params.queue_size_ctrl, queue_type, 0, LA_TO_LCS));
    }
    for(uint32_t i = 0; i < num_gcs; i++){
        toGCSQueues.push_back(Factory::get_queue(i, rate, params.queue_size_ctrl, queue_type, 0, LA_TO_GCS));
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
    for (uint32_t i = 0; i < params.k/2 * params.k/2; i++){
        uint32_t host_id = this->id * params.k/2 * params.k/2 + i;
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
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), delay_response_packet, toLCSQueues[(packet->src->id % ((params.k/2) * (params.k/2) )) / (params.k/2)]));    
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

// void LocalArbiter::receive_rts(Packet* packet){
//     HeirScheduleRTSPkt *rts_packet = (HeirScheduleRTSPkt *)packet;
//     cout << "💻 LocalArbiter " << this->id << " receive rts from Host @ " << get_current_time() << ", packet delay: " << get_current_time() - packet->sending_time << endl;
// }

GlobalArbiter::GlobalArbiter(uint32_t id, double rate, uint32_t queue_type) : Host(id, 0, queue_type, GLOBAL_ARBITER) {
    this->type = GLOBAL_ARBITER;
    toGCSQueue = Factory::get_queue(0, rate, params.queue_size_ctrl, queue_type, 0, GA_TO_GCS);
    this->local_time_bias = 0.0;
}

void GlobalArbiter::SendSyncMessageToLA(){
    for (uint32_t i = 0; i < params.k; i++){
    // for (uint32_t i = 0; i < 1; i++){
        SyncMessage *sync_packet = new SyncMessage(this, dynamic_cast<HeirScheduleTopology*>(topology)->local_arbiters[i], get_current_time() + local_time_bias);
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), sync_packet, toGCSQueue));
        cout << "🧠 GlobalArbiter " << this->id << " send sync message to LocalArbiter " << i << endl;
    }
}

void GlobalArbiter::receive(Packet *packet) {
    // TODO: implement
    cout << "🧠 GlobalArbiter " << this->id << " receive packet" << endl;
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
