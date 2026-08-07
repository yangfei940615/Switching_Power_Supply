/**
 * @file    ups_fsm.h
 * @brief   UPS 主状态机: 模式切换、继电器控制、风扇控制
 */
#ifndef UPS_FSM_H
#define UPS_FSM_H

#include "ups_types.h"

void FSM_Init(void);
void FSM_CtrlTask(void);          /* 10ms: 状态迁移判定 */
void FSM_RequestShutdown(void);
const char *FSM_StateName(UPS_State_t s);

#endif /* UPS_FSM_H */
