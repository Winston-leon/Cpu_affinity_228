#ifndef CONN_BIND_MANAGER_H
#define CONN_BIND_MANAGER_H

#include "sql/sql_class.h"
// #include "storage/innobase/include/srv0srv.h"
#include <numa.h>
#include <mutex>
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

    void Init(char* attrs[]);
    void DynamicBind(THD *thd);
    void DynamicUnbind(THD *thd);
    void StaticBind(struct bitmask* bm);
    void StaticUnbind(struct bitmask* bm);

private:
    void AssignConnBindAttr(char* attrs[]);
    void GetProcessCpuInfo();
    void InitCpuSetThreadCount();
    bool CheckAttrValid(char* attr, strcut bitmask* bm);
    bool CheckCpuBind();
    void CheckUserBackgroundConflict(struct bitmask* bm1, struct bitmask* bm2);
    bool CheckThreadProcConflict(struct bitmask* bm1, struct bitmask* bm2);

private:
    std::vector<CpuSetThreadCount> cpuSetThreadCount;
    CPUInfo cpuInfo;
    const char* connBindAttr[BM_MAX];
    mutex mu;
}

#endif /* CONN_BIND_MANAGER_H */