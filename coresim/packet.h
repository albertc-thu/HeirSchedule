#ifndef PACKET_H
#define PACKET_H

#include "flow.h"
#include "node.h"
#include <stdint.h>
#include <map>
using namespace std;
// TODO: Change to Enum
#define NORMAL_PACKET 0
#define ACK_PACKET 1

#define RTS_PACKET 3
#define CTS_PACKET 4
#define OFFER_PACKET 5
#define DECISION_PACKET 6
#define CAPABILITY_PACKET 7
#define STATUS_PACKET 8
#define FASTPASS_RTS 9
#define FASTPASS_SCHEDULE 10

#define HeirScheduleData 20
#define HeirScheduleRTS 21
#define HeirScheduleSCHD 22
#define HeirScheduleIPR 23
#define HeirScheduleIPS 24
#define HeirScheduleIPD 25
#define CORE_RTS 26
#define CORE_SCHD 27
#define CORE_DENY 28

#define SYNC_MSG 30
#define DELAY_REQ_MSG 31
#define DELAY_RES_MSG 32

class FastpassEpochSchedule;
class Flow;
class Host;

/**
 * use to indicate which node the packet will pass
*/
class SCHD
{
public:
    //host id
    uint32_t slot;
    uint32_t src_host_id;
    uint32_t src_tor_id;
    uint32_t src_agg_id;
    uint32_t core_id;
    uint32_t dst_agg_id;
    uint32_t dst_tor_id;
    uint32_t dst_host_id;
    SCHD(){}
    SCHD(uint32_t slot, uint32_t src_host_id, uint32_t src_tor_id, uint32_t src_agg_id, uint32_t core_id, uint32_t dst_agg_id, uint32_t dst_tor_id, uint32_t dst_host_id)
    {
        this->slot = slot;
        this->src_host_id = src_host_id;
        this->src_tor_id = src_tor_id;
        this->src_agg_id = src_agg_id;
        this->core_id = core_id;
        this->dst_agg_id = dst_agg_id;
        this->dst_tor_id = dst_tor_id;
        this->dst_host_id = dst_host_id;
    }
    SCHD(SCHD* _schd){
        this->slot = _schd->slot;
        this->src_host_id = _schd->src_host_id;
        this->src_tor_id = _schd->src_tor_id;
        this->src_agg_id = _schd->src_agg_id;
        this->core_id = _schd->core_id;
        this->dst_agg_id = _schd->dst_agg_id;
        this->dst_tor_id = _schd->dst_tor_id;
        this->dst_host_id = _schd->dst_host_id;
    }
    // Host *host;
};

struct rts
{
    // the rts size
    uint32_t size;
    // the host id
    uint32_t src_id;
    uint32_t dst_id;
};

class ipr // inter-pod request
{
public:
    uint32_t slot;
    uint32_t src_host_id;
    uint32_t src_agg_id;
    uint32_t dst_host_id;
    ipr(){}
    ipr(uint32_t slot, uint32_t src_host_id, uint32_t src_agg_id, uint32_t dst_host_id)
    {
        this->slot = slot;
        this->src_host_id = src_host_id;
        this->src_agg_id = src_agg_id;
        this->dst_host_id = dst_host_id;
    }
};

class ips // inter-pod schedule
{
public:
    uint32_t slot;
    uint32_t src_host_id;
    uint32_t src_agg_id;
    uint32_t dst_agg_id;
    uint32_t dst_host_id;
    ips(){}
    ips(uint32_t slot, uint32_t src_host_id, uint32_t src_agg_id, uint32_t dst_host_id, uint32_t dst_agg_id)
    {
        this->slot = slot;
        this->src_host_id = src_host_id;
        this->src_agg_id = src_agg_id;
        this->dst_host_id = dst_host_id;
        this->dst_agg_id = dst_agg_id;
    }
};

class ipd // inter-pod deny
{
public:
    uint32_t slot;
    uint32_t src_host_id;
    uint32_t src_agg_id;
    uint32_t dst_host_id;
    ipd(){}
    ipd(uint32_t slot, uint32_t src_host_id, uint32_t src_agg_id, uint32_t dst_host_id)
    {
        this->slot = slot;
        this->src_host_id = src_host_id;
        this->src_agg_id = src_agg_id;
        this->dst_host_id = dst_host_id;
    }
};

class core_rts // agg-agg request
{
public:
    uint32_t Slot;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t src_agg_id;
    uint32_t dst_agg_id;
    core_rts(){}
    core_rts(uint32_t Slot, uint32_t src_id, uint32_t src_agg_id, uint32_t dst_id, uint32_t dst_agg_id)
    {
        this->Slot = Slot;
        this->src_id = src_id;
        this->src_agg_id = src_agg_id;
        this->dst_id = dst_id;
        this->dst_agg_id = dst_agg_id;
    }
    
};

