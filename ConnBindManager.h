#ifndef CONN_BIND_MANAGER_H
#define CONN_BIND_MANAGER_H

#include "sql/sql_class.h"
#include <numa.h>
#include <cstdio>
#include <pthread.h>
#include <vector.h>

extern ConnBindManager connBindManger;

enum CpuBitMaskId {
    BM_USER = 0,
    BM_LW,
    BM_LF,
    BM_LWN,
    BM_LFN,
    BM_LC,
    BM_LCP,
    BM_LP,
    BM_MAX
};

struct CpuSetThreadCount {
    struct bitmask* nodeAvailCpuMask;
    int nodeAvailCpuNum;
    int count;
};

struct CPUInfo {
    int totalCpuNum;
    int totalNodeNum;
    int availCpuNum;
    int cpuNumPerNode;
    struct bitmask* procAvailCpuMask;

    struct bitmask* bms[BT_MAX];
};

class ConnBindManager {
public:
    ConnBindManager();
    ~ConnBindManager();

    void Init();
    void Bind(THD *thd);
    void Unbind(THD *thd);

private:
    void GetProcessCpuInfo();
    void InitCpuSetThreadCount();
    bool CheckAttrValid(char* attr, strcut bitmask* bm);
    bool CheckCpuBind();
    void CheckUserBackgroundConflict(struct bitmask* bm1, struct bitmask* bm2);
    bool CheckThreadProcConflict(struct bitmask* bm1, struct bitmask* bm2);

private:
    std::vector<CpuSetThreadCount> cpuSetThreadCount;
    CPUInfo cpuInfo;
    char* connBindAttr[BM_MAX];
}

#endif /* CONN_BIND_MANAGER_H */