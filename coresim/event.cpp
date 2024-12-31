//
//  event.cpp
//  TurboCpp
//
//  Created by Gautam Kumar on 3/9/14.
//
//

#include <iomanip>

#include "event.h"
#include "packet.h"
#include "topology.h"
#include "debug.h"

#include "../ext/factory.h"

#include "../run/params.h"

extern Topology* topology;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern double current_time;
extern DCExpParams params;
extern std::deque<Event*> flow_arrivals;
extern std::deque<Flow*> flows_to_schedule;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;

extern uint32_t num_outstanding_packets_at_50;
extern uint32_t num_outstanding_packets_at_100;
extern uint32_t arrival_packets_at_50;
extern uint32_t arrival_packets_at_100;
extern uint32_t arrival_packets_count;
extern uint32_t total_finished_flows;

extern uint32_t backlog3;
extern uint32_t backlog4;
extern uint32_t duplicated_packets_received;
extern uint32_t duplicated_packets;
extern uint32_t injected_packets;
extern uint32_t completed_packets;
extern uint32_t total_completed_packets;
extern uint32_t dead_packets;
extern uint32_t sent_packets;

extern EmpiricalRandomVariable *nv_bytes;

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();

uint32_t Event::instance_count = 0;

Event::Event(uint32_t type, double time) {
    this->type = type;
    this->time = time;
    this->cancelled = false;
    this->unique_id = Event::instance_count++;
}

Event::~Event() {
}


/* Flow Arrival */
FlowCreationForInitializationEvent::FlowCreationForInitializationEvent(
        double time, 
        Host *src, 
        Host *dst,
        EmpiricalRandomVariable *nv_bytes, 
        RandomVariable *nv_intarr
    ) : Event(FLOW_CREATION_EVENT, time) {
    this->src = src;
    this->dst = dst;
    this->nv_bytes = nv_bytes;
    this->nv_intarr = nv_intarr;
}

FlowCreationForInitializationEvent::~FlowCreationForInitializationEvent() {}

void FlowCreationForInitializationEvent::process_event() {
    uint32_t nvVal, size;
    uint32_t id = flows_to_schedule.size();
    if (params.bytes_mode) {
        nvVal = nv_bytes->value();
        size = (uint32_t) nvVal;
    } else {
        nvVal = (nv_bytes->value() + 0.5); // truncate(val + 0.5) equivalent to round to nearest int
        if (nvVal > 2500000) {
            std::cout << "Giant Flow! event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size " << (nvVal*1460) << " between " << src->id << " " << dst->id << "\n";
            nvVal = 2500000;
        }
        size = (uint32_t) nvVal * 1460;
    }

    if (size != 0) {
        flows_to_schedule.push_back(Factory::get_flow(id, time, size, src, dst, params.flow_type));
    }

    double tnext = time + nv_intarr->value();
//        std::cout << "event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size "
//         << size << " between " << src->id << " " << dst->id << " " << (tnext - get_current_time())*1e6 << "\n";

    add_to_event_queue(
            new FlowCreationForInitializationEvent(
                tnext,
                src, 
                dst,
                nv_bytes, 
                nv_intarr
                )
            );
}


/* Flow Arrival */
int flow_arrival_count = 0;

FlowArrivalEvent::FlowArrivalEvent(double time, Flow* flow) : Event(FLOW_ARRIVAL, time) {
    this->flow = flow;
}

FlowArrivalEvent::~FlowArrivalEvent() {
}

