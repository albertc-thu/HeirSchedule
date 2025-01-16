#ifndef NODE_H
#define NODE_H

#include <vector>
#include <queue>
#include <map>
#include <set>
#include "queue.h"
#include "packet.h"
#include "../run/params.h"


#define HOST 0
#define SWITCH 1
#define HeirSchedule_HOST 2
#define LOCAL_ARBITER 3
#define GLOBAL_ARBITER 4

#define CORE_SWITCH 10
#define AGG_SWITCH 11
#define TOR_SWITCH 12
#define LOCAL_CONTROL_SWITCH 13
#define GLOBAL_CONTROL_SWITCH 14
#define S3_SWITCH 15

#define CPU 0
#define MEM 1
#define DISK 2

#define HOST_TO_TOR 0
#define TOR_TO_AGG 1
#define AGG_TO_CORE 2
#define CORE_TO_AGG 3
#define AGG_TO_TOR 4
#define TOR_TO_HOST 5

#define HOST_TO_LCS 10
#define LCS_TO_LA 11
#define LA_TO_GCS 12
#define GCS_TO_LA 13
#define GCS_TO_GA 14
#define GA_TO_GCS 15
#define LA_TO_LCS 16
#define LCS_TO_HOST 17


using namespace std;
extern DCExpParams params;


class Packet;
class HeirScheduleDataPkt;
class HeirScheduleRTSPkt;
class HeirScheduleIPRPkt;
class HeirScheduleIPSPkt;
class HeirScheduleIPDPkt;
class HeirScheduleCoreRequestPkt;
class HeirScheduleCoreSCHDPkt;
class HeirScheduleCoreDenyPkt;
class Flow;
class rts;
class SCHD;
class ipr;
class ips;

struct dst_remaining
{
    uint32_t dst_id;
    uint32_t remaining_packets;
};

struct flow_data_at_src {
    uint32_t remaining_size;
    Flow* flow;
    uint32_t priority;
};

struct host_has_flow
{
    Flow* flow;
    uint32_t remaining_packets;
};

#include <unordered_map>

struct src_dst_pair {
    uint32_t src;
    uint32_t dst;

    bool operator==(const src_dst_pair &other) const {
        return src == other.src && dst == other.dst;
    }
};

namespace std {
    template <>
    struct hash<src_dst_pair> {
        std::size_t operator()(const src_dst_pair &k) const {
            return ((std::hash<uint32_t>()(k.src) ^ (std::hash<uint32_t>()(k.dst) << 1)) >> 1);
        }
    };
}

class FlowComparator{
    public:
        bool operator() (Flow *a, Flow *b);
};


class Node {
    public:
        Node(uint32_t id, uint32_t type);
        virtual ~Node() {} // Add a virtual destructor to make the class polymorphic
        uint32_t id;
        uint32_t type;
        double local_time_bias;
};

class Host : public Node {
    public:
        Host(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type);
        virtual ~Host() {} // Add a virtual destructor to make the class polymorphic
        
        Queue *queue;
        int host_type;
        long long int received_bytes_all;
        double received_first_packet_time;
        double received_last_packet_time;


        set<Flow *> sending_flows;
        set<Flow *> receiving_flows;
        set<Flow*> now_receiving;
        uint32_t max_out_of_order_buffer = 0;
        uint32_t max_flow_num = 0;
        vector<vector<deque <flow_data_at_src>>> per_dst_queues; // 发送数据的时候，将数据从 per-dst queue 取出，转移到 queue 中

        vector<dst_remaining> remainings;
};

class HeirScheduleHost : public Host{
    public:
        HeirScheduleHost(uint32_t id, double r1, double r2, uint32_t queue_type);
        void receive(Packet *packet);
        void receive_sync_message(Packet *packet);
        void receive_delay_response_message(Packet *packet);
        
        
        void host_send_rts();
        void receive_schd_and_send_data(Packet *packet);
        void receive_data_packet(Packet *packet);

        HeirScheduleDataPkt* get_data_packet(uint32_t dst_id);
        
