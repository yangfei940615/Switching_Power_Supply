/**
 * @file    mains.c
 * @brief   市电检测实现
 *
 * 测频: PB0 EXTI 双边沿捕获, 相邻过零间隔 -> 半周期时间 -> 频率
 * 判定: RMS 电压窗口 + 频率窗口, 异常消抖 20ms, 恢复确认 3s
 * 掉电: 超过 60ms 无过零脉冲判定 MAINS_LOST (半个工频周期=10ms)
 */
#include "mains.h"
#include "ups_config.h"

extern uint32_t HAL_GetTick(void);
extern void Inverter_PhaseLockOnMainsZC(void);

static volatile uint32_t s_last_zc_ms = 0;
static volatile uint32_t s_zc_interval_ms = 10;   /* 相邻过零间隔 (半周期) */
static MainsStatus_t s_raw_status = MAINS_UNKNOWN;
static uint32_t s_bad_since_ms = 0;
static uint32_t s_good_since_ms = 0;
static bool s_was_bad = false;

void Mains_Init(void)
{
    s_last_zc_ms = HAL_GetTick();
    g_ups.mains_status = MAINS_UNKNOWN;
}

/**
 * @brief 市电过零中断 (EXTI0, 双边沿)
 */
void Mains_ZC_ISR(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt = now - s_last_zc_ms;
    /* 合理半周期 6~25ms (80Hz~20Hz), 滤除干扰毛刺 */
    if (dt >= 6U && dt <= 25U) {
        s_zc_interval_ms = dt;
        s_last_zc_ms = now;
        Inverter_PhaseLockOnMainsZC();   /* 输出相位跟踪市电 */
    }
}

void Mains_CtrlTask(void)
{
    UPS_Measure_t *m = &g_ups.meas;
    uint32_t now = HAL_GetTick();

    /* 频率: 半周期间隔换算 (1s 内 2*f 个过零) */
    m->mains_freq = (s_zc_interval_ms > 0) ? (1000.0f / (2.0f * (float)s_zc_interval_ms)) : 0.0f;

    /* 原始质量判定 */
    if ((now - s_last_zc_ms) > 60U || m->mains_volt_rms < 50.0f) {
        s_raw_status = MAINS_LOST;
    } else if (m->mains_volt_rms < MAINS_VOLT_LOW) {
        s_raw_status = MAINS_VOLT_LOW;
    } else if (m->mains_volt_rms > MAINS_VOLT_HIGH) {
        s_raw_status = MAINS_VOLT_HIGH;
    } else if (m->mains_freq < MAINS_FREQ_LOW || m->mains_freq > MAINS_FREQ_HIGH) {
        s_raw_status = MAINS_FREQ_ABNORMAL;
    } else {
        s_raw_status = MAINS_GOOD;
    }

    /* 消抖: 变坏要快 (20ms), 变好要慢 (3s, 带电压回差) */
    if (s_raw_status != MAINS_GOOD) {
        s_good_since_ms = 0;
        if (!s_was_bad) { s_bad_since_ms = now; s_was_bad = true; }
        if ((now - s_bad_since_ms) >= MAINS_FAIL_DEBOUNCE_MS) {
            g_ups.mains_status = s_raw_status;
            g_ups.warning_flags |= WARN_MAINS_ABNORMAL;
        }
    } else {
        s_was_bad = false;
        if (s_good_since_ms == 0) s_good_since_ms = now;
        /* 恢复回差: 掉电恢复后电压需高于欠压点+回差 */
        if (m->mains_volt_rms > (MAINS_VOLT_LOW + MAINS_VOLT_RECOVERY) &&
            (now - s_good_since_ms) >= MAINS_GOOD_DEBOUNCE_MS) {
            g_ups.mains_status = MAINS_GOOD;
            g_ups.warning_flags &= ~WARN_MAINS_ABNORMAL;
        }
    }
}
