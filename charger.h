/**
 * @file    charger.h
 * @brief   三段式电池充电控制 (恒流 -> 恒压 -> 浮充), BUCK 拓扑
 */
#ifndef CHARGER_H
#define CHARGER_H

#include "ups_types.h"

void Charger_Init(void);
void Charger_Enable(void);
void Charger_Disable(void);
void Charger_CtrlTask(void);   /* 10ms: 充电状态机 + 双闭环 */
void Charger_SlowTask(void);   /* 100ms: 阶段切换判定 */

#endif /* CHARGER_H */