class core_schd // agg-agg schedule
{
public:
    uint32_t Slot;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t src_agg_id;
    uint32_t core_id;
    uint32_t dst_agg_id;
    core_schd(){}
    core_schd(uint32_t Slot, uint32_t src_id, uint32_t src_agg_id, uint32_t core_id, uint32_t dst_id, uint32_t dst_agg_id)
    {
        this->Slot = Slot;
        this->src_id = src_id;
        this->src_agg_id = src_agg_id;
        this->core_id = core_id;
        this->dst_id = dst_id;
        this->dst_agg_id = dst_agg_id;
    }
};

class core_deny // agg-agg deny
{
public:
    uint32_t Slot;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t src_agg_id;
    uint32_t dst_agg_id;
    core_deny(){}
    core_deny(uint32_t Slot, uint32_t src_id, uint32_t src_agg_id, uint32_t dst_id, uint32_t dst_agg_id)
    {
        this->Slot = Slot;
        this->src_id = src_id;
        this->src_agg_id = src_agg_id;
        this->dst_id = dst_id;
        this->dst_agg_id = dst_agg_id;
    }
};

class Packet {

    public:
        Packet(double sending_time, Flow *flow, uint32_t seq_no, uint32_t pf_priority,
                uint32_t size, Host *src, Host *dst);
        virtual ~Packet();
        double sending_time;
        Flow *flow;
        uint32_t seq_no;
        uint32_t pf_priority;
        uint32_t size;
        uint32_t hdr_size;
        Host *src;
        Host *dst;
        uint32_t unique_id;
        static uint32_t instance_count;
        int remaining_pkts_in_batch;
        int capability_seq_num_in_data;

        uint32_t type; // Normal or Ack packet
        double total_queuing_delay;
        double last_enque_time;

        int capa_data_seq;

        double release_time;
        double arrive_time;

        map<Flow*, vector<uint32_t>> inside_seqs;
        map<Flow*, vector<uint32_t>> inside_bytes;
        bool scheduled_flag; // 显示该包是否被调度
        bool dropped_flag; // 显示该包是否被丢弃

        SCHD* path;

        static int new_num;
        static int delete_num;
};

class PlainAck : public Packet {
    public:
        PlainAck(Flow *flow, uint32_t seq_no_acked, uint32_t size, Host* src, Host* dst);
};

class Ack : public Packet {
    public:
        Ack(Flow *flow, uint32_t seq_no_acked, std::vector<uint32_t> sack_list,
                uint32_t size,
                Host* src, Host *dst);
        uint32_t sack_bytes;
        std::vector<uint32_t> sack_list;
};

class RTSCTS : public Packet {
    public:
        //type: true if RTS, false if CTS
        RTSCTS(bool type, double sending_time, Flow *f, uint32_t size, Host *src, Host *dst);
};

class RTS : public Packet{
    public:
        RTS(Flow *flow, Host *src, Host *dst, double delay, int iter);
        double delay;
        int iter;
};

class OfferPkt : public Packet{
    public:
        OfferPkt(Flow *flow, Host *src, Host *dst, bool is_free, int iter);
        bool is_free;
        int iter;
};

class DecisionPkt : public Packet{
    public:
        DecisionPkt(Flow *flow, Host *src, Host *dst, bool accept);
        bool accept;
};

class CTS : public Packet{
    public:
        CTS(Flow *flow, Host *src, Host *dst);
};

class CapabilityPkt : public Packet{
    public:
        CapabilityPkt(Flow *flow, Host *src, Host *dst, double ttl, int remaining, int cap_seq_num, int data_seq_num);
        double ttl;
        int remaining_sz;
        int cap_seq_num;
        int data_seq_num;
};

class StatusPkt : public Packet{
    public:
        StatusPkt(Flow *flow, Host *src, Host *dst, int num_flows_at_sender);
        double ttl;
        bool num_flows_at_sender;
};


class FastpassRTS : public Packet
{
    public:
        FastpassRTS(Flow *flow, Host *src, Host *dst, int remaining_pkt);
        int remaining_num_pkts;
};

class FastpassSchedulePkt : public Packet
{
    public:
        FastpassSchedulePkt(Flow *flow, Host *src, Host *dst, FastpassEpochSchedule* schd);
        FastpassEpochSchedule* schedule;
};

class SyncMessage : public Packet
{
    public:
        SyncMessage(Host *src, Host *dst, double time);
        double T1_time;
        double Enqueue_time;
        double Dequeue_time;
        double innetwork_delay = 0.0;
};

class DelayRequestMessage : public Packet
{
    public:
        DelayRequestMessage(Host *src, Host *dst);
        double Enqueue_time;
        double Dequeue_time;
        double innetwork_delay = 0.0;
};

class DelayResponseMessage : public Packet
{
    public:
        DelayResponseMessage(Host *src, Host *dst, double time);
        double T4_time;
};

class HeirScheduleDataPkt : public Packet
{
    public:
        HeirScheduleDataPkt(Host *src, Host *dst, uint32_t payload_size, uint32_t header_size, vector<Flow*> flows, 
                vector<uint32_t> flow_segment_sizes, vector<uint32_t> flow_segment_begin_seq_no);
        ~HeirScheduleDataPkt();

        static int new_num;
        static int delete_num;

