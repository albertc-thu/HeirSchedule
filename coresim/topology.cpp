#include "topology.h"

extern DCExpParams params;
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
    this->num_hosts = k*k*k/4;
    this->num_tor_switches = k*k / 2;
    this->num_agg_switches = k*k / 2;
    this->num_core_switches = k*k/4;
    this->num_local_arbiters = k;
    this->num_global_arbiters = 1;
    this->num_lcs = k*k / 2;
    this->num_gcs = 1;


    uint32_t cores_per_agg_switch = k / 2;
    uint32_t tors_per_agg_switch = k / 2;
    uint32_t aggs_per_tor_switch = k / 2;
    uint32_t hosts_per_tor_switch = k / 2;
    uint32_t aggs_per_core_switch = k;
    uint32_t tors_per_pod = k / 2;
    uint32_t aggs_per_pod = k / 2;
    
    uint32_t hosts_per_lcs = k / 2; // lcs: local control switch
    uint32_t las_per_lcs = 1;
    uint32_t lcs_per_la = k / 2;
    uint32_t gcs_per_la = 1; // 每个local arbiter连接一个gcs
    uint32_t las_per_gcs = k; // gcs: global control switch
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

    // agg->tor and agg->core
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
        // agg to core
        for (uint32_t j = 0; j < cores_per_agg_switch; j++)
        {
            Queue *q = agg_switches[i]->toCoreQueues[j];
            q->set_src_dst(agg_switches[i], core_switches[(i % aggs_per_pod) * cores_per_agg_switch + j]);
            // cout << "🐼 Agg " << i << " to Core " << (i % aggs_per_pod) * cores_per_agg_switch + j << endl;
            // std::cout << "Linking Agg " << i << " to Core" << (i % aggs_per_pod) * cores_per_agg_switch + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }
    cout << "🍍 Finished linking Agg switches" << endl;

    // core->agg
    for (uint32_t i = 0; i < num_core_switches; i++)
    {
        for (uint32_t j = 0; j < aggs_per_core_switch; j++)
        {
            Queue *q = core_switches[i]->toAggQueues[j];
            q->set_src_dst(core_switches[i], agg_switches[(i / cores_per_agg_switch) + aggs_per_pod * j]);
            // std::cout << "Linking Core " << i << " to Agg" << i / cores_per_agg_switch * aggs_per_pod + j << " with queue " << q->id << " " << q->unique_id << "\n";
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
            propagation_delay = 2 * 1000000.0 * 2 * hosts[0]->toToRQueue->propagation_delay; //us
        }
        if (num_hops == 4) {
            propagation_delay =
                    hosts[0]->toToRQueue->propagation_delay + tor_switches[0]->toAggQueues[0]->propagation_delay + \
                            agg_switches[0]->toToRQueues[0]->propagation_delay +
                    tor_switches[0]->toHostQueues[0]->propagation_delay;
        }
        if (num_hops == 6) {
            propagation_delay =
                    hosts[0]->toToRQueue->propagation_delay + tor_switches[0]->toAggQueues[k / 2]->propagation_delay + \
                            agg_switches[0]->toCoreQueues[0]->propagation_delay +core_switches[0]->toAggQueues[0]->propagation_delay + \
                    agg_switches[0]->toToRQueues[0]->propagation_delay + tor_switches[0]->toHostQueues[0]->propagation_delay ;
        }
    }else{
        // 暂时不考虑 ddc 的情况
        assert(false);
    }
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
        //std::cout << "pd: " << propagation_delay << " td: " << transmission_delay << std::endl;
    }
    else
    {
        // 暂不考虑没有 cut_through 的情况
        assert(false);
    }
    transmission_delay *= 1000000.0; // 微秒

    return (propagation_delay + transmission_delay); //us

}

Queue* HeirScheduleTopology::get_next_hop(Packet *p, Queue *q){
    //数据包，在数据面，看p->path
    // cout << "🚀 Start: get_next_hop" << endl;
    if (p->type == HeirScheduleData) {
        assert(p->path.src_host_id == p->src->id);
        if (q->location == HOST_TO_TOR){
            return ((ToRSwitch *) q->dst)->toAggQueues[p->path.src_agg_id % (k / 2)];
        }
        else if(q->location == TOR_TO_AGG){
            return ((AggSwitch *) q->dst)->toCoreQueues[p->path.core_id % (k / 2)];
        }
        else if(q->location == AGG_TO_CORE){
            return ((CoreSwitch *) q->dst)->toAggQueues[p->path.dst_agg_id / (k / 2)]; //要看是哪个pod
        }
        else if(q->location == CORE_TO_AGG){
            return ((AggSwitch *) q->dst)->toToRQueues[p->path.dst_tor_id % (k / 2)]; 
        }
        else if(q->location == AGG_TO_TOR){
            return ((ToRSwitch *) q->dst)->toHostQueues[p->path.dst_host_id % (k / 2)];
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