        Queue *toToRQueue;
        Queue *toLAQueue; //前往local arbiter的队列
        // int host_type;
        // long long int received_bytes_all;
        // double received_first_packet_time;
        // double received_last_packet_time;

        // 当前正在发送的流表
        // set<Flow *> sending_flows;
        // 当前正在接收的流表
        // set<Flow *> receiving_flows;
        // uint32_t max_out_of_order_buffer = 0;
        // uint32_t max_flow_num = 0;
        
        // typedef vector<Queue *> priority_queue;
        // map<uint32_t, priority_queue> hosts_queue; // 发送数据的队列
        

        map<uint32_t, host_has_flow> flows_to_send; // flow_id, flow
        double T3_time; // 需要记录T3
        double master_slave_diff;
        double slave_master_diff;

};

class LocalArbiter : public Host {
public:
    LocalArbiter(uint32_t id, double rate, uint32_t num_gcs, uint32_t queue_type);
    void receive(Packet *packet);
    // 与GA时间同步
    void receive_sync_message(Packet *packet);
    void receive_delay_response_message(Packet *packet);

    // 与Host时间同步
    void send_sync_message_to_host();
    void receive_delay_request_message_from_host(Packet *packet);

    // 路由
    void receive_rts(Packet *packet);
    void allocate_uplink();
    void send_request_to_la(LocalArbiter *dst, HeirScheduleIPRPkt *ipr_packet);
    void receive_ipr(Packet *packet);
    void allocate_downlink(HeirScheduleIPRPkt *ipr_packet);
    void send_ips_to_la(LocalArbiter *src, HeirScheduleIPSPkt *ips_packet);
    void send_deny_to_la(LocalArbiter *src, HeirScheduleIPDPkt* ipd_packet);
    void receive_ipd(Packet *packet);
    void take_back_link(HeirScheduleIPDPkt* ipd_packet);
    void receive_ips(Packet *packet);
    void update_routing_table(HeirScheduleIPSPkt *ips_packet);
    
    void send_request_to_ga(HeirScheduleCoreRequestPkt *core_rts_packet);
    void receive_core_schd(Packet *packet);
    void generate_full_path(HeirScheduleCoreSCHDPkt *core_schd_packet);
    void receive_core_deny(Packet *packet);
    void take_back_link(HeirScheduleCoreDenyPkt *core_deny_packet);
    
    // vector<Queue *> queues; 
    uint32_t hosts_per_pod = params.k * params.k / 4;
    uint32_t tors_per_pod = params.k / 2;
    uint32_t aggs_per_pod = params.k / 2;
    uint32_t num_gcs;
    vector<Queue *> toLCSQueues; //前往host的队列
    vector<Queue *> toGCSQueues; //前往其他Arbiter(LA and GA)的队列

    // void process_rts(); //从host接收请求
    // void send_interpod_rts(double time); // 向其他LA发送interpod请求
    // void recv_interpod_rts(); // 从其他LA接收interpod请求
    // void process_interpod_rts(); // 处理interpod请求 
    // void send_interpod_schd(); // 向其他LA发送interpod中间结果
    // void recv_interpod_schd(); // 从其他LA接收interpod中间结果
    // void send_agg_agg_rts();   // 向GA发送agg-agg请求
    // void recv_agg_core_agg_schd();  // 从GA接收agg-core-agg结果
    // void glue_everyting(); // 将所有的中间结果合并为最终结果
    // void send_final_results(); // 向host发送最终结果

    vector<rts> received_rts;
    
    double T3_time; // 需要记录T3
    double master_slave_diff;
    double slave_master_diff;


