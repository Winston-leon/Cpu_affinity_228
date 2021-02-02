#include "ConnBindManager.h"
#include "sql/mysqld.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <mutex>

using namespace std;

~ConnBindManager()
{
    for(int i = BM_USER; i < BM_MAX; i++) {
        numa_bitmask_free(cpuInfo.bms[i]);
    }
}

void ConnBindManager::Init(char* attrs[])
{
    AssignConnBindAttr(attrs);

    GetProcessCpuInfo();

    CheckCpuBind();

    InitCpuSetThreadCount();

    return;
};

void AssignConnBindAttr(char* attrs[]) {
    for(int i = BM_USER; i < BM_MAX; i++) {
        connBindAttr[i] = attrs[i];
    }
}

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

bool ConnBindManager::CheckAttrValid(const char* attr, struct bitmask* bm)
{
    if(attr == nullptr || strcmp(attr, "\0")) {
        return false;
    }

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
            fprintf(stderr, "User thread conflict with background thread!\n");
        }
    }
};

bool ConnBindManager::CheckThreadProcConflict(struct bitmask* bm1, struct bitmask* bm2)
{
    for(unsigned long i = 0; i < cpuInfo.totalCpuNum; i++) {
        if(numa_bitmask_isbitset(bm2, i)
            && !numa_bitmask_isbitset(bm1, i)) {
                fprintf(stderr, "Thread bind cpu range conflict with allowed cpu range!\n");
                return true;
            }
    }
    return false;
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

void ConnBindManager::DynamicBind(THD* thd)
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
        /* 可复用 MySQL LogErr */
        fprintf(stderr, "Cpu bind fail!\n");
    }

    /* 记录线程绑定的node */
    thd->setThreadBindNode(pos);

    mu.unlock();
};

void ConnBindManager::DynamicUnbind(THD *thd)
{
    /* 获取线程绑定的node */
    int pos = thd->getThreadBindNode();

    /* cpuSetThreadCount中对应node记录的绑定线程数减一 */
    cpuSetThreadCount[pos].count--;
};

void ConnBindManager::StaticBind(struct bitmask* bm)
{
    int ret = numa_sched_setaffinity(std::this_thread::get_id(), &bm);

    if(ret = -1) {
        /* 可复用 MySQL LogErr */
        __throw_logic_error;
    }
}

ConnBindManager connBindManager;

