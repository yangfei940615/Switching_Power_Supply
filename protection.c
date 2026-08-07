/**
 * @file    protection.c
 * @brief   系统保护实现
 *
 * 过载反时限曲线 (负载率 -> 允许持续时间):
 *   >200% : 100ms  -> FAULT_OVERLOAD (逆变限流但保护元件优先)
 *   >150% : 1s
 *   >125% : 30s
 *   >105% : 持续告警 (蜂鸣器), 不强制关机
 * 短路: 硬件比较器 50us 内封锁 PWM, 软件 1ms 内确认锁存
 */
#include "protection.h"
#include "ups_config.h"

extern uint32_t HAL_GetTick(void);

static uint32_t s_overload_since_ms = 0;
static uint8_t  s_sc_confirm_cnt = 0;

void Protection_Init(void)
{
    s_overload_since_ms = 0;
    s_sc_confirm_cnt = 0;
}

void Protection_RequestFault(uint32_t fault_bit)
{
    g_ups.fault_flags |= fault_bit;
}

void Protection_FastTask(void)
{
    /* 短路确认: 硬件标志置位后连续 3 次(3ms)确认 -> 锁存
       硬件 BKIN 已即时封锁 PWM, 这里是状态机层面的故障锁存 */
    if (g_ups.fault_flags & FAULT_INVERTER_HW) {
        if (++s_sc_confirm_cnt >= 3) {
            g_ups.fault_flags |= FAULT_SHORT_CIRCUIT;
        }
    } else {
        s_sc_confirm_cnt = 0;
    }
}

void Protection_CtrlTask(void)
{
    const UPS_Measure_t *m = &g_ups.meas;
    uint32_t now = HAL_GetTick();

    /* 故障已锁存则无需继续判定, 交由状态机处理 */
    if (g_ups.fault_flags != FAULT_NONE) return;

    /* ---- 过载反时限 ---- */
    float load = m->load_percent;
    uint32_t allowed_ms;
    if      (load > 200.0f) allowed_ms = PROT_OVERLOAD_200_MS;
    else if (load > 150.0f) allowed_ms = PROT_OVERLOAD_150_MS;
    else if (load > 125.0f) allowed_ms = PROT_OVERLOAD_125_MS;
    else                    allowed_ms = 0;   /* 不触发计时 */

    if (load > (float)PROT_OVERLOAD_WARN_PCT) {
        g_ups.warning_flags |= WARN_OVERLOAD;
    } else {
        g_ups.warning_flags &= ~WARN_OVERLOAD;
    }

    if (allowed_ms > 0) {
        if (s_overload_since_ms == 0) s_overload_since_ms = now;
        if ((now - s_overload_since_ms) >= allowed_ms) {
            Protection_RequestFault(FAULT_OVERLOAD);
        }
    } else {
        s_overload_since_ms = 0;
    }

    /* ---- 母线电压 ---- */
    if (m->bus_volt > BUS_VOLTAGE_MAX) Protection_RequestFault(FAULT_BUS_OVERVOLT);
    /* 逆变运行时母线欠压 -> 电池模式下的常见故障 */
    if (g_ups.inverter_on && m->bus_volt < BUS_VOLTAGE_MIN) {
        Protection_RequestFault(FAULT_BUS_UNDERVOLT);
    }

    /* ---- 输出电压 (仅逆变输出时判定) ---- */
    if (g_ups.inverter_on) {
        if (m->out_volt_rms > PROT_OUTPUT_VOLT_HIGH ||
            (m->out_volt_rms < PROT_OUTPUT_VOLT_LOW && m->load_percent > 5.0f)) {
            Protection_RequestFault(FAULT_OUTPUT_VOLT);
        }
    }

    /* ---- 电池过压 ---- */
    if (m->bat_volt > BAT_OVERVOLTAGE) Protection_RequestFault(FAULT_BAT_OVERVOLT);

    /* ---- 温度 ---- */
    if (m->heatsink_temp >= PROT_TEMP_SHUTDOWN) {
        Protection_RequestFault(FAULT_OVER_TEMP);
    } else if (m->heatsink_temp >= PROT_TEMP_WARN) {
        g_ups.warning_flags |= WARN_OVER_TEMP;
    } else {
        g_ups.warning_flags &= ~WARN_OVER_TEMP;
    }
}
