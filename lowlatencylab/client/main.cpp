//
// Created by joshuacoutinho on 21/12/23.
//

#include "mdmcclient.h"
#include "L2OB.h"
#include "OE.h"
#include "Strat.h"
#include <cassert>
#include <cstring>


int main() {
    const auto numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores < 0) {
        perror("sysconf");
        assert(false);
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(PINNED_CPU, &cpuset);

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
        assert(false);
    }
    int ret = sched_getaffinity(0, sizeof(cpuset), &cpuset);
    if (ret == -1) {
        perror("sched_getaffinity");
        assert(false);
    }
    assert(CPU_ISSET(PINNED_CPU, &cpuset));

    sched_param schparam{};
    constexpr int receiveThreadPolicy = SCHED_FIFO;
    constexpr int priority = 99; // sched_get_priority_max(receiveThreadPolicy);
    schparam.sched_priority = priority;
    ret = sched_setscheduler(0, receiveThreadPolicy, &schparam);

    if (ret) {
        printf__("Error [%d] in priority [%s]", errno, strerror(errno));
        assert(false);
    }


    Driver s{};
    s.run();
}