void FlowArrivalEvent::process_event() {
    if (flow_arrivals.size() > 0) // Flow arrival 的链式连锁反应
    {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }

    cout << "Flow " << flow->id << " arrived at " << get_current_time() << endl;

    HeirScheduleHost* src = flow->src;
    HeirScheduleHost* dst = flow->dst;

    src->sending_flows.insert(flow);

    if(params.pias == 1){

        // < pias_1
        flow_data_at_src new_data_1;
        new_data_1.flow = flow;
        new_data_1.remaining_size = min(params.pias_1, flow->size);
        new_data_1.priority = 0;
        src->per_dst_queues[0][dst->id].push_back(new_data_1);

        // cout << "new_data_1.remaining_size: " << new_data_1.remaining_size << endl;
        // cout << "new_data_1.flow->remaining_size: " << new_data_1.flow->remaining_size_to_send << endl;

        // pias_1 < < pias_2
        if(flow->size > params.pias_1){
            flow_data_at_src new_data_2;
            new_data_2.flow = flow;
            new_data_2.remaining_size = min(flow->size - params.pias_1, params.pias_2 - params.pias_1);
            new_data_2.priority = 1;
            src->per_dst_queues[1][dst->id].push_back(new_data_2);
            // cout << "new_data_2: " << new_data_2.remaining_size << endl;
            // cout << "new_data_2.flow->remaining_size: " << new_data_2.flow->remaining_size_to_send << endl;

        }

        // > pias_2
        if(flow->size > params.pias_2){
            flow_data_at_src new_data_3;
            new_data_3.flow = flow;
            new_data_3.remaining_size = flow->size - params.pias_2;
            new_data_3.priority = 2;
            src->per_dst_queues[2][dst->id].push_back(new_data_3);
            // cout << "new_data_3: " << new_data_3.remaining_size << endl;
        }
    }else{
        flow_data_at_src new_data_1;
        new_data_1.flow = flow;
        new_data_1.remaining_size = flow->size;
        new_data_1.priority = 0;
        src->per_dst_queues[0][dst->id].push_back(new_data_1);
    }
}

PacketQueuingEvent::PacketQueuingEvent(double time, Packet *packet, Queue *queue)
    : Event(PACKET_QUEUING, time) {
        this->packet = packet;
        this->queue = queue;
    }
PacketQueuingEvent::~PacketQueuingEvent() {
}
// 处理 PacketQueuingEvent
void PacketQueuingEvent::process_event() {
    if (!queue->busy) {
        // 新包触发的 processing
        queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy = true;
        queue->packet_transmitting = packet;
    }

    // if (packet->flow->id == 28237){
    //     string Node = "Null";
    //     if (queue->src->type == 0){
    //         Node = "End Host ";
    //     }
    //     else{
    //         switch (((Switch*) queue->src)->switch_type)
    //         {
    //         case 10:
    //             Node = "Core Switch ";
    //             break;
    //         case 11:
    //             Node = "Agg Switch ";
    //             break;
    //         case 12:
    //             Node = "Edge Switch ";
    //             break;
            
    //         default:
    //             break;
    //         }
    //     }
        
    //     cout << "seq: " << packet->seq_no << " queueing at " << Node << queue->src->id << endl;
    // }

    // 抢占，只有 pfabric 会用到
    else if( params.preemptive_queue && this->packet->pf_priority < queue->packet_transmitting->pf_priority) {
        double remaining_percentage = (queue->queue_proc_event->time - get_current_time()) / queue->get_transmission_delay(queue->packet_transmitting->size);

        if(remaining_percentage > 0.01){
            queue->preempt_current_transmission();

            queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
            add_to_event_queue(queue->queue_proc_event);
            queue->busy = true;
            queue->packet_transmitting = packet;
        }
    }

    queue->enque(packet);
}

/* Packet Arrival */
PacketArrivalEvent::PacketArrivalEvent(double time, Packet *packet)
    : Event(PACKET_ARRIVAL, time) {
        this->packet = packet;
    }

PacketArrivalEvent::~PacketArrivalEvent() {
}

void PacketArrivalEvent::process_event() {
    if (packet->type == NORMAL_PACKET) {
        completed_packets++;
    }

    packet->flow->receive(packet);
}

DataPacketArrivalEvent::DataPacketArrivalEvent(double time, Packet *packet)
    : Event(DATA_PACKET_ARRIVAL, time) {
        this->packet = packet;
}
DataPacketArrivalEvent::~DataPacketArrivalEvent(){
    
}

/* Queue Processing */
QueueProcessingEvent::QueueProcessingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
}

QueueProcessingEvent::~QueueProcessingEvent() {
    if (queue->queue_proc_event == this) {
        queue->queue_proc_event = NULL;
        queue->busy = false; //TODO is this ok??
    }
}

