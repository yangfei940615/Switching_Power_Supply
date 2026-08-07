/**
 * @file    battery.c
 * @brief   电池管理实现
 *
 * SOC 估算: 开路电压法(静置/浮充时校准) + 安时积分(放电时) 融合
 * 自检: 每月一次或按键触发, 转电池模式带载 10s, 监测电压跌落判断电池健康
 */
#include "battery.h"
#include "ups_config.h"
#include "inverter.h"

extern uint32_t HAL_GetTick(void);

/* 24V 铅酸 OCV-SOC 查表点 (V -> %) */
static const float s_ocv_tab[] = {20.0f, 21.0f, 22.0f, 23.0f, 24.2f, 25.4f, 26.6f};
static const float s_soc_tab[] = {0.0f,  10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 100.0f};
#define OCV_TAB_LEN  (sizeof(s_ocv_tab) / sizeof(s_ocv_tab[0]))

static float s_coulomb_soc = 100.0f;      /* 安时积分 SOC */
static uint32_t s_last_selftest_ms = 0;
static uint32_t s_selftest_start_ms = 0;
static float s_selftest_start_volt = 0.0f;
static uint32_t s_bat_low_since_ms = 0;

#define SELFTEST_INTERVAL_MS   (30UL * 24UL * 3600UL * 1000UL)  /* 30 天 */
#define SELFTEST_DURATION_MS   10000U

static float OCV_ToSOC(float v)
{
    if (v <= s_ocv_tab[0]) return 0.0f;
    if (v >= s_ocv_tab[OCV_TAB_LEN - 1]) return 100.0f;
    for (uint8_t i = 1; i < OCV_TAB_LEN; i++) {
        if (v <= s_ocv_tab[i]) {
            float t = (v - s_ocv_tab[i-1]) / (s_ocv_tab[i] - s_ocv_tab[i-1]);
            return s_soc_tab[i-1] + t * (s_soc_tab[i] - s_soc_tab[i-1]);
        }
    }
    return 0.0f;
}

void Battery_Init(void)
{
    s_coulomb_soc = 100.0f;
    g_ups.bat_soc = 100.0f;
    s_last_selftest_ms = HAL_GetTick();
}

void Battery_SlowTask(void)
{
    const UPS_Measure_t *m = &g_ups.meas;

    if (g_ups.state == UPS_STATE_BATTERY_MODE) {
        /* 放电: 安时积分 (100ms 步长) */
        float discharge_a = (m->apparent_power * 0.9f) / m->bat_volt; /* 逆变效率≈90% */
        s_coulomb_soc -= (discharge_a / (BAT_CAPACITY_AH * 3600.0f)) * 100.0f
                         * ((float)TASK_SLOW_MS / 1000.0f);
        if (s_coulomb_soc < 0.0f) s_coulomb_soc = 0.0f;
        /* 放电中电压法失真大, 以安时积分为主 */
        g_ups.bat_soc = s_coulomb_soc;

        /* 低电位告警 (消抖 2s) */
        if (m->bat_volt <= BAT_LOW_WARNING) {
            g_ups.warning_flags |= WARN_BAT_LOW;
            if (m->bat_volt <= BAT_LOW_SHUTDOWN) {
                if (s_bat_low_since_ms == 0) s_bat_low_since_ms = HAL_GetTick();
                if (HAL_GetTick() - s_bat_low_since_ms >= 2000U) {
                    g_ups.fault_flags |= FAULT_BAT_LOW;
                }
            }
        } else {
            g_ups.warning_flags &= ~WARN_BAT_LOW;
            s_bat_low_since_ms = 0;
        }

        /* 预估后备时间: 剩余安时 / 当前放电电流 */
        if (discharge_a > 0.5f) {
            float remain_ah = BAT_CAPACITY_AH * (s_coulomb_soc / 100.0f);
            g_ups.est_runtime_min = (uint16_t)(remain_ah / discharge_a * 60.0f);
        } else {
            g_ups.est_runtime_min = 999;
        }
    } else {
        /* 市电模式: 浮充状态下用 OCV 法校准安时积分 */
        if (g_ups.chg_stage == CHG_FLOAT) {
            float ocv_soc = OCV_ToSOC(m->bat_volt);
            s_coulomb_soc = s_coulomb_soc * 0.99f + ocv_soc * 0.01f;  /* 慢速收敛 */
        }
        g_ups.bat_soc = s_coulomb_soc;
        g_ups.warning_flags &= ~WARN_BAT_LOW;
        s_bat_low_since_ms = 0;

        /* 定期自检触发 */
        if (!g_ups.self_test_running &&
            (HAL_GetTick() - s_last_selftest_ms) >= SELFTEST_INTERVAL_MS) {
            Battery_StartSelfTest();
        }
    }
}

void Battery_StartSelfTest(void)
{
    /* 仅市电正常且逆变热备时允许自检 */
    if (g_ups.mains_status != MAINS_GOOD || g_ups.self_test_running) return;
    if (g_ups.state != UPS_STATE_LINE_MODE) return;
    g_ups.self_test_running = true;
    s_selftest_start_ms = HAL_GetTick();
    s_selftest_start_volt = g_ups.meas.bat_volt;
    /* 状态机检测到 self_test_running 后会切换到电池模式 10 秒 */
}

void Battery_SelfTestTask(void)
{
    if (!g_ups.self_test_running) return;
    uint32_t elapsed = HAL_GetTick() - s_selftest_start_ms;

    if (elapsed >= SELFTEST_DURATION_MS) {
        /* 判定: 10s 带载放电电压跌落 >1.5V 或低于低电告警点 -> 电池老化 */
        float drop = s_selftest_start_volt - g_ups.meas.bat_volt;
        if (drop > 1.5f || g_ups.meas.bat_volt < BAT_LOW_WARNING) {
            g_ups.warning_flags |= WARN_BAT_REPLACE;
        } else {
            g_ups.warning_flags &= ~WARN_BAT_REPLACE;
        }
        g_ups.self_test_running = false;
        s_last_selftest_ms = HAL_GetTick();
    }
    /* 自检中电池异常低: 提前终止, 交还状态机 */
    if (g_ups.meas.bat_volt < BAT_LOW_SHUTDOWN) {
        g_ups.self_test_running = false;
        g_ups.warning_flags |= WARN_BAT_REPLACE;
        s_last_selftest_ms = HAL_GetTick();
    }
}
