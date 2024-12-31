//
//  event.h
//  TurboCpp
//
//  Created by Gautam Kumar on 3/9/14.
//
//

#ifndef EVENT_H
#define EVENT_H

#include <iostream>
#include <math.h>
#include <queue>
#include "node.h"
#include "queue.h"
#include "flow.h"
#include "random_variable.h"

//TODO import globals

#define FLOW_ARRIVAL 0
#define PACKET_QUEUING 1
#define PACKET_ARRIVAL 2
#define QUEUE_PROCESSING 3
#define RETX_TIMEOUT 5
#define FLOW_FINISHED 6
#define FLOW_PROCESSING 7
#define FLOW_CREATION_EVENT 8
#define LOGGING 9

#define HOST_SEND_RTS 10
#define HOST_SEND_DATA 11
#define LA_SEND_IPR 12
#define LA_SEND_IPS 13
#define LA_SEND_AAR 14
#define LA_SEND_RESULT 15
#define GA_SEND_AAS 16

#define DATA_PACKET_ARRIVAL 20
#define RTS_PACKET_ARRIVAL 21
#define IPR_PACKET_ARRIVAL 22
#define IPS_PACKET_ARRIVAL 23
#define AAR_PACKET_ARRIVAL 24
#define AAS_PACKET_ARRIVAL 25
#define SCHD_PACKET_ARRIVAL 26

extern void add_to_event_queue(Event *);

class Event {
    public:
        Event(uint32_t type, double time);
        virtual ~Event();
        bool operator == (const Event& e) const {
            return (time == e.time && type == e.type);
        }
        bool operator < (const Event& e) const {
            if (time < e.time) return true;
            else if (time == e.time && type < e.type) return true;
            else return false;
        }
        bool operator > (const Event& e) const {
            if (time > e.time) return true;
            else if (time == e.time && type > e.type) return true;
            else return false;
        }

        virtual void process_event() = 0;

        uint32_t unique_id;
        static uint32_t instance_count;

        uint32_t type;
        double time;
        bool cancelled;
};

struct EventComparator
{
    bool operator() (Event *a, Event *b) {
        if (fabs(a->time - b->time) < 1e-15) {
            return a->type > b->type;
        } else {
            return a->time > b->time;
        }
    }
};

//A flow arrival event Only used for FlowCreation
class FlowCreationForInitializationEvent : public Event {
    public:
        FlowCreationForInitializationEvent(
                double time, 
                Host *src, 
                Host *dst,
                EmpiricalRandomVariable *nv_bytes,
                RandomVariable *nv_intarr
                );
        ~FlowCreationForInitializationEvent();
        void process_event();
        Host *src;
        Host *dst;
        EmpiricalRandomVariable *nv_bytes;
        RandomVariable *nv_intarr;
};

//A flow arrival event
class FlowArrivalEvent : public Event {
    public:
        FlowArrivalEvent(double time, Flow *flow);
        ~FlowArrivalEvent();
        void process_event();
        Flow *flow;
};

// packet gets queued
class PacketQueuingEvent : public Event {
    public:
        PacketQueuingEvent(double time, Packet *packet, Queue *queue);
        ~PacketQueuingEvent();
        void process_event();
        Packet *packet;
        Queue *queue;
};

// packet arrival
class PacketArrivalEvent : public Event {
    public:
        PacketArrivalEvent(double time, Packet *packet);
        ~PacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class DataPacketArrivalEvent : public Event {
    public:
        DataPacketArrivalEvent(double time, Packet *packet);
        ~DataPacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class RTSPacketArrivalEvent : public Event {
    public:
        RTSPacketArrivalEvent(double time, Packet *packet);
        ~RTSPacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class IPRPacketArrivalEvent : public Event {
    public:
        IPRPacketArrivalEvent(double time, Packet *packet);
        ~IPRPacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class IPSPacketArrivalEvent : public Event {
    public:
        IPSPacketArrivalEvent(double time, Packet *packet);
        ~IPSPacketArrivalEvent();
        void process_event();
        Packet *packet;

};

class AARPacketArrivalEvent : public Event {
    public:
        AARPacketArrivalEvent(double time, Packet *packet);
        ~AARPacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class AASPacketArrivalEvent : public Event {
    public:
        AASPacketArrivalEvent(double time, Packet *packet);
        ~AASPacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class SCHDPacketArrivalEvent : public Event {
    public:
        SCHDPacketArrivalEvent(double time, Packet *packet);
        ~SCHDPacketArrivalEvent();
        void process_event();
        Packet *packet;
};


class QueueProcessingEvent : public Event {
    public:
        QueueProcessingEvent(double time, Queue *queue);
        ~QueueProcessingEvent();
        void process_event();
        Queue *queue;
};

class LoggingEvent : public Event {
    public:
        LoggingEvent(double time);
        LoggingEvent(double time, double ttl);
        ~LoggingEvent();
        void process_event();
        double ttl;
};

//A flow finished event
class FlowFinishedEvent : public Event {
    public:
        FlowFinishedEvent(double time, Flow *flow);
        ~FlowFinishedEvent();
        void process_event();
        Flow *flow;
};

//A flow processing event
class FlowProcessingEvent : public Event {
    public:
        FlowProcessingEvent(double time, Flow *flow);
        ~FlowProcessingEvent();

        void process_event();
        Flow *flow;
};

class RetxTimeoutEvent : public Event {
    public:
        RetxTimeoutEvent(double time, Flow *flow);
        ~RetxTimeoutEvent();
        void process_event();
        Flow *flow;
};

class HostSendRTSEvent : public Event {
    public:
        HostSendRTSEvent(double time, HeirScheduleHost *src, LocalArbiter *dst);
        ~HostSendRTSEvent();
        void process_event();
        HeirScheduleHost *src;
        LocalArbiter *dst;
};

class HostSendDataEvent : public Event {
    public:
        HostSendDataEvent(double time, HeirScheduleHost *src, HeirScheduleHost *dst);
        ~HostSendDataEvent();
        void process_event();
        HeirScheduleHost *src;
        HeirScheduleHost *dst;
};

class LASendIPREvent : public Event {
    public:
        LASendIPREvent(double time, LocalArbiter *src, LocalArbiter *dst);
        ~LASendIPREvent();
        void process_event();
        LocalArbiter *src;
        LocalArbiter *dst;
};

class LASendIPSEvent : public Event {
    public:
        LASendIPSEvent(double time, LocalArbiter *src, LocalArbiter *dst);
        ~LASendIPSEvent();
        void process_event();
        LocalArbiter *src;
        LocalArbiter *dst;
};

class LASendAAREvent : public Event {
    public:
        LASendAAREvent(double time, LocalArbiter *src, GlobalArbiter *dst);
        ~LASendAAREvent();
        void process_event();
        LocalArbiter *src;
        GlobalArbiter *dst;
};

class GASendAASEvent : public Event {
    public:
        GASendAASEvent(double time, GlobalArbiter *src, LocalArbiter *dst);
        ~GASendAASEvent();
        void process_event();
        GlobalArbiter *src;
        LocalArbiter *dst;
};

class LASendResultEvent : public Event {
    public:
        LASendResultEvent(double time, LocalArbiter *src, HeirScheduleHost *dst);
        ~LASendResultEvent();
        void process_event();
        LocalArbiter *src;
        HeirScheduleHost *dst;
};



// 拓扑时间槽改变
class TimeslotChangeEvent : public Event
{
public:
    TimeslotChangeEvent(double time, Topology* topology);
    ~TimeslotChangeEvent();
    void process_event();
    Topology* topology;
};

#endif /* defined(EVENT_H) */

