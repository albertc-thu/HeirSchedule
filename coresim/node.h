#ifndef NODE_H
#define NODE_H

#include <vector>
#include <queue>
#include <map>
#include <set>
#include "queue.h"
#include "packet.h"


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

class Packet;
class Flow;
class rts;

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

        // 当前正在接收的流表
        set<Flow *> receiving_flows;
        uint32_t max_out_of_order_buffer = 0;
        uint32_t max_flow_num = 0;
};

class HeirScheduleHost : public Host{
    public:
        HeirScheduleHost(uint32_t id, double r1, double r2, uint32_t queue_type);
        void receive(Packet *packet);
        void receive_sync_message(Packet *packet);
        void receive_delay_response_message(Packet *packet);
        
        
        void host_send_rts();
        void host_recv_schd();
        void host_send_data(double time);
        
        Queue *toToRQueue;
        Queue *toLAQueue; //前往local arbiter的队列
        // int host_type;
        // long long int received_bytes_all;
        // double received_first_packet_time;
        // double received_last_packet_time;

        // 当前正在发送的流表
        set<Flow *> sending_flows;
        // 当前正在接收的流表
        set<Flow *> receiving_flows;
        uint32_t max_out_of_order_buffer = 0;
        uint32_t max_flow_num = 0;
        
        // typedef vector<Queue *> priority_queue;
        // map<uint32_t, priority_queue> hosts_queue; // 发送数据的队列
        vector<vector<deque <flow_data_at_src>>> per_dst_queues; // 发送数据的时候，将数据从 per-dst queue 取出，转移到 queue 中

        vector<dst_remaining> remainings;

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

    void receive_rts(Packet *packet);
    
    // vector<Queue *> queues; 
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
};

class GlobalArbiter : public Host {
    public:
        GlobalArbiter(uint32_t id, double rate, uint32_t type);
        void SendSyncMessageToLA();
        void receive(Packet *packet);
        void receive_delay_request_message(Packet *packet);
        Queue *toGCSQueue;
        // void recv_agg_agg_rts(); // 从LA接收agg-agg请求
        // void process_agg_agg_rts(); // 处理agg-agg请求
        // void send_agg_agg_schd(); // 向LA发送agg-agg结果
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
