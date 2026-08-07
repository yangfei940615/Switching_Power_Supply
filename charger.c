/**
 * @file    charger.c
 * @brief   三段式充电实现 (TIM3 CH1 BUCK, 从母线/市电辅助绕组取电)
 *
 * 控制策略: 电流环为内环(10ms), 电压限幅为外环
 *   CC   : 恒流 BAT_CHARGE_CURRENT_MAX, 直到电压升至均充电压
 *   CV   : 恒压 BAT_BOOST_VOLTAGE, 电流衰减至 0.1C 转浮充
 *   FLOAT: 恒压 BAT_FLOAT_VOLTAGE 长期浮充
 */
#include "charger.h"
#include "ups_config.h"

static bool s_chg_enabled = false;
static uint32_t s_cv_timer_ms = 0;
static uint32_t s_float_retry_ms = 0;

/* 电流环 PI: 输出占空比 0~0.9 */
static PI_Controller_t s_cc_pi = {
    .kp = 0.02f, .ki = 0.2f,
    .integral = 0.0f, .out_min = 0.0f, .out_max = 0.9f, .prev_err = 0.0f
};
/* 电压环 PI: 输出占空比限幅 */
static PI_Controller_t s_cv_pi = {
    .kp = 0.01f, .ki = 0.1f,
    .integral = 0.0f, .out_min = 0.0f, .out_max = 0.9f, .prev_err = 0.0f
};

extern void PWM_TIM3_SetDuty(uint16_t cmp);
extern void PWM_TIM3_Start(void);
extern void PWM_TIM3_Stop(void);

void Charger_Init(void)
{
    s_chg_enabled = false;
    g_ups.chg_stage = CHG_IDLE;
    PI_Reset(&s_cc_pi);
    PI_Reset(&s_cv_pi);
}

void Charger_Enable(void)
{
    if (s_chg_enabled) return;
    /* 初始阶段判定: 电压低于均充阈值走恒流, 否则直接浮充 */
    if (g_ups.meas.bat_volt < BAT_BOOST_VOLTAGE - 0.5f) {
        g_ups.chg_stage = CHG_CC;
    } else {
        g_ups.chg_stage = CHG_FLOAT;
    }
    PI_Reset(&s_cc_pi);
    PI_Reset(&s_cv_pi);
    s_cv_timer_ms = 0;
    s_chg_enabled = true;
    PWM_TIM3_Start();
}

void Charger_Disable(void)
{
    s_chg_enabled = false;
    g_ups.chg_stage = CHG_IDLE;
    PWM_TIM3_Stop();
}

void Charger_CtrlTask(void)
{
    if (!s_chg_enabled) return;

    /* 电池过压/反接保护: 立即关断 */
    if (g_ups.meas.bat_volt > BAT_OVERVOLTAGE || g_ups.meas.bat_volt < 5.0f) {
        Charger_Disable();
        return;
    }

    float duty;
    switch (g_ups.chg_stage) {
    case CHG_CC: {
        float err = BAT_CHARGE_CURRENT_MAX - g_ups.meas.chg_curr;
        duty = PI_Run(&s_cc_pi, err, (float)TASK_CTRL_MS / 1000.0f);
        break;
    }
    case CHG_CV: {
        float err = BAT_BOOST_VOLTAGE - g_ups.meas.bat_volt;
        duty = PI_Run(&s_cv_pi, err, (float)TASK_CTRL_MS / 1000.0f);
        /* 恒压阶段同时限流 */
        if (g_ups.meas.chg_curr > BAT_CHARGE_CURRENT_MAX) {
            duty -= 0.05f;
            if (duty < 0.0f) duty = 0.0f;
        }
        break;
    }
    case CHG_FLOAT:
    default: {
        float err = BAT_FLOAT_VOLTAGE - g_ups.meas.bat_volt;
        duty = PI_Run(&s_cv_pi, err, (float)TASK_CTRL_MS / 1000.0f);
        break;
    }
    }

    uint16_t cmp = (uint16_t)(duty * (72000000U / CHG_PWM_FREQ_HZ));
    PWM_TIM3_SetDuty(cmp);
}

void Charger_SlowTask(void)
{
    if (!s_chg_enabled) return;

    switch (g_ups.chg_stage) {
    case CHG_CC:
        /* 电压到达均充点 -> 转恒压 */
        if (g_ups.meas.bat_volt >= BAT_BOOST_VOLTAGE - 0.1f) {
            g_ups.chg_stage = CHG_CV;
            PI_Reset(&s_cv_pi);
            s_cv_timer_ms = 0;
        }
        break;
    case CHG_CV:
        /* 电流衰减到 <0.1C (0.9A) 持续 10 分钟 -> 转浮充 */
        if (g_ups.meas.chg_curr < BAT_CAPACITY_AH * 0.1f) {
            s_cv_timer_ms += TASK_SLOW_MS;
            if (s_cv_timer_ms >= 10U * 60U * 1000U) {
                g_ups.chg_stage = CHG_FLOAT;
                PI_Reset(&s_cv_pi);
            }
        } else {
            s_cv_timer_ms = 0;
        }
        break;
    case CHG_FLOAT:
        /* 浮充中若电压被拉低 (自放电/小负载), 超过 1 小时未恢复则重新均充 */
        if (g_ups.meas.bat_volt < BAT_NOMINAL_VOLTAGE - 0.8f) {
            s_float_retry_ms += TASK_SLOW_MS;
            if (s_float_retry_ms >= 3600000U) {
                g_ups.chg_stage = CHG_CC;
                PI_Reset(&s_cc_pi);
                s_float_retry_ms = 0;
            }
        } else {
            s_float_retry_ms = 0;
        }
        break;
    default:
        break;
    }
}
