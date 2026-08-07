/**
 * @file    mains.h
 * @brief   市电检测: 过零捕获测频 + 电压窗口判断 + 掉电消抖
 */
#ifndef MAINS_H
#define MAINS_H

#include "ups_types.h"

void Mains_Init(void);
void Mains_ZC_ISR(void);        /* EXTI0 过零中断 */
void Mains_CtrlTask(void);      /* 10ms: 市电质量判定 (更新 g_ups.mains_status) */

#endif /* MAINS_H */
