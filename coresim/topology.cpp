#include "topology.h"

extern DCExpParams params;
extern unordered_map<uint32_t, string> node_type_map;
extern unordered_map<uint32_t, string> queue_type_map;
extern double get_current_time();
/*
   uint32_t num_hosts = 144;
   uint32_t num_agg_switches = 9;
   uint32_t num_core_switches = 4;
   */
Topology::Topology() {}

/*
 * PFabric topology with 144 hosts (16, 9, 4)
 */
PFabricTopology::PFabricTopology(
        uint32_t num_hosts, 
        uint32_t num_agg_switches,
        uint32_t num_core_switches, 
        double bandwidth,
        uint32_t queue_type
        ) : Topology () {
    uint32_t hosts_per_agg_switch = num_hosts / num_agg_switches;

    this->num_hosts = num_hosts;
    this->num_agg_switches = num_agg_switches;
    this->num_core_switches = num_core_switches;

    //Capacities
    double c1 = bandwidth;
    double c2 = hosts_per_agg_switch * bandwidth / num_core_switches;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type)); 
    }

    // Create Switches
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        AggSwitch* sw = new AggSwitch(i, hosts_per_agg_switch, c1, num_core_switches, c2, queue_type);
        agg_switches.push_back(sw); // TODO make generic
        switches.push_back(sw);
    }
    for (uint32_t i = 0; i < num_core_switches; i++) {
        CoreSwitch* sw = new CoreSwitch(i + num_agg_switches, num_agg_switches, c2, queue_type);
        core_switches.push_back(sw);
        switches.push_back(sw);
    }

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], agg_switches[i/16]);
        //std::cout << "Linking Host " << i << " to Agg " << i/16 << "\n";
    }

    // For agg switches -- REMAINING
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        // Queues to Hosts
        for (uint32_t j = 0; j < hosts_per_agg_switch; j++) { // TODO make generic
            Queue *q = agg_switches[i]->queues[j];
            q->set_src_dst(agg_switches[i], hosts[i * 16 + j]);
            //std::cout << "Linking Agg " << i << " to Host" << i * 16 + j << "\n";
        }
        // Queues to Core
        for (uint32_t j = 0; j < num_core_switches; j++) {
            Queue *q = agg_switches[i]->queues[j + 16];
            q->set_src_dst(agg_switches[i], core_switches[j]);
            //std::cout << "Linking Agg " << i << " to Core" << j << "\n";
        }
    }

    //For core switches -- PERFECT
    for (uint32_t i = 0; i < num_core_switches; i++) {
        for (uint32_t j = 0; j < num_agg_switches; j++) {
            Queue *q = core_switches[i]->queues[j];
            q->set_src_dst(core_switches[i], agg_switches[j]);
            //std::cout << "Linking Core " << i << " to Agg" << j << "\n";
        }
    }
}


Queue *PFabricTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->dst->type == HOST) {
        return NULL; // Packet Arrival
    }

    // At host level
    if (q->src->type == HOST) { // Same Rack or not
        assert (p->src->id == q->src->id);

        if (p->src->id / 16 == p->dst->id / 16) {
            return ((Switch *) q->dst)->queues[p->dst->id % 16];
        } 
        else {
            uint32_t hash_port = 0;
            if(params.load_balancing == 0)
                hash_port = q->spray_counter++%4;
            else if(params.load_balancing == 1)
                hash_port = (p->src->id + p->dst->id + p->flow->id) % 4;
            return ((Switch *) q->dst)->queues[16 + hash_port];
        }
    }

    // At switch level
    if (q->src->type == SWITCH) {
        if (((Switch *) q->src)->switch_type == AGG_SWITCH) {
            return ((Switch *) q->dst)->queues[p->dst->id / 16];
        }
        if (((Switch *) q->src)->switch_type == CORE_SWITCH) {
            return ((Switch *) q->dst)->queues[p->dst->id % 16];
        }
    }
    assert(false);

}


