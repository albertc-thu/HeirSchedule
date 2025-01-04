#include "packet.h"
#include "../run/params.h"

extern DCExpParams params;
uint32_t Packet::instance_count = 0;

Packet::Packet(
        double sending_time, 
        Flow *flow, 
        uint32_t seq_no, 
        uint32_t pf_priority,
        uint32_t size, 
        Host *src, 
        Host *dst
    ) {
    this->sending_time = sending_time;
    this->flow = flow;
    this->seq_no = seq_no;
    this->pf_priority = pf_priority;
    this->size = size;
    this->hdr_size = params.hdr_size;
    this->src = src;
    this->dst = dst;

    this->type = NORMAL_PACKET;
    this->unique_id = Packet::instance_count++;
    this->total_queuing_delay = 0;
}

Packet::~Packet() {}

PlainAck::PlainAck(Flow *flow, uint32_t seq_no_acked, uint32_t size, Host* src, Host *dst) : Packet(0, flow, seq_no_acked, 0, size, src, dst) {
    this->type = ACK_PACKET;
}

Ack::Ack(Flow *flow, uint32_t seq_no_acked, std::vector<uint32_t> sack_list, uint32_t size, Host* src, Host *dst) : Packet(0, flow, seq_no_acked, 0, size, src, dst) {
    this->type = ACK_PACKET;
    this->sack_list = sack_list;
}

RTSCTS::RTSCTS(bool type, double sending_time, Flow *f, uint32_t size, Host *src, Host *dst) : Packet(sending_time, f, 0, 0, f->hdr_size, src, dst) {
    if (type) {
        this->type = RTS_PACKET;
    }
    else {
        this->type = CTS_PACKET;
    }
}

RTS::RTS(Flow *flow, Host *src, Host *dst, double delay, int iter) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = RTS_PACKET;
    this->delay = delay;
    this->iter = iter;
}


OfferPkt::OfferPkt(Flow *flow, Host *src, Host *dst, bool is_free, int iter) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = OFFER_PACKET;
    this->is_free = is_free;
    this->iter = iter;
}

DecisionPkt::DecisionPkt(Flow *flow, Host *src, Host *dst, bool accept) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = DECISION_PACKET;
    this->accept = accept;
}

CTS::CTS(Flow *flow, Host *src, Host *dst) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = CTS_PACKET;
}

CapabilityPkt::CapabilityPkt(Flow *flow, Host *src, Host *dst, double ttl, int remaining, int cap_seq_num, int data_seq_num) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = CAPABILITY_PACKET;
    this->ttl = ttl;
    this->remaining_sz = remaining;
    this->cap_seq_num = cap_seq_num;
    this->data_seq_num = data_seq_num;
}

StatusPkt::StatusPkt(Flow *flow, Host *src, Host *dst, int num_flows_at_sender) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = STATUS_PACKET;
    this->num_flows_at_sender = num_flows_at_sender;
}


FastpassRTS::FastpassRTS(Flow *flow, Host *src, Host *dst, int remaining_pkt) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = FASTPASS_RTS;
    this->remaining_num_pkts = remaining_pkt;
}

FastpassSchedulePkt::FastpassSchedulePkt(Flow *flow, Host *src, Host *dst, FastpassEpochSchedule* schd) : Packet(0, flow, 0, 0, params.hdr_size, src, dst) {
    this->type = FASTPASS_SCHEDULE;
    this->schedule = schd;
}

SyncMessage::SyncMessage(Host *src, Host *dst, double time) : Packet(0, nullptr, 0, 0, params.hdr_size, src, dst) {
    this->type = SYNC_MSG;
    this->T1_time = time;
}

DelayRequestMessage::DelayRequestMessage(Host *src, Host *dst) : Packet(0, nullptr, 0, 0, params.hdr_size, src, dst) {
    this->type = DELAY_REQ_MSG;
}

DelayResponseMessage::DelayResponseMessage(Host *src, Host *dst, double time) : Packet(0, nullptr, 0, 0, params.hdr_size, src, dst) {
    this->type = DELAY_RES_MSG;
    this->T4_time = time;
}


