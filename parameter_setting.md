## Bandwidth
data plane：100Gbps
control plane：10Gbps

## propogation delay
data plane：1us
control plane：10ns (from LA to GCS, GCS to GA)

## queue length
Referring to DCTCP, it sets the queue length to 256KB when the bandwidth is 100Gbps.
So I set the queue length of control plane switch to 25.6KB.
