#include "ConnBindManager.h"
#include "sql/mysqld.h"
#include <iostream>
#include <thread>
#include <mutex>

using namespace std;

mutex mu;

void ConnBindManager::Init()
{
    GetProcessCpuInfo();

    CheckCpuBind();

    InitCpuSetThreadCount();

    return;
};

/* Get available cpu information of MySQL */
void ConnBindManager::GetProcessCpuInfo()
{
    cpuInfo.totalCpuNum = numa_num_configured_cpus();
    cpuInfo.availCpuNum = numa_num_task_cpus();
    cpuInfo.totalNodeNum = numa_num_configured_nodes();
    cpuInfo.cpuNumPerNode = cpuInfo.totalCpuNum / cpuInfo.totalNodeNum;

    nodemask_t nm;
    cpuInfo.procAvailCpuMask = {
        .size = sizeof(nm) * 8,
        .maskp = nm.n
    };

    numa_bitmask_clearall(&cpuInfo.procAvailCpuMask);
    numa_sched_getaffinity(0, &cpuInfo.procAvailCpuMask);
};

void ConnBindManager::InitCpuSetThreadCount()
{
    cpuSetThreadCount.resize(cpuInfo.totalNodeNum);

    nodemask_t nm;

    for(int i = 0; i < cpuInfo.totalNodeNum; i++) {
        cpuSetThreadCount.nodeAvailCpuNum = 0;

        for(int j = cpuInfo.cpuNumPerNode * i; j < cpuInfo.cpuNumPerNode * (i + 1); j++) {
            cpuSetThreadCount[i].nodeAvailCpuMask = {
                .size = sizeof(nm) * 8,
                .maskp = nm.n
            };

            if(numa_bitmask_isbitset(cpuInfo.userThreadCpuMask, j)) {
                numa_bitmask_setbit(cpuSetThreadCount[i].nodeAvailCpuMask, j);
                cpuSetThreadCount.nodeAvailCpuNum++;
            }
        }

        cpuSetThreadCount[i].count = 0;
    }
};

bool ConnBindManager::CheckAttrValid(char* attr, struct bitmask* bm)
{
    bm = numa_parse_cpustring(attr);

    if(!bm) {
        return false;
    }

    return true;
};

bool ConnBindManager::CheckCpuBind()
{
    bool ret = true;

    for(int i = BM_USER; i < BM_MAX; i++) {
        if(!CheckAttrValid(connBindAttr[i], CPUInfo.bms[i])
            || CheckThreadProcConflict(CPUInfo.procAvailCpuMask, CPUInfo.bms[i])) {
            ret = false;
            break;
        }

        if(i != BT_USER) {
            CheckUserBackgroundConflict(CPUInfo.bms[BM_USER], CPUInfo.bms[i]);
        }
    }

    return ret;
};

void ConnBindManager::CheckUserBackgroundConflict(struct bitmask* bm1, struct bitmask* bm2)
{
    for(unsigned long i = 0; i < cpuInfo.totalCpuNum; i++) {
        if(numa_bitmask_isbitset(bm2, i)
            && numa_bitmask_isbitset(bm1, i)) {
            
        }
    }
};

void ConnBindManager::CheckThreadProcConflict(struct bitmask* bm1, struct bitmask* bm2)
{
    for(unsigned long i = 0; i < cpuInfo.totalCpuNum; i++) {
        if(numa_bitmask_isbitset(bm2, i)
            && !numa_bitmask_isbitset(bm1, i)) {
                return true;
            }
    }
    return false;
};

void ConnBindManager::Bind(THD* thd)
{
    mu.lock();

    /* 遍历找最小负载cpu组 */
    int pos = 0;
    for(int i = 0; i < cpuSetThreadCount.size(); i++) {
        if((double)cpuSetThreadCount[i].count / cpuSetThreadCount[i].nodeAvailCpuNum 
            < (double)cpuSetThreadCount[pos].count / cpuSetThreadCount[pos].nodeAvailCpuNum) {
            pos = i;
        }
    }

    /* 绑核 */
    int ret = numa_sched_setaffinity(std::this_thread::get_id(), &(cpuSetThreadCount[pos].nodeAvailCpuMask));

    if(ret == 0) {
        cpuSetThreadCount[pos].count++;
    } else if(ret == -1) {
        __throw_logic_error;
    }

    /* 记录线程绑定的node */
    thd->setThreadBindNode(pos);

    mu.unlock();
};

void ConnBindManager::Unbind(THD *thd)
{
    /* 获取线程绑定的node */
    int pos = thd->getThreadBindNode();

    /* cpuSetThreadCount中对应node记录的绑定线程数减一 */
    cpuSetThreadCount[pos].count--;
};

ConnBindManager connBindManager;