double PFabricTopology::get_oracle_fct(Flow *f) {
    int num_hops = 4;
    if (f->src->id/16 == f->dst->id/16) {
        num_hops = 2;
    }
    double propagation_delay;
    if (params.ddc != 0) { 
        if (num_hops == 2) {
            propagation_delay = 0.440;
        }
        if (num_hops == 4) {
            propagation_delay = 2.040;
        }
    }
    else {
        propagation_delay = 2 * 1000000.0 * num_hops * f->src->queue->propagation_delay; //us
    }
   
    double pkts = (double) f->size / params.mss;
    uint32_t np = floor(pkts);
    uint32_t leftover = (pkts - np) * params.mss;
	double incl_overhead_bytes = (params.mss + f->hdr_size) * np + (leftover + f->hdr_size);

    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
        if (num_hops == 4) {
            //1 packet and 1 ack
            transmission_delay += 2 * (2*params.hdr_size) * 8.0 / (4 * bandwidth);
        }
        //std::cout << "pd: " << propagation_delay << " td: " << transmission_delay << std::endl;
    }
    else {
		transmission_delay = (incl_overhead_bytes + 2.0 * f->hdr_size) * 8.0 / bandwidth;
		if (num_hops == 4) {
			// 1 packet and 1 ack
			if (np == 0) {
				// less than mss sized flow. the 1 packet is leftover sized.
				transmission_delay += 2 * (leftover + 2*params.hdr_size) * 8.0 / (4 * bandwidth);
				
			} else {
				// 1 packet is full sized
				transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth);
			}
		}
        //transmission_delay = 
        //    (
        //        (np + 1) * (params.mss + params.hdr_size) + (leftover + params.hdr_size)
        //        + 2.0 * params.hdr_size // ACK has to travel two hops
        //    ) * 8.0 / bandwidth;
        //if (num_hops == 4) {
        //    //1 packet and 1 ack
        //    transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth);  //TODO: 4 * bw is not right.
        //}
    }
    return (propagation_delay + transmission_delay); //us
}


/*
 *BigSwitchTopology  with 144 hosts
 */
BigSwitchTopology::BigSwitchTopology(
        uint32_t num_hosts, 
        double bandwidth, 
        uint32_t queue_type
        ) : Topology () {
    this->num_hosts = num_hosts;
    double c1 = bandwidth;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type));
    }

    the_switch = new CoreSwitch(0, num_hosts, c1, queue_type);
    this->switches.push_back(the_switch);

    assert(this->switches.size() == 1);

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], the_switch);
        Queue *q = the_switch->queues[i];
        q->set_src_dst(the_switch, hosts[i]);
    }
}

Queue* BigSwitchTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->dst->type == HOST) {
        assert(p->dst->id == q->dst->id);
        return NULL; // Packet Arrival
    }

    // At host level
    if (q->src->type == HOST) { // Same Rack or not
        assert (p->src->id == q->src->id);
        return the_switch->queues[p->dst->id];
    }

    assert(false);
}

double BigSwitchTopology::get_oracle_fct(Flow *f) {
    double propagation_delay = 2 * 1000000.0 * 2 * f->src->queue->propagation_delay; //us

    uint32_t np = ceil(f->size / params.mss); // TODO: Must be a multiple of 1460
    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
    }
    else {
        transmission_delay = ((np + 1) * (params.mss + params.hdr_size) 
                + 2.0 * params.hdr_size) // ACK has to travel two hops
            * 8.0 / bandwidth;
    }
    return (propagation_delay + transmission_delay); //us
}

HeirScheduleTopology::HeirScheduleTopology(uint32_t k, double rate_data, double rate_control, uint32_t queue_type): Topology(){
    this->k = k;
    this->num_hosts = k*k*k/8;
    this->num_tor_switches = k*k / 4;
    this->num_agg_switches = k*k / 4;
    this->num_core_switches = k*k/4;
    this->num_local_arbiters = k/2;
    this->num_global_arbiters = 1;
    this->num_lcs = k*k / 4;
    this->num_gcs = 1;


    uint32_t cores_per_agg_switch = k / 2;
    uint32_t tors_per_agg_switch = k / 2;
    uint32_t aggs_per_tor_switch = k / 2;
    uint32_t hosts_per_tor_switch = k / 2;
    uint32_t aggs_per_core_switch = k / 2;
    uint32_t tors_per_pod = k / 2;
    uint32_t aggs_per_pod = k / 2;
    
    uint32_t hosts_per_lcs = k / 2; // lcs: local control switch
    uint32_t las_per_lcs = 1;
    uint32_t lcs_per_la = k / 2;
    uint32_t gcs_per_la = 1; // 每个local arbiter连接一个gcs
    uint32_t las_per_gcs = k / 2; // gcs: global control switch
    uint32_t gas_per_gcs = 1;
    uint32_t gcs_per_ga = 1;
    assert(gas_per_gcs == 1);



    this->hosts.clear();
    this->tor_switches.clear();
    this->agg_switches.clear();
    this->core_switches.clear();
    this->local_arbiters.clear();
    this->global_arbiter = NULL;
    this->local_control_switches.clear();
    this->global_control_switches.clear();

    //-------------------------------create-------------------------------
    // Create Hosts
    cout << "💻 Start: create hosts." << endl;
    // uint32_t global_id = 0;
    for (uint32_t i = 0; i < num_hosts; i++)
    {
        hosts.push_back(new HeirScheduleHost(i, rate_data, rate_control, queue_type));
        // hosts.push_back(Factory::get_HeirScheduleHost(i, bandwidth, queue_type, params.host_type));
    }
    cout << "💻 Finished: create hosts." << endl;


    //Create ToR Switches
    cout << "🔗 Start: create ToR switches." << endl;
    for (uint32_t i = 0; i < num_tor_switches; i++)
    {
        tor_switches.push_back(new ToRSwitch(i, hosts_per_tor_switch, rate_data, aggs_per_tor_switch, rate_data, queue_type));
    }
    cout << "🔗 Finished: create ToR switches." << endl;

    //Create Agg Switches
    cout << "🍎 Start: create Agg switches." << endl;
    for (uint32_t i = 0; i < num_agg_switches; i++)
    {
        agg_switches.push_back(new AggSwitch(i, tors_per_agg_switch, rate_data, cores_per_agg_switch, rate_data, queue_type));
    }
    cout << "🍎 Finished: create Agg switches." << endl;

    //Create Core Switches
    cout << "🍍 Start: create Core switches." << endl;
    for (uint32_t i = 0; i < num_core_switches; i++)
    {
        core_switches.push_back(new CoreSwitch(i, aggs_per_core_switch, rate_data, queue_type));
    }
    cout << "🍍 Finished: create Core switches." << endl;

    //Create Local Arbiters
    cout << "🔒 Start: create Local Arbiters." << endl;
    for (uint32_t i = 0; i < num_local_arbiters; i++)
    {
        local_arbiters.push_back(new LocalArbiter(i, rate_control, num_gcs, queue_type));
    }
    cout << "🔒 Finished: create Local Arbiters." << endl;

    //Create Global Arbiter
    cout << "🔑 Start: create Global Arbiters." << endl;
    global_arbiter = new GlobalArbiter(0, rate_control, queue_type);
    cout << "🔑 Finished: create Global Arbiters." << endl;

    //Create Local Control Switches
    cout << "🔗 Start: create Local Control Switches." << endl;
    for (uint32_t i = 0; i < num_lcs; i++)
    {
        local_control_switches.push_back(new LocalControlSwitch(i, hosts_per_lcs, rate_control, las_per_lcs, rate_control, queue_type));
    }
    cout << "🔗 Finished: create Local Control Switches." << endl;

    //Create Global Control Switches
    cout << "🌊 Start: create Global Control Switches." << endl;
    for(uint32_t i = 0; i < num_gcs; i++)
    {
        global_control_switches.push_back(new GlobalControlSwitch(i, las_per_gcs, rate_control, gas_per_gcs, rate_control, queue_type));
    }
    cout << "🌊 Finished: create Global Control Switches." << endl;



   //-------------------------------link-------------------------------
   // host->tor
   for (uint32_t i = 0; i < num_hosts; i++)
    {
        hosts[i]->toToRQueue->set_src_dst(hosts[i], tor_switches[i / hosts_per_tor_switch]);
    }
    // host to lcs
    for (uint32_t i = 0; i < num_hosts; i++)
    {
        hosts[i]->toLAQueue->set_src_dst(hosts[i], local_control_switches[i / hosts_per_lcs]);
    }
    cout << "🍌 Finished linking hosts" << endl;

    // tor->host and tor->agg
    for (uint32_t i = 0; i < num_tor_switches; i++)
    {
        // tor to host
        for (uint32_t j = 0; j < hosts_per_tor_switch; j++)
        {
            Queue *q = tor_switches[i]->toHostQueues[j];
            q->set_src_dst(tor_switches[i], hosts[i * hosts_per_tor_switch + j]);
            // std::cout << "Linking ToR " << i << " to Host" << i * hosts_per_tor_switch + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        // tor to agg
        uint32_t pod_id = i / (k / 2);
        for (uint32_t j = 0; j < aggs_per_tor_switch; j++)
        {
            Queue *q = tor_switches[i]->toAggQueues[j];
            q->set_src_dst(tor_switches[i], agg_switches[pod_id * aggs_per_pod + j]);
            // std::cout << "Linking ToR " << i << " to Agg" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }
    cout << "🍎 Finished linking ToR switches" << endl;

    // agg->tor
    for (uint32_t i = 0; i < num_agg_switches; i++)
    {
        // agg to tor
        uint32_t pod_id = i / (k / 2);
        for (uint32_t j = 0; j < tors_per_agg_switch; j++)
        {
            Queue *q = agg_switches[i]->toToRQueues[j];
            q->set_src_dst(agg_switches[i], tor_switches[pod_id * tors_per_pod + j]);
            // std::cout << "Linking Agg " << i << " to ToR" << pod_id * tors_per_pod + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        // // agg to core
        // for (uint32_t j = 0; j < cores_per_agg_switch; j++)
        // {
        //     Queue *q = agg_switches[i]->toCoreQueues[j];
        //     q->set_src_dst(agg_switches[i], core_switches[(i % aggs_per_pod) * cores_per_agg_switch + j]);
        //     // cout << "🐼 Agg " << i << " to Core " << (i % aggs_per_pod) * cores_per_agg_switch + j << endl;
        //     // std::cout << "Linking Agg " << i << " to Core" << (i % aggs_per_pod) * cores_per_agg_switch + j << " with queue " << q->id << " " << q->unique_id << "\n";
        // }
    }
    cout << "🍍 Finished linking Agg switches" << endl;

    // core->agg
    // for (uint32_t i = 0; i < num_core_switches; i++)
    // {
    //     for (uint32_t j = 0; j < aggs_per_core_switch; j++)
    //     {
    //         Queue *q = core_switches[i]->toAggQueues[j];
    //         q->set_src_dst(core_switches[i], agg_switches[(i / cores_per_agg_switch) + aggs_per_pod * j]);
    //         // std::cout << "Linking Core " << i << " to Agg" << i / cores_per_agg_switch * aggs_per_pod + j << " with queue " << q->id << " " << q->unique_id << "\n";
    //     }
    // }
    // int conn_agg_core[num_agg_switches][params.k/2] = {
    //     {0, 1}, 
    //     {2, 3},
    //     {0, 2}, 
    //     {1, 3},
    //     {0, 3}, 
    //     {1, 2},
    //     {0, 3}, 
    //     {1, 2}
    // };
    // int conn_core_agg[num_core_switches][params.k] = {
    //     {0, 2, 4, 6},
    //     {0, 3, 5, 7},
    //     {1, 2, 5, 7},
    //     {1, 3, 4, 6}
    // };
    //     n = 5时生成的排列：
    // 排列1: 0 1 2 3 4|5 6 7 8 9|10 11 12 13 14|15 16 17 18 19|20 21 22 23 24]
    // 排列2: 0 9 13 17 21|1 5 14 18 22|2 6 10 19 23|3 7 11 15 24|4 8 12 16 20]
    // 排列3: 0 7 14 16 23|3 5 12 19 21|1 8 10 17 24|4 6 13 15 22|2 9 11 18 20]
    // 排列4: 0 8 11 19 22|2 5 13 16 24|4 7 10 18 21|1 9 12 15 23|3 6 14 17 20]
    // 排列5: 0 6 12 18 24|4 5 11 17 23|3 9 10 16 22|2 8 14 15 21|1 7 13 19 20]
    
    // agg->core
    int conn_agg_core[num_agg_switches][params.k/2] = {
        {0, 1, 2, 3, 4}, {5, 6, 7, 8, 9}, {10, 11, 12, 13, 14}, {15, 16, 17, 18, 19}, {20, 21, 22, 23, 24},
        {0, 9, 13, 17, 21}, {1, 5, 14, 18, 22}, {2, 6, 10, 19, 23}, {3, 7, 11, 15, 24}, {4, 8, 12, 16, 20},
        {0, 7, 14, 16, 23}, {3, 5, 12, 19, 21}, {1, 8, 10, 17, 24}, {4, 6, 13, 15, 22}, {2, 9, 11, 18, 20},
        {0, 8, 11, 19, 22}, {2, 5, 13, 16, 24}, {4, 7, 10, 18, 21}, {1, 9, 12, 15, 23}, {3, 6, 14, 17, 20},
        {0, 6, 12, 18, 24}, {4, 5, 11, 17, 23}, {3, 9, 10, 16, 22}, {2, 8, 14, 15, 21}, {1, 7, 13, 19, 20}
    };
    // core->agg
    // 数字0在排列中的位置：[0, 5, 10, 15, 20]
    // 数字1在排列中的位置：[0, 6, 12, 18, 24]
    // 数字2在排列中的位置：[0, 7, 14, 16, 23]
    // 数字3在排列中的位置：[0, 8, 11, 19, 22]
    // 数字4在排列中的位置：[0, 9, 13, 17, 21]
    // 数字5在排列中的位置：[1, 6, 11, 16, 21]
    // 数字6在排列中的位置：[1, 7, 13, 19, 20]
    // 数字7在排列中的位置：[1, 8, 10, 17, 24]
    // 数字8在排列中的位置：[1, 9, 12, 15, 23]
    // 数字9在排列中的位置：[1, 5, 14, 18, 22]
    // 数字10在排列中的位置：[2, 7, 12, 17, 22]
    // 数字11在排列中的位置：[2, 8, 14, 15, 21]
    // 数字12在排列中的位置：[2, 9, 11, 18, 20]
    // 数字13在排列中的位置：[2, 5, 13, 16, 24]
    // 数字14在排列中的位置：[2, 6, 10, 19, 23]
    // 数字15在排列中的位置：[3, 8, 13, 18, 23]
    // 数字16在排列中的位置：[3, 9, 10, 16, 22]
    // 数字17在排列中的位置：[3, 5, 12, 19, 21]
    // 数字18在排列中的位置：[3, 6, 14, 17, 20]
    // 数字19在排列中的位置：[3, 7, 11, 15, 24]
    // 数字20在排列中的位置：[4, 9, 14, 19, 24]
    // 数字21在排列中的位置：[4, 5, 11, 17, 23]
    // 数字22在排列中的位置：[4, 6, 13, 15, 22]
    // 数字23在排列中的位置：[4, 7, 10, 18, 21]
    // 数字24在排列中的位置：[4, 8, 12, 16, 20]
    int conn_core_agg[num_core_switches][params.k/2] = {
        {0, 5, 10, 15, 20},
        {0, 6, 12, 18, 24},
        {0, 7, 14, 16, 23},
        {0, 8, 11, 19, 22},
        {0, 9, 13, 17, 21},
        {1, 6, 11, 16, 21},
        {1, 7, 13, 19, 20},
        {1, 8, 10, 17, 24},
        {1, 9, 12, 15, 23},
        {1, 5, 14, 18, 22},
        {2, 7, 12, 17, 22},
        {2, 8, 14, 15, 21},
        {2, 9, 11, 18, 20},
        {2, 5, 13, 16, 24},
        {2, 6, 10, 19, 23},
        {3, 8, 13, 18, 23},
        {3, 9, 10, 16, 22},
        {3, 5, 12, 19, 21},
        {3, 6, 14, 17, 20},
        {3, 7, 11, 15, 24},
        {4, 9, 14, 19, 24},
        {4, 5, 11, 17, 23},
        {4, 6, 13, 15, 22},
        {4, 7, 10, 18, 21},
        {4, 8, 12, 16, 20}
    };

    for(int i = 0; i < num_core_switches; i++){ // core id
        for(int j = 0; j < params.k/2; j++){ // port in 
            for(int m = 0; m < params.k/2; m++){ // port out
                uint32_t src_agg = conn_core_agg[i][j];
                uint32_t dst_agg = conn_core_agg[i][m];
                src_dst_agg_to_core_map[{src_agg, dst_agg}] = i;
                printf("🍐 src_dst_agg_to_core_map[{%d, %d}] = %d\n", src_agg, dst_agg, i);
            }
        }
    }
    for(uint32_t agg_id = 0; agg_id < num_agg_switches; agg_id++){
        for(uint32_t _port = 0; _port < params.k / 2; _port++){
            uint32_t core_id = conn_agg_core[agg_id][_port];
            agg_to_core_port[agg_id][core_id] = _port;
            printf("🍠 agg_to_core_port[%d][%d] = %d\n", agg_id, core_id, _port);
        }
    }
    for(uint32_t core_id = 0; core_id < num_core_switches; core_id++){
        for(uint32_t _port = 0; _port < params.k / 2; _port++){
            uint32_t agg_id = conn_core_agg[core_id][_port];
            core_to_agg_port[core_id][agg_id] = _port;
            printf("🍠 core_to_agg_port[%d][%d] = %d\n", core_id, agg_id, _port);
        }
    }

    cout << "🍐 Start linking Core switches" << endl;
    cout << "num_agg_switches: " << num_agg_switches << endl;
    cout << "num_core_switches: " << num_core_switches << endl;

    // agg->core
    for (uint32_t i = 0; i < num_agg_switches; i++)
    {
        for (uint32_t j = 0; j < params.k / 2; j++){
            cout << "🍐 Linking Agg " << i << " to Core " << conn_agg_core[i][j] << endl;
            AggSwitch *agg = agg_switches[i];
            CoreSwitch *core = core_switches[conn_agg_core[i][j]];
            agg->toCoreQueues[j]->set_src_dst(agg, core);
        }
    }
    // core->agg
    for (uint32_t i = 0; i < num_core_switches; i++)
    {
        for (uint32_t j = 0; j < params.k / 2; j++){
            cout << "🍐 Linking Core " << i << " to Agg " << conn_core_agg[i][j] << endl;
            CoreSwitch *core = core_switches[i];
            AggSwitch *agg = agg_switches[conn_core_agg[i][j]];
            core->toAggQueues[j]->set_src_dst(core, agg);
        }
    }
    cout << "🍐 Finished linking Core switches" << endl;

    
    // lcs to host and lcs to la
    for (uint32_t i = 0; i < num_lcs; i++)
    {
        // lcs to host
        for (uint32_t j = 0; j < hosts_per_lcs; j++)
        {
            Queue *q = local_control_switches[i]->toHostQueues[j];
            q->set_src_dst(local_control_switches[i], hosts[i * hosts_per_lcs + j]);
            // std::cout << "Linking LCS " << i << " to Host" << i * hosts_per_lcs + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        // lcs to la
        Queue *q = local_control_switches[i]->toLAQueue;
        q->set_src_dst(local_control_switches[i], local_arbiters[i/lcs_per_la]);
            // std::cout << "Linking LCS " << i << " to LA" << i << " with queue " << q->id << " " << q->unique_id << "\n";
    }
    cout << "🍓 Finished linking Local Control Switches" << endl;

    // la to lcs and la to gcs 
    for (uint32_t i = 0; i < num_local_arbiters; i++)
    {
        // la to lcs
        for (uint32_t j = 0; j < lcs_per_la; j++)
        {
            Queue *q = local_arbiters[i]->toLCSQueues[j];
            q->set_src_dst(local_arbiters[i], local_control_switches[i * lcs_per_la + j]);
            // std::cout << "Linking LA " << i << " to LCS" << i * lcs_per_la + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }

        // la to gcs
        for(uint32_t j = 0; j < gcs_per_la; j++)
        {
            Queue *q = local_arbiters[i]->toGCSQueues[j];
            q->set_src_dst(local_arbiters[i], global_control_switches[j]);
            // std::cout << "Linking LA " << i << " to GCS" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        
    }
    cout << "🍇 Finished linking Local Arbiters" << endl;

    // gcs to la and gcs to ga
    for(uint32_t i = 0; i < num_gcs; i++)
    {
        // gcs to la
        for(uint32_t j = 0; j < las_per_gcs; j++)
        {
            Queue *q = global_control_switches[i]->toLAQueues[j];
            q->set_src_dst(global_control_switches[i], local_arbiters[j]);
            // std::cout << "Linking GCS " << i << " to LA" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }

        // gcs to ga
        for(uint32_t j = 0; j < gas_per_gcs; j++) // gas_per_gcs == 1
        {
            Queue *q = global_control_switches[i]->toGAQueue;
            q->set_src_dst(global_control_switches[i], global_arbiter);
            // std::cout << "Linking GCS " << i << " to S3" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }

    // ga to gcs
    for(uint32_t i = 0; i < num_global_arbiters; i++)
    {
        for(uint32_t j = 0; j < gcs_per_ga; j++)
        {
            Queue *q = global_arbiter->toGCSQueue;
            q->set_src_dst(global_arbiter, global_control_switches[j]);
            // std::cout << "Linking GA " << i << " to GCS" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }
    cout << "🍉 Finished linking Global Control Switches" << endl;

    cout << "✅ Establish HeirSchedule Topology." << endl;

    node_type_map[2] = "HeirSchedule_HOST";
    node_type_map[3] = "LOCAL_ARBITER";
    node_type_map[4] = "GLOBAL_ARBITER";
    node_type_map[10] = "CORE_SWITCH";
    node_type_map[11] = "AGG_SWITCH";
    node_type_map[12] = "TOR_SWITCH";
    node_type_map[13] = "LOCAL_CONTROL_SWITCH";
    node_type_map[14] = "GLOBAL_CONTROL_SWITCH";

    queue_type_map[0] = "HOST_TO_TOR";
    queue_type_map[1] = "TOR_TO_AGG";
    queue_type_map[2] = "AGG_TO_CORE";
    queue_type_map[3] = "CORE_TO_AGG";
    queue_type_map[4] = "AGG_TO_TOR";
    queue_type_map[5] = "TOR_TO_HOST";
    queue_type_map[10] = "HOST_TO_LCS";
    queue_type_map[11] = "LCS_TO_LA";
    queue_type_map[12] = "LA_TO_GCS";
    queue_type_map[13] = "GCS_TO_LA";
    queue_type_map[14] = "GCS_TO_GA";
    queue_type_map[15] = "GA_TO_GCS";
    queue_type_map[16] = "LA_TO_LCS";
    queue_type_map[17] = "LCS_TO_HOST";

}

// 从理论上，计算传播+传输时延
// src, dst 是流实际的源和目的地
double HeirScheduleTopology::get_oracle_fct(Flow* f)
{
    // cout << "💻 This is whatever's get_oracle_fct, flow_size is " << flow_size << endl;

    Host* src = f->src;
    Host* dst = f->dst;
    uint32_t flow_size = f->size;
    int num_hops = 4;
    int hosts_per_pod = k*k/4;
    int hosts_per_tor_switch = k / 2;
    // 出入不在同一pod内
    if (src->id / hosts_per_pod != dst->id / hosts_per_pod) {
        num_hops = 6;
    }
    else if (src->id / hosts_per_tor_switch != dst->id / hosts_per_tor_switch) {
        num_hops = 4;
    }
    else {
        num_hops = 2;
    }

    double propagation_delay;
    if(!params.ddc) {
        if (num_hops == 2) {
            propagation_delay = 2 * 2 * hosts[0]->toToRQueue->propagation_delay;
        }
        if (num_hops == 4) {
            propagation_delay =
                    hosts[0]->toToRQueue->propagation_delay + tor_switches[0]->toAggQueues[0]->propagation_delay + \
                            agg_switches[0]->toToRQueues[0]->propagation_delay +
                    tor_switches[0]->toHostQueues[0]->propagation_delay;
        }
        if (num_hops == 6) {
            propagation_delay =
                    hosts[0]->toToRQueue->propagation_delay + tor_switches[0]->toAggQueues[0]->propagation_delay + \
                            agg_switches[0]->toCoreQueues[0]->propagation_delay +core_switches[0]->toAggQueues[0]->propagation_delay + \
                    agg_switches[0]->toToRQueues[0]->propagation_delay + tor_switches[0]->toHostQueues[0]->propagation_delay ;
        }
    }else{
        // 暂时不考虑 ddc 的情况
        assert(false);
    }
    // cout << "🍒 Propagation delay: " << propagation_delay << endl;
    propagation_delay *= 1000000.0; // 微秒

    // 获取数据包的总数，注意是 uint32_t 除法
    uint32_t np = flow_size / params.mss;
    // if(np * params.mss < flow_size){
    //     np++;
    // }
    uint32_t over_size = flow_size - np * params.mss; // 超出的部分按照此计算
    // cout << "😂 Data packet num is " << np << endl;


    double transmission_delay;
    if (params.cut_through)
    {
        if (num_hops == 2){
            transmission_delay =
                // hdr 是 header 的简称，这里是计算总的传输数据量
                // 从 host 发出的时间 + 中间交换机上的包头时间
                ((np * (params.mss + params.hdr_size) + over_size) / hosts[0]->toToRQueue->rate + \
                params.hdr_size / tor_switches[0]->toHostQueues[0]->rate) * 8.0;
        }
        transmission_delay =
                // 多了两个交换机的 hdr
                ((np * (params.mss + params.hdr_size) + over_size) / hosts[0]->toToRQueue->rate + \
                params.hdr_size / tor_switches[0]->toAggQueues[0]->rate + \
                params.hdr_size / agg_switches[0]->toToRQueues[0]->rate + \
                params.hdr_size / tor_switches[0]->toHostQueues[0]->rate) * 8.0;
        if (num_hops == 6)
        {
            // 多了两个交换机的 hdr
            transmission_delay =
                // 多了两个交换机的 hdr
                ((np * (params.mss + params.hdr_size) + over_size) / hosts[0]->toToRQueue->rate + \
                params.hdr_size / tor_switches[0]->toAggQueues[0]->rate + \
                params.hdr_size / agg_switches[0]->toCoreQueues[0]->rate + \
                params.hdr_size / core_switches[0]->toAggQueues[0]->rate + \
                params.hdr_size / agg_switches[0]->toToRQueues[0]->rate + \
                params.hdr_size / tor_switches[0]->toHostQueues[0]->rate) * 8.0;
        }
        // std::cout << "pd: " << propagation_delay << " td: " << transmission_delay << std::endl;
    }
    else
    {
        // 暂不考虑没有 cut_through 的情况
        assert(false);
    }
    transmission_delay *= 1000000.0; // 微秒
    // cout << "🚀 Propagation delay: " << propagation_delay << " Transmission delay: " << transmission_delay << endl;

    return (propagation_delay + transmission_delay); //us

}

Queue* HeirScheduleTopology::get_next_hop(Packet *p, Queue *q){
    //数据包，在数据面，看p->path
    // cout << "🚀 Start: get_next_hop" << endl;
    if (p->type == HeirScheduleData) {
        assert(p->path->src_host_id == p->src->id);
        if (q->location == HOST_TO_TOR){
            return ((ToRSwitch *) q->dst)->toAggQueues[p->path->src_agg_id % (k / 2)];
        }
        else if(q->location == TOR_TO_AGG){
            uint32_t _port = agg_to_core_port[p->path->src_agg_id][p->path->core_id];
            return ((AggSwitch *) q->dst)->toCoreQueues[_port];
        }
        else if(q->location == AGG_TO_CORE){
            uint32_t _port = core_to_agg_port[p->path->core_id][p->path->dst_agg_id];
            return ((CoreSwitch *) q->dst)->toAggQueues[_port]; //要看是哪个pod
        }
        else if(q->location == CORE_TO_AGG){
            return ((AggSwitch *) q->dst)->toToRQueues[p->path->dst_tor_id % (k / 2)]; 
        }
        else if(q->location == AGG_TO_TOR){
            return ((ToRSwitch *) q->dst)->toHostQueues[p->path->dst_host_id % (k / 2)];
        }
        else if(q->location == TOR_TO_HOST){
            return NULL;
        }
        else{
            assert(false);
        }
    }
    else{
        // cout << "🐽 q->location: " << q->location << endl;
        if (q->location == HOST_TO_LCS){
            return ((LocalControlSwitch *) q->dst)->toLAQueue;
        }
        else if (q->location == LCS_TO_LA){
            return NULL;
        }
        else if (q->location == LA_TO_GCS){
            if(q->dst->type == LOCAL_ARBITER){
                return ((GlobalControlSwitch *) q->dst)->toLAQueues[p->dst->id];
            }
            else{
                return ((GlobalControlSwitch *) q->dst)->toGAQueue;
            }
        }
        else if (q->location == GCS_TO_LA){
            return NULL;
        }
        else if (q->location == GCS_TO_GA){
            return NULL;
        }
        else if (q->location == GA_TO_GCS){
            // cout << "🐒 p->dst->id: " << p->dst->id << endl;
            // cout << "🐔 q->dst->type: " << (dynamic_cast<GlobalControlSwitch*> (q->dst))->type << endl;
            return ((GlobalControlSwitch *) q->dst)->toLAQueues[p->dst->id];
        }
        else if (q->location == LA_TO_LCS){
            return ((LocalControlSwitch *) q->dst)->toHostQueues[(p->dst->id % (k*k/4)) / (k/2)];
        }
        else if (q->location == LCS_TO_HOST){
            return NULL;
        }
        else{
            assert(false);
        }
    }
}

// void HeirScheduleTopology::timeslot_start(double time){
//     this->epoch = 0;
//     for(int i =0; i < int(this->hosts.size()); i++){
//         add_to_event_queue(new HostSendRTSEvent(time, hosts[i], this->local_arbiters[i/(k*k/4)]));
//     }

//     double next_slot_time = time + params.slot_length;
//     add_to_event_queue(new TimeslotChangeEvent(next_slot_time, this));
// }

// void HeirScheduleTopology::timeslot_stride(double time){
//     this->epoch++;
//     for(int i =0; i < int(this->hosts.size()); i++){
//         add_to_event_queue(new HostSendRTSEvent(time, hosts[i], this->local_arbiters[i/(k*k/4)]));
//     }

//     double next_slot_time = time + params.slot_length;
//     add_to_event_queue(new TimeslotChangeEvent(next_slot_time, this));
// }





