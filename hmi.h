/**
 * @file    hmi.h
 * @brief   人机界面: LED 指示、蜂鸣器告警音型、按键 (开机/消音/自检)
 */
#ifndef HMI_H
#define HMI_H

#include "ups_types.h"

void HMI_Init(void);
void HMI_Task(void);           /* 250ms: LED/蜂鸣器刷新 */
void HMI_KeyScan(void);        /* 10ms: 按键扫描(消抖/长按) */

#endif /* HMI_H */