        vector<Flow*> flows;
        vector<uint32_t> flow_segment_sizes;
        vector<uint32_t> flow_segment_begin_seq_no;
};


class HeirScheduleRTSPkt : public Packet
{
public:
    /**
    * replace the params.hrs_size with the RTS packet size
    * \param flow_dst the dst node to transmit the data
    * \param total_sz_inque the total size in the queue destinate for the flow_dst
    */
    HeirScheduleRTSPkt(double sending_time, Host *src, Host *dst, std::vector<struct rts> rts_vector);
    // HeirScheduleRTSPkt(double sending_time, Host *src, Host *dst,
    //                std::vector<struct rts> rts_vector, std::vector<uint32_t> available_pods);
    ~HeirScheduleRTSPkt();
    // WhateverRTSPacket(double sending_time, Host *src, Host *dst, Host *flow_dst, int total_sz_inque);
    // ~WhateverRTSPacket();

    static int new_num;
    static int delete_num;

    std::map<uint32_t, uint32_t> smallest_dst_flowsize;
    // std::map<uint32_t, uint32_t> e2p_smallest_dst_flowsize;
    // std::map<uint32_t, uint32_t> c2e_smallest_dst_flowsize;

    // flag = 0 --> end flag = 1 --> core
    // uint32_t flag = 0;
    /**
     * src,dst,type in packet
    */
    std::vector<rts> rts_vector;
    // only core switch has this attribute
    // std::vector<uint32_t> available_pods;

    
};

class HeirScheduleSCHDPkt : public Packet
{
public:
    /**
     * Total information is about 10 Bytes
     * \param sending_time  time when the schedule packet is sent
     * \param offset time when packet begins to be sent = sending_time + offset
     * \param rts_offset timeToSendRTS = sending_time + offset + rts_offset
     * \param path choose the S or L switch
     *        If the path.flag == 1 ----> means inside the pod
     *        If the path.flag == 2 ----> means E-->C find the flow_dst_id in 
     * \param flow_dst_id allow the corresponding queue to transmit packets
    */
    HeirScheduleSCHDPkt(double sending_time, Host *src, Host *dst,
                        SCHD* schd);
    ~HeirScheduleSCHDPkt();

    //flag = 0 means the dst is host flag = 1 means the dst are pods
    // uint32_t flag;
    // double offset;
    // uint32_t rts_offset;
    // bool if_rts;

    // use to pointout the sending path
    // end_host_id指的是数据包最终的目的
    SCHD* schd;
    // uint32_t endhost_id;
    // // std::vector<pod_schedule> pods_schedule;
    // bool dummy_flag;

    

    // start of a slot. this schd pkt will be executed at this slot.
    // double time_to_run;
    static int new_num;
    static int delete_num;
};


class HeirScheduleIPRPkt : public Packet // IPR: Inter-pod Request 用于LA之间交换Inter-pod需求
{
public:
    HeirScheduleIPRPkt(double sending_time, Host *src, Host *dst, struct ipr* ipr);
    ~HeirScheduleIPRPkt();

    ipr* ipr_info;
    static int new_num;
    static int delete_num;

};

class HeirScheduleIPSPkt : public Packet // IPS: Inter-pod Schedule 用于LA之间传递中间结果
{
public:
    HeirScheduleIPSPkt(double sending_time, Host *src, Host *dst, ips* ips_info);
    ~HeirScheduleIPSPkt();

    ips* ips_info;
    static int new_num;
    static int delete_num;
};

class HeirScheduleIPDPkt : public Packet // IPD: Inter-pod Deny 用于LA之间传递拒绝信息
{
public:
    HeirScheduleIPDPkt(double sending_time, Host *src, Host *dst, ipd* ipd_info);
    ~HeirScheduleIPDPkt();

    ipd* ipd_info;
    static int new_num;
    static int delete_num;
};

class HeirScheduleCoreRequestPkt : public Packet // AAR: Agg-Agg Request 用于LA向GA发起请求
{
public:
    HeirScheduleCoreRequestPkt(double sending_time, Host *src, Host *dst, core_rts* core_rts_info);
    ~HeirScheduleCoreRequestPkt();

    core_rts* core_rts_info;
    static int new_num;
    static int delete_num;
};

class HeirScheduleCoreSCHDPkt : public Packet // AAS: Agg-Agg Schedule 用于GA向LA传递agg-core-agg调度结果
{
public:
    HeirScheduleCoreSCHDPkt(double sending_time, Host *src, Host *dst, core_schd* core_schd_info);
    ~HeirScheduleCoreSCHDPkt();

    core_schd* core_schd_info;
    static int new_num;
    static int delete_num;
};

class HeirScheduleCoreDenyPkt : public Packet // AAD: Agg-Agg Deny 用于GA向LA传递拒绝信息
{
public:
    HeirScheduleCoreDenyPkt(double sending_time, Host *src, Host *dst, core_deny* core_deny_info);
    ~HeirScheduleCoreDenyPkt();

    core_deny* core_deny_info;
    static int new_num;
    static int delete_num;
};


#endif