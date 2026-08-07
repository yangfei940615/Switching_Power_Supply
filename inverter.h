/**
 * @file    inverter.h
 * @brief   SPWM 全桥逆变控制: 正弦调制 + 输出电压闭环 + 市电锁相 + 软启动
 *          以及 DC/DC 高频升压 (24V -> 380V 母线) 控制
 */
#ifndef INVERTER_H
#define INVERTER_H

#include "ups_types.h"

void  Inverter_Init(void);
void  Inverter_Start(void);          /* 启动逆变 (带软启动) */
void  Inverter_Stop(void);           /* 立即停止 PWM 输出 */
void  Inverter_SPWM_ISR(void);       /* TIM1 更新中断, 20kHz 波形控制 */
void  Inverter_CtrlTask(void);       /* 10ms: RMS 外环 + 锁相 */
bool  Inverter_IsSynced(void);       /* 输出与市电是否同相同频 */
void  Inverter_HwFaultISR(void);     /* 硬件过流比较器中断 (TIM1 BKIN) */

void  Boost_Init(void);
void  Boost_Start(void);
void  Boost_Stop(void);
void  Boost_CtrlTask(void);          /* 10ms: 母线电压 PI 闭环 */

#endif /* INVERTER_H */
