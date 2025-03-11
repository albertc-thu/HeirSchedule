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
bandwidth_ctrl: 10000000000.0
queue_size: 262144
queue_size_ctrl: 26214
slot_length: {slot_length}
mss: 512
T: 100
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
max_slot_to_allocate: {msta}
pias: 1
pias_1: 10000
pias_2: 100000
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
semaphore = threading.Semaphore(35)

runs = ['heirschedule']
workloads = ['aditya', 'dctcp', 'datamining']
workloads = ["W5_0.1", "W5_0.25", "W5_0.5", "W5_0.75", "W5_1"]
# workloads = ["W5_0.5"]
workloads = ["test"]
# slot_lengths = [4, 8, 16, 32, 64]
slot_lengths = [32]
# max_slot_to_allocate = [1, 4, 10, 15, 20]
max_slot_to_allocate = [10]

for r in runs:
    for msta in max_slot_to_allocate:
        for slot_length in slot_lengths:
            for w in workloads:
                cdf = cdf_temp.format(w)
                numLines = 1000000
                
                dir_name = '../DATA/PS3/length_{slot_length}_msta_{msta}/125_intra/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta)
                dir_name = '../DATA/125_intra/DATA_{w}'.format(slot_length=slot_length, w=w, msta=msta)
                dir_name = '../DATA/Test'
                os.makedirs(dir_name, exist_ok=True)
                
                flow_trace = "../flows/flow_data_test/flows_" + w + ".txt"
                # flow_trace = "../flows/flow_data_8_28/flows_" + w + ".txt"
                # flow_trace = "../flows/flow_data_125_28/flows_" + w + ".txt"
                # flow_trace = "../flows/flow_data_125_intra/flows_" + w + ".txt"

                #  generate conf file
                if r == 'heirschedule':
                    conf_str = conf_str_heirschedule.format(numLines, flow_trace=flow_trace, dir_name=dir_name, slot_length=slot_length, msta=msta)
                else:
                    assert False, r

                confFile = "conf_{r}_{w}_{slot_length}_{msta}.txt".format(r=r, w=w, slot_length=slot_length, msta=msta)
                with open(confFile, 'w') as f:
                    print(confFile)
                    f.write(conf_str)
                
                command = '../simulator 1 {confFile} > {dir}/result_{r}_{w}.txt'.format(r=r, w=w, dir=dir_name, slot_length=slot_length, confFile=confFile)
                threads.append(threading.Thread(target=run_command, args=(command, semaphore)))

print('\n')
[t.start() for t in threads]
[t.join() for t in threads]
print('finished', len(threads), 'experiments')
