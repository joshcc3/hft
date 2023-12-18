//
// Created by joshuacoutinho on 20/12/23.
//

#ifndef LAUNCH_H
#define LAUNCH_H

#ifdef __cplusplus
extern "C" {
#endif
void initStrategy(void);

void strategyPath(void* rx, void *adapter, int irq);
#ifdef __cplusplus
}
#endif

#endif //LAUNCH_H
