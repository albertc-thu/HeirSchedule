我正在基于一个开源仿真器写一个数据中心网络的仿真，我定义好接口，请你帮我补全。
开源仿真器的地址：https://github.com/NetSys/simulator
请基于这个仿真器编写代码


一些数据结构：
``` C++
struct Chunk {
    uint32_t chunkId;
    uint32_t srcId;
    uint32_t dstId;
    uint32_t chunkSize;
};
```

### 1. HeirScheduleHost
#### 数据表
chunksToSend: 是一个队列，存放发送需求，即chunk

#### 发送请求，接收路由结果
- SendRequest(): 发送请求，包含{chunkId, src, dst, chunkSize}
- ReceiveRoute(): 接收路由结果，包含{chunkId, timeslot, srcToR, srcAgg, core, dstAgg, dstToR}

#### 发送数据、接收数据
- SendData(): 发送数据包，跟据接收到的路由结果，发送数据包
- ReceiveData(): 接收数据包



### 2. ToRSwitch、AggSwitch、CoreSwitch
#### 根据包头转发数据
这三类Switch都是没有缓存的，只需要根据包头转发数据即可，不需要维护任何状态



### 1. LocalArbiter
#### 链路分配状态表
维护接下来的T个时隙内，本Pod内的链路分配情况:ToR->Agg, Agg->ToR  
- ToR2Agg: 一个$T * \frac{k}{2} * \frac{k}{2}$的矩阵，ToR2Agg[t][i][j]表示第t个时隙，ToR i->Agg j的链路是否被分配
- Agg2ToR: 一个$T * \frac{k}{2} * \frac{k}{2}$的矩阵，Agg2ToR[t][i][j]表示第t个时隙，Agg i->ToR j的链路是否被分配

#### 端节点状态表
维护接下来的T时隙内，本Pod内的端节点状态: hostIsSrc, hostIsDst  
hostIsSrc: 一个$T * \frac{k^2}{4}$的矩阵，hostIsSrc[t][i]表示第t个时隙，第i个Host是否是源节点  
hostIsDst: 一个$T * \frac{k^2}{4}$的矩阵，hostIsDst[t][i]表示第t个时隙，第i个Host是否是目的节点

#### 流量-时隙表
chunkSlotTable: 是一个Map，key是chunkId，value是时隙t

#### 路由分配表
routeTable: 是一个Map，key是chunkId，value是一个七元组(srcHost, srcToR, srcAgg, core, dstAgg, dstToR, dstHost)

#### 接收请求
- 接收来自Host的请求
- 接收来自其他LocalArbiter的请求
- 触发处理请求

#### 分配上行链路
为srcHost在本Pod内的流量需求分配上行链路，即从ToR到Agg的链路
- AllocateUpLink(): 为srcHost分配上行链路
routeTable[chunkId] = (srcHost, srcToR, srcAgg, 0, 0, 0, dstHost)

#### 分配下行链路
为dstHost在本Pod内的流量需求分配下行链路，即从Agg到ToR的链路
- AllocateDownLink(): 为dstHost分配下行链路
routeTable[chunkId] = (srcHost, srcToR, srcAgg, 0, dstAgg, dstToR, dstHost)

#### 向dstLA发送dst-pod流量请求
- SendPodRequest(): 向目的Pod发送流量请求: (chunkId, srcAgg, dstHost)

#### 向srcLA发送dst-Pod链路分配结果
- SendPodLinkResult(): 向源Pod发送链路分配结果: (chunkId, dstAgg, dstToR)

#### dstLA向GA发送Pod间链路请求
- SendGlobalRequest(): 向GA发送Pod间链路请求: (chunkId, t, srcAgg, dstAgg)

#### 组装链路分配结果
- CombineResults()
#### srcLA向HeirScheduleHost发送完整链路分配结果
- SendFinalResults(): {chunkId: {t, srcHost, srcToR, srcAgg, core, dstAgg, dstToR, dstHost}}

### 2. GlobalArbiter
#### Core分配状态表
维护接下来的T时隙内，Core的分配情况
- CoreOccupationIn: 一个$T * \frac{k^2}{4} * {k}$的矩阵，CoreOccupationIn[t][i][p]表示第t个时隙，Core i的入端口p是否被分配
- CoreOccupationOut: 一个$T * \frac{k^2}{4} * {k}$的矩阵，CoreOccupationOut[t][i][p]表示第t个时隙，Core i的出端口p是否被分配

#### 分配Core链路
- AllocateCore(): 根据(srcAgg, dstAgg)分配Core

#### 向srcLA和dstLA发送链路分配结果
- SendCoreLinkResult(): 向srcLA和dstLA发送链路分配结果

### 3. LocalArbitrationSwitch

#### 根据源、目的节点，转发请求

### 4. GlobalArbitrationSwitch

#### 根据源、目的节点，转发请求