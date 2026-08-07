/**
 * @file    protection.h
 * @brief   系统保护: 过载 I-t 曲线、短路、过温、母线/电池/输出电压保护
 */
#ifndef PROTECTION_H
#define PROTECTION_H

#include "ups_types.h"

void Protection_Init(void);
void Protection_FastTask(void);   /* 1ms: 短路/硬件级确认 */
void Protection_CtrlTask(void);   /* 10ms: 过载曲线/电压/温度 */
void Protection_RequestFault(uint32_t fault_bit);

#endif /* PROTECTION_H */