int HeirScheduleDataPkt::new_num = 0;
int HeirScheduleDataPkt::delete_num = 0;
HeirScheduleDataPkt::HeirScheduleDataPkt(double sending_time, Flow *flow, uint32_t seq_no, uint32_t pf_priority,
                       uint32_t size, Host *src, Host *dst): Packet(sending_time, flow, seq_no, pf_priority, size, src, dst)
{
    this->type = HeirScheduleData;
    this->path = path;
    new_num++;
    // if(new_num > -1){
    //     cout << "new_num: " << new_num << endl;
    // } 
}
HeirScheduleDataPkt::~HeirScheduleDataPkt()
{
    delete_num++;
}

int HeirScheduleRTSPkt::new_num = 0;
int HeirScheduleRTSPkt::delete_num = 0;
HeirScheduleRTSPkt::HeirScheduleRTSPkt(double sending_time, Host *src, Host *dst, std::vector<struct rts> rts_vector):Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{

    this->type = HeirScheduleRTS;
    this->rts_vector = rts_vector;
    new_num++;
}

HeirScheduleRTSPkt::~HeirScheduleRTSPkt()
{
    // cout << "delete HeirScheduleRTSPkt" << endl;
    this->rts_vector.clear();
    delete_num++;
}

int HeirScheduleSCHDPkt::new_num = 0;
int HeirScheduleSCHDPkt::delete_num = 0;
HeirScheduleSCHDPkt::HeirScheduleSCHDPkt(double sending_time, Host *src, Host *dst,
                        double offset, double rts_offset, bool if_rts, struct path sending_path,
                        uint32_t endhost_id, uint32_t flag, bool dummy_flag, double time_to_run):Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{
    this->type = HeirScheduleSCHD;
    this->offset = offset;
    this->rts_offset = rts_offset;
    this->if_rts = if_rts;
    this->sending_path = sending_path;
    this->endhost_id = endhost_id;
    this->dummy_flag = dummy_flag;
    this->time_to_run = time_to_run;

    new_num++;
}

HeirScheduleSCHDPkt::~HeirScheduleSCHDPkt()
{
    delete_num++;
}

int HeirScheduleIPRPkt::new_num = 0;
int HeirScheduleIPRPkt::delete_num = 0;
HeirScheduleIPRPkt::HeirScheduleIPRPkt(double sending_time, Host *src, Host *dst, struct ipr ipr): Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{
    this->type = HeirScheduleIPR;
    this->ipr = ipr;
    new_num++;
}

HeirScheduleIPRPkt::~HeirScheduleIPRPkt()
{
    delete_num++;
}

int HeirScheduleIPSPkt::new_num = 0;
int HeirScheduleIPSPkt::delete_num = 0;
HeirScheduleIPSPkt::HeirScheduleIPSPkt(double sending_time, Host *src, Host *dst, struct ips ips): Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{
    this->type = HeirScheduleIPS;
    this->ips = ips;
    new_num++;
}

HeirScheduleIPSPkt::~HeirScheduleIPSPkt()
{
    delete_num++;
}


int HeirScheduleAARPkt::new_num = 0;
int HeirScheduleAARPkt::delete_num = 0;
HeirScheduleAARPkt::HeirScheduleAARPkt(double sending_time, Host *src, Host *dst, struct aar aar): Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{
    this->type = HeirScheduleAAR;
    this->aar = aar;
    new_num++;
}

HeirScheduleAARPkt::~HeirScheduleAARPkt()
{
    delete_num++;
}

int HeirScheduleAASPkt::new_num = 0;
int HeirScheduleAASPkt::delete_num = 0;
HeirScheduleAASPkt::HeirScheduleAASPkt(double sending_time, Host *src, Host *dst, struct aas aas): Packet(sending_time, NULL, 0, 0, params.hdr_size, src, dst)
{
    this->type = HeirScheduleAAS;
    this->aas = aas;
    new_num++;
}

HeirScheduleAASPkt::~HeirScheduleAASPkt()
{
    delete_num++;
}