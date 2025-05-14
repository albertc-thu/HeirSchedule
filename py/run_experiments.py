#!/usr/bin/python

import subprocess
import threading
import multiprocessing
import os

conf_str_heirschedule = '''init_cwnd: 2
max_cwnd: 6
k: 10
retx_timeout: 9.50003e-06
propagation_delay: 1e-6
propagation_delay_data: 1e-06
propagation_delay_ctrl: 1e-08
bandwidth_data: 100000000000.0
bandwidth_ctrl: 20000000000.0
queue_size: 262144
queue_size_ctrl: 26214
slot_length: {slot_length}
mss: 512
T: {T}
dctcp_mark_thresh: 0.125
dir_name: {dir_name}
queue_type: 1
hdr_size: 40
flow_type: 112
num_flow: {0}
flow_trace: {flow_trace}
cut_through: 1
mean_flow_size: 0
load_balancing: 0
preemptive_queue: 0
big_switch: 0
host_type: 12
traffic_imbalance: 0
load: 0.6
reauth_limit: 3
magic_trans_slack: 1.1
magic_delay_scheduling: 1
use_flow_trace: 1
smooth_cdf: 1
burst_at_beginning: 0
capability_timeout: 1.5
capability_resend_timeout: 9
capability_initial: 8
capability_window: 8
capability_window_timeout: 25
ddc: 0
ddc_cpu_ratio: 0.33
ddc_mem_ratio: 0.33
ddc_disk_ratio: 0.34
ddc_normalize: 2
ddc_type: 0
deadline: 0
schedule_by_deadline: 0
avg_deadline: 0.0001
capability_third_level: 1
capability_fourth_level: 0
magic_inflate: 1
interarrival_cdf: none
num_host_types: 13
permutation_tm: 1
policy: {policy}
Threshold: {Threshold}
pias: 1
pias_1: 100000
pias_2: 100000000
'''

template = '../simulator 1 conf_{0}_{1}.txt > {dir}/result_{0}_{1}.txt'
cdf_temp = './CDF_{}.txt'



def getNumLines(trace):
    out = subprocess.check_output('wc -l {}'.format(trace), shell=True)
    return int(out.split()[0])


# def run_exp(rw, semaphore):
#     semaphore.acquire()
#     print(template.format(*rw))
#     subprocess.call(template.format(*rw), shell=True)
#     semaphore.release()

def run_command(cmd, semaphore):
    semaphore.acquire()

    print (cmd)
    subprocess.call(cmd, shell=True)
    semaphore.release()
    # process = subprocess.Popen(cmd, shell=True)
    # process.wait()

threads = []
semaphore = threading.Semaphore(5)

runs = ['heirschedule']
workloads = ['aditya', 'dctcp', 'datamining']
workloads = ["W5_0.1", "W5_0.25", "W5_0.5", "W5_0.75", "W5_1"]
# workloads = ["incast_20", "incast_40", "incast_60", "incast_80", "incast_100", "incast_120"]
# workloads = ["W5_0.1", "W5_1"]
# workloads = ["test"]
# slot_lengths = [4, 8, 16, 32, 64]
slot_lengths = [22]
# max_slot_to_allocate = [1, 4, 10, 15, 20]
max_slot_to_allocate = [10]
Ts = [100]
Polocies = ['LRU', 'SRF', 'RND', 'FCFS']
Polocies = ['SRF']
Thresholds = [1, 100000, 1000000, 10000000, 10000000000]
Thresholds = [100000]
for r in runs:
    for policy in Polocies:
        for slot_length in slot_lengths:
            for T in Ts:
                for Threshold in Thresholds:
                    for w in workloads:
                        cdf = cdf_temp.format(w)
                        numLines = 1000000
                        
                        # dir_name = '../DATA/PS3/length_{slot_length}_msta_{msta}/125_intra/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta)
                        # dir_name = '../DATA/125_28_LRU_slide_for_all/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta)
                        dir_name = '../DATA/125_28_SRF_slide_for_mice_allow_multi-srcs-dsts/T={T}/DATA_{w}'.format(slot_length=slot_length, w=w, T=T)
                        dir_name = '../DATA/125_28_LRU_slide_for_all_allow_multi-srcs-dsts/T={T}/DATA_{w}'.format(slot_length=slot_length, w=w, T=T)
                        dir_name = '../DATA/SRF-flow-based-prio-at-src/Treshold={Threshold}/multi-srcs-dsts/DATA_{w}'.format(slot_length=slot_length, w=w, T=T, Threshold=Threshold)
                        dir_name = '../DATA/{policy}/Treshold={Threshold}/multi-srcs-dsts/DATA_{w}'.format(slot_length=slot_length, w=w, T=T, Threshold=Threshold, policy=policy)
                        dir_name = '../DATA/PS-Slot-length/length={slot_length}/DATA_{w}'.format(slot_length=slot_length, w=w, T=T, Threshold=Threshold, policy=policy)
                        # dir_name = '../DATA/2x/incast/DATA_{w}'.format(slot_length=slot_length, w=w, T=T, Threshold=Threshold, policy=policy)
                        # dir_name = '../DATA/{policy}/slot-probing-off/single-srcs-dsts/DATA_{w}'.format(slot_length=slot_length, w=w, T=T, Threshold=Threshold, policy=policy)
                        # dir_name = '../DATA/LRU-flow-based/Treshold={Threshold}/single-srcs-dsts/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta, T=T, Threshold=Threshold)
                        # dir_name = '../DATA/125_28_LRU_slide_for_all/length=0.25us/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta, T=T)
                        dir_name = '../DATA/Test'
                        os.makedirs(dir_name, exist_ok=True)
                        
                        flow_trace = "../flows/flow_data_test/flows_" + w + ".txt"
                        # flow_trace = "../flows/flow_data_8_28/flows_" + w + ".txt"
                        # flow_trace = "../flows/flow_data_125_28/flows_" + w + ".txt"
                        # flow_trace = "../flows/flow_data_125_pureincast/flows_" + w + ".txt"
                        # flow_trace = "../flows/flow_data_125_intra/flows_" + w + ".txt"

                        #  generate conf file
                        if r == 'heirschedule':
                            conf_str = conf_str_heirschedule.format(numLines, flow_trace=flow_trace, dir_name=dir_name, slot_length=slot_length, T=T, Threshold=Threshold, policy=policy)
                        else:
                            assert False, r

                        confFile = "conf_{r}_{w}_{policy}_{slot_length}_{T}_Threshold={Threshold}.txt".format(r=r, w=w, slot_length=slot_length, T=T, Threshold=Threshold, policy=policy)
                        with open(confFile, 'w') as f:
                            print(confFile)
                            f.write(conf_str)
                        
                        command = '../simulator 1 {confFile} > {dir}/result_{r}_{w}.txt'.format(r=r, w=w, dir=dir_name, confFile=confFile)
                        threads.append(threading.Thread(target=run_command, args=(command, semaphore)))

print('\n')
[t.start() for t in threads]
[t.join() for t in threads]
print('finished', len(threads), 'experiments')
