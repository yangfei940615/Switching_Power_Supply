/**
 * @file    comm.c
 * @brief   通信协议实现 (USART3, 2400 8N1, Megatec 兼容)
 *
 * 支持命令:
 *   Q1\r  -> 状态查询: (MMM.M NNN.N PPP.P QQQ RR.R S.SS TT.T b7..b0\r
 *            市电V 故障V 输出V 负载% 频率 电池V 温度 状态位
 *   QF\r  -> 故障码查询: (F:XXXXXXXX\r  (hex 故障位图)
 *   QI\r  -> 机型信息: (#UPS-1KVA FW1.0 220V50Hz\r
 *   QS\r  -> SOC/后备时间: (SSS SSSS\r  (SOC%, 后备分钟)
 *   TL\r  -> 电池自检 10s
 *   S<x>\r-> 蜂鸣器静音 S0=恢复 S1=静音
 *   F\r   -> 额定信息: (F 220 050 024 027.2\r
 */
#include "comm.h"
#include "ups_config.h"
#include "ups_fsm.h"
#include "battery.h"
#include <string.h>
#include <stdio.h>

#define RX_BUF_LEN 32
#define TX_BUF_LEN 64

static uint8_t  s_rx_buf[RX_BUF_LEN];
static uint8_t  s_rx_len = 0;
static volatile bool s_cmd_ready = false;

extern void BSP_UART3_Send(const uint8_t *data, uint16_t len);

void Comm_Init(void)
{
    s_rx_len = 0;
    s_cmd_ready = false;
}

void Comm_RxByte(uint8_t b)
{
    if (b == '\r') { s_cmd_ready = true; return; }
    if (s_rx_len < RX_BUF_LEN - 1) {
        s_rx_buf[s_rx_len++] = b;
    } else {
        s_rx_len = 0;   /* 溢出保护: 丢弃本帧 */
    }
}

static void Comm_Reply(const char *s)
{
    BSP_UART3_Send((const uint8_t *)s, (uint16_t)strlen(s));
}

static void Handle_Q1(void)
{
    /* 状态位: bit0 电池模式, bit1 电池低, bit2 旁路, bit3 UPS故障,
               bit4 待机, bit5 自检中, bit6 关机中, bit7 蜂鸣器静音 */
    uint8_t st = 0;
    if (g_ups.state == UPS_STATE_BATTERY_MODE)           st |= 0x01;
    if (g_ups.warning_flags & WARN_BAT_LOW)              st |= 0x02;
    if (g_ups.state == UPS_STATE_BYPASS)                 st |= 0x04;
    if (g_ups.fault_flags != FAULT_NONE)                 st |= 0x08;
    if (g_ups.state == UPS_STATE_STANDBY)                st |= 0x10;
    if (g_ups.self_test_running)                         st |= 0x20;
    if (g_ups.state == UPS_STATE_SHUTDOWN)               st |= 0x40;
    if (g_ups.buzzer_mute)                               st |= 0x80;

    const UPS_Measure_t *m = &g_ups.meas;
    char buf[TX_BUF_LEN];
    snprintf(buf, sizeof(buf), "(%03.1f %03.1f %03.1f %03.0f %02.1f %02.1f %03.1f %08d\r",
             m->mains_volt_rms,                    /* 市电电压 */
             m->mains_volt_rms,                    /* 故障时市电电压(简化为当前值) */
             m->out_volt_rms,                      /* 输出电压 */
             m->load_percent,                      /* 负载 % */
             m->mains_freq,                        /* 市电频率 */
             m->bat_volt,                          /* 电池电压 */
             m->heatsink_temp,                     /* 温度 */
             (int)st);
    Comm_Reply(buf);
}

void Comm_Task(void)
{
    if (!s_cmd_ready) return;
    s_cmd_ready = false;
    s_rx_buf[s_rx_len] = '\0';
    char buf[TX_BUF_LEN];

    if (strcmp((char *)s_rx_buf, "Q1") == 0) {
        Handle_Q1();
    } else if (strcmp((char *)s_rx_buf, "QF") == 0) {
        snprintf(buf, sizeof(buf), "(F:%08lX\r", (unsigned long)g_ups.fault_flags);
        Comm_Reply(buf);
    } else if (strcmp((char *)s_rx_buf, "QI") == 0) {
        Comm_Reply("(#UPS-1KVA-HF FW1.0.0 220V/50Hz 24VDC\r");
    } else if (strcmp((char *)s_rx_buf, "QS") == 0) {
        snprintf(buf, sizeof(buf), "(%03.0f %04u %s\r",
                 g_ups.bat_soc, g_ups.est_runtime_min,
                 FSM_StateName(g_ups.state));
        Comm_Reply(buf);
    } else if (strcmp((char *)s_rx_buf, "TL") == 0) {
        Battery_StartSelfTest();
        Comm_Reply("(ACK\r");
    } else if (strcmp((char *)s_rx_buf, "F") == 0) {
        snprintf(buf, sizeof(buf), "(F %03.0f %03.0f %02.0f %04.1f\r",
                 UPS_OUTPUT_VOLTAGE, UPS_OUTPUT_FREQ,
                 BAT_NOMINAL_VOLTAGE, BAT_FLOAT_VOLTAGE);
        Comm_Reply(buf);
    } else if (s_rx_buf[0] == 'S' && s_rx_len == 2) {
        g_ups.buzzer_mute = (s_rx_buf[1] == '1');
        Comm_Reply("(ACK\r");
    } else {
        Comm_Reply("(NAK\r");
    }
    s_rx_len = 0;
}
