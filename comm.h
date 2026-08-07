/**
 * @file    comm.h
 * @brief   串口通信协议: 兼容 Megatec Q1 (NUT/WinPower 可直连) + 扩展命令
 */
#ifndef COMM_H
#define COMM_H

#include "ups_types.h"

void Comm_Init(void);
void Comm_Task(void);          /* 50ms: 解析接收命令并应答 */
void Comm_RxByte(uint8_t b);   /* UART 中断接收字节入口 */

#endif /* COMM_H */
