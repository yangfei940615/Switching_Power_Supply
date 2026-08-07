/**
 * @file    battery.h
 * @brief   电池管理: SOC 估算、后备时间预测、低电关机、定期自检
 */
#ifndef BATTERY_H
#define BATTERY_H

#include "ups_types.h"

void Battery_Init(void);
void Battery_SlowTask(void);       /* 100ms: SOC/后备时间/低电判定 */
void Battery_StartSelfTest(void);  /* 手动/定期电池自检 */
void Battery_SelfTestTask(void);   /* 自检流程控制 */

#endif /* BATTERY_H */