void QueueProcessingEvent::process_event() {
    Packet *packet = queue->deque();
    if (packet) {
        queue->busy = true;
        queue->busy_events.clear();
        queue->packet_transmitting = packet;
        Queue *next_hop = topology->get_next_hop(packet, queue);
        double td = queue->get_transmission_delay(packet->size);
        double pd = queue->propagation_delay;
        //double additional_delay = 1e-10;
        queue->queue_proc_event = new QueueProcessingEvent(time + td, queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy_events.push_back(queue->queue_proc_event);
        if (next_hop == NULL) {
            Event* arrival_evt;
            // 根据包的种类，确定下一步的处理
            switch (packet->type)
            {
            case HeirScheduleData:
                arrival_evt = new DataPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleRTS:
                arrival_evt = new RTSPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleIPR:
                arrival_evt = new IPRPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleIPS:
                arrival_evt = new IPSPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleAAR:
                arrival_evt = new AARPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleAAS:
                arrival_evt = new AASPacketArrivalEvent(time + td + pd, packet);
                break;
            case HeirScheduleSCHD:
                arrival_evt = new SCHDPacketArrivalEvent(time + td + pd, packet);
                break;
            default:
                break;
            }
            
            add_to_event_queue(arrival_evt);
            queue->busy_events.push_back(arrival_evt);
        } else {
            Event* queuing_evt = NULL;
            if (params.cut_through == 1) {
                double cut_through_delay =
                    queue->get_transmission_delay(packet->flow->hdr_size);
                queuing_evt = new PacketQueuingEvent(time + cut_through_delay + pd, packet, next_hop);
            } else {
                queuing_evt = new PacketQueuingEvent(time + td + pd, packet, next_hop);
            }

            add_to_event_queue(queuing_evt);
            queue->busy_events.push_back(queuing_evt);
        }
    } else {
        queue->busy = false;
        queue->busy_events.clear();
        queue->packet_transmitting = NULL;
        queue->queue_proc_event = NULL;
    }
}


RTSPacketArrivalEvent::RTSPacketArrivalEvent(double time, Packet *packet)
    : Event(RTS_PACKET_ARRIVAL, time) {
        this->packet = packet;
    }
RTSPacketArrivalEvent::~RTSPacketArrivalEvent() {
}

void RTSPacketArrivalEvent::process_event() {
    //将rts保存到LA的received_rts中
    for (auto it = ((HeirScheduleRTSPkt*)packet)->rts_vector.begin(); it != ((HeirScheduleRTSPkt*)packet)->rts_vector.end(); it++) {
        ((LocalArbiter* )packet->dst)->received_rts.push_back(*it);
    }
    ((LocalArbiter* )packet->dst)->process_rts();

}



LoggingEvent::LoggingEvent(double time) : Event(LOGGING, time){
    this->ttl = 1e10;
}

LoggingEvent::LoggingEvent(double time, double ttl) : Event(LOGGING, time){
    this->ttl = ttl;
}

LoggingEvent::~LoggingEvent() {
}

void LoggingEvent::process_event() {
    double current_time = get_current_time();
    // can log simulator statistics here.
}


/* Flow Finished */
FlowFinishedEvent::FlowFinishedEvent(double time, Flow *flow)
    : Event(FLOW_FINISHED, time) {
        this->flow = flow;
    }

FlowFinishedEvent::~FlowFinishedEvent() {}

void FlowFinishedEvent::process_event() {
    this->flow->finished = true;
    this->flow->finish_time = get_current_time();
    this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    total_finished_flows++;
    auto slowdown = 1000000 * flow->flow_completion_time / topology->get_oracle_fct(flow);
    if (slowdown < 1.0 && slowdown > 0.9999) {
        slowdown = 1.0;
    }
    if (slowdown < 1.0) {
        std::cout << "bad slowdown " << 1e6 * flow->flow_completion_time << " " << topology->get_oracle_fct(flow) << " " << slowdown << "\n";
    }
    assert(slowdown >= 1.0);

    if (print_flow_result()) {
        std::cout << std::setprecision(4) << std::fixed ;
        std::cout
            << flow->id << " "
            << flow->size << " "
            << flow->src->id << " "
            << flow->dst->id << " "
            << 1000000 * flow->start_time << " "
            << 1000000 * flow->finish_time << " "
            << 1000000.0 * flow->flow_completion_time << " "
            << topology->get_oracle_fct(flow) << " "
            << slowdown << " "
            << flow->total_pkt_sent << "/" << (flow->size/flow->mss) << "//" << flow->received_count << " "
            << flow->data_pkt_drop << "/" << flow->ack_pkt_drop << "/" << flow->pkt_drop << " "
            << 1000000 * (flow->first_byte_send_time - flow->start_time) << " "
            << std::endl;
        std::cout << std::setprecision(9) << std::fixed;
    }
}


/* Flow Processing */
FlowProcessingEvent::FlowProcessingEvent(double time, Flow *flow)
    : Event(FLOW_PROCESSING, time) {
        this->flow = flow;
    }

FlowProcessingEvent::~FlowProcessingEvent() {
    if (flow->flow_proc_event == this) {
        flow->flow_proc_event = NULL;
    }
}

void FlowProcessingEvent::process_event() {
    this->flow->send_pending_data();
}


/* Retx Timeout */
RetxTimeoutEvent::RetxTimeoutEvent(double time, Flow *flow)
    : Event(RETX_TIMEOUT, time) {
        this->flow = flow;
    }

RetxTimeoutEvent::~RetxTimeoutEvent() {
    if (flow->retx_event == this) {
        flow->retx_event = NULL;
    }
}

void RetxTimeoutEvent::process_event() {
    flow->handle_timeout();
}

HostSendRTSEvent::HostSendRTSEvent(double time, HeirScheduleHost *src, LocalArbiter *dst)
    : Event(HOST_SEND_RTS, time) {
        this->src = src;
        this->dst = dst;
    }

HostSendRTSEvent::~HostSendRTSEvent() {
}

void HostSendRTSEvent::process_event() {
    src->host_send_rts(time);
}

HostSendDataEvent::HostSendDataEvent(double time, HeirScheduleHost *src, HeirScheduleHost *dst)
    : Event(HOST_SEND_DATA, time) {
        this->src = src;
        this->dst = dst;
    }

HostSendDataEvent::~HostSendDataEvent() {
}

void HostSendDataEvent::process_event() {
    src->host_send_data(time);
}

LASendIPREvent::LASendIPREvent(double time, LocalArbiter *src, LocalArbiter *dst)
    : Event(LA_SEND_IPR, time) {
        this->src = src;
        this->dst = dst;
    }
LASendIPREvent::~LASendIPREvent() {}

void LASendIPREvent::process_event() {
    src->send_interpod_rts(time);
}

LASendIPSEvent::LASendIPSEvent(double time, LocalArbiter *src, LocalArbiter *dst)
    : Event(LA_SEND_IPS, time) {
        this->src = src;
        this->dst = dst;
    }
LASendIPSEvent::~LASendIPSEvent() {}

void LASendIPSEvent::process_event() {
    src->send_interpod_schd();
}

LASendAAREvent::LASendAAREvent(double time, LocalArbiter *src, GlobalArbiter *dst)
    : Event(LA_SEND_AAR, time) {
        this->src = src;
        this->dst = dst;
    }
LASendAAREvent::~LASendAAREvent() {}

void LASendAAREvent::process_event() {
    src->send_agg_agg_rts();
}

GASendAASEvent::GASendAASEvent(double time, GlobalArbiter *src, LocalArbiter *dst)
    : Event(GA_SEND_AAS, time) {
        this->src = src;
        this->dst = dst;
    }
GASendAASEvent::~GASendAASEvent() {}

void GASendAASEvent::process_event() {
    src->send_agg_agg_schd();
}

LASendResultEvent::LASendResultEvent(double time, LocalArbiter *src, HeirScheduleHost *dst)
    : Event(LA_SEND_RESULT, time) {
        this->src = src;
        this->dst = dst;
    }
LASendResultEvent::~LASendResultEvent() {}

void LASendResultEvent::process_event() {
    src->send_final_results();
}

