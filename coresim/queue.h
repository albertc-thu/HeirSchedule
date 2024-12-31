#ifndef QUEUE_H
#define QUEUE_H

#include <deque>
#include <stdint.h>
#include <vector>

#define DROPTAIL_QUEUE 1

class Node;
class Packet;
class Event;

class QueueProcessingEvent;
class PacketPropagationEvent;

class Queue {
    public:
        Queue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        void set_src_dst(Node *src, Node *dst);
        virtual void enque(Packet *packet);
        virtual Packet *deque();
        virtual void drop(Packet *packet);
        double get_transmission_delay(uint32_t size);
        void preempt_current_transmission();
        uint32_t limit_pkts;
        uint32_t pkt_num;

        // Members
        uint32_t id;
        uint32_t unique_id;
        static uint32_t instance_count;
        double rate;

        //限制队列长度
        uint32_t limit_bytes;
        std::deque<Packet *> packets;
        uint32_t bytes_in_queue;
        //在处理 QueueProcessingEvent 和 PacketQueuingEvent 时，标记为真
        bool busy;
        QueueProcessingEvent *queue_proc_event;

        std::vector<Event*> busy_events;
        Packet* packet_transmitting;

        Node *src;
        Node *dst;

        uint64_t b_arrivals, b_departures;
        uint64_t p_arrivals, p_departures;

        double propagation_delay;
        bool interested;

        uint64_t pkt_drop;
        uint64_t spray_counter;

        int location;

        double output_stop_occupied;

        void print_packet(Packet* packet);

        uint32_t pkt_num_cutthrough = 0;

        Packet* packet_in_occupation;
};

class CPQueue : public Queue {
    public:
        CPQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        virtual void enque(Packet *packet);
};



class ProbDropQueue : public Queue {
    public:
        ProbDropQueue(
                uint32_t id, 
                double rate, 
                uint32_t limit_bytes,
                double drop_prob, 
                int location
                );
        virtual void enque(Packet *packet);

        double drop_prob;
};

#endif