    // 路由相关
    // unordered_map<uint32_t, unordered_map<uint32_t, uint32_t>> src_dst_data_size_table; // 记录每个源-目的对应的数据总量
    // unordered_map<uint32_t, unordered_map<uint32_t, uint32_t>> src_dst_slot_table; // 记录每个源-目的对应的时间槽
    vector<vector<bool>> host_is_src; // T * k^2/4, hostIsSrc[t][i]表示第t个时隙，第i个Host是否是源节点 
    vector<vector<bool>> host_is_dst; // T * k^2/4, hostIsDst[t][i]表示第t个时隙，第i个Host是否是目的节点
    vector<vector<vector<bool>>> ToR2Agg; // 一个$T * \frac{k}{2} * \frac{k}{2}$的矩阵, ToR2Agg[t][i][j]表示第t个时隙，ToR i->Agg j的链路是否被分配
    vector<vector<vector<bool>>> Agg2ToR; // 一个$T * \frac{k}{2} * \frac{k}{2}$的矩阵, Agg2ToR[t][i][j]表示第t个时隙，Agg i->ToR j的链路是否被分配
    unordered_map<src_dst_pair, uint32_t> src_dst_data_size_table; // 记录每个源-目的对应的数据总量
    unordered_map<src_dst_pair, uint32_t> src_dst_slot_table; // 记录每个源-目的对应的时间槽
    unordered_map<src_dst_pair, SCHD*> routing_table; // 记录每个源-目的对应的调度信息
};

class GlobalArbiter : public Host {
    public:
        GlobalArbiter(uint32_t id, double rate, uint32_t type);
        void send_sync_message_to_la();
        void receive(Packet *packet);
        void receive_delay_request_message(Packet *packet);

        void receive_core_rts(Packet *packet);
        void allocate_core_link(HeirScheduleCoreRequestPkt *core_rts_packet);
        void send_core_schd_to_la(HeirScheduleCoreSCHDPkt *core_schd_packet);
        void send_core_deny_to_la(HeirScheduleCoreDenyPkt *core_deny_packet);
        Queue *toGCSQueue;
        // void recv_agg_agg_rts(); // 从LA接收agg-agg请求
        // void process_agg_agg_rts(); // 处理agg-agg请求
        // void send_agg_agg_schd(); // 向LA发送agg-agg结果

        //- CoreOccupationIn: 一个$T * \frac{k^2}{4} * {k}$的矩阵，CoreOccupationIn[t][i][p]表示第t个时隙，Core i的入端口p是否被分配
        vector<vector<vector<bool>>> CoreOccupationIn;
        //- CoreOccupationOut: 一个$T * \frac{k^2}{4} * {k}$的矩阵，CoreOccupationOut[t][i][p]表示第t个时隙，Core i的出端口p是否被分配
        vector<vector<vector<bool>>> CoreOccupationOut;
};

class Switch : public Node {
    public:
        Switch(uint32_t id, uint32_t switch_type);
        uint32_t switch_type;
        vector<Queue *> queues;
};

class CoreSwitch : public Switch {
    public:
        //All queues have same rate
        
        CoreSwitch(uint32_t id, uint32_t numQueue, double rate, uint32_t queue_type);
        vector<Queue *> toAggQueues;
};

class AggSwitch : public Switch {
    public:
        // Different Rates
        AggSwitch(uint32_t id, uint32_t numOfQToToR, double r1, uint32_t numOfQToCore, double r2, uint32_t queue_type);
        vector<Queue *> toToRQueues;
        vector<Queue *> toCoreQueues;
};

class ToRSwitch : public Switch {
    public:
        // Different Rates
        ToRSwitch(uint32_t id, uint32_t numOfQToHost, double r1, uint32_t numOfQToAgg, double r2, uint32_t queue_type);
        vector<Queue *> toAggQueues;
        vector<Queue *> toHostQueues;
};

class LocalControlSwitch : public Switch {
public:
    LocalControlSwitch(uint32_t id, uint32_t numOfQToHost, double r1, uint32_t numOfQToLA, double r2, uint32_t queue_type);
    vector<Queue *> toHostQueues;
    Queue *toLAQueue;
};

class GlobalControlSwitch : public Switch {
    public:
        GlobalControlSwitch(uint32_t id, uint32_t numOfQToLA, double r1, uint32_t numOfQToGA, double r2, uint32_t queue_type);
        vector<Queue *> toLAQueues;
        Queue *toGAQueue;
};

#endif
