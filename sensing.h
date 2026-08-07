/**
 * @file    sensing.h
 * @brief   ADC 采样与电量计算 (电压/电流 RMS、频率、温度、负载率)
 */
#ifndef SENSING_H
#define SENSING_H

#include "ups_types.h"

void  Sensing_Init(void);
void  Sensing_FastTask(void);   /* 1ms: 原始采样滤波, 交流瞬时值入环 */
void  Sensing_CtrlTask(void);   /* 10ms: RMS/功率/负载率计算 */
float Sensing_NTC_ToTemp(float v_ntc);

#endif /* SENSING_H */
