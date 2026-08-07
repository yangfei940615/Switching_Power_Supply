/**
 * @file    inverter.c
 * @brief   SPWM 逆变 + DC/DC 升压实现
 *
 * 架构:
 *  - TIM1 中心对齐 PWM 20kHz, CH1/CH1N + CH2/CH2N 驱动全桥, 单极性调制
 *  - 内环: 20kHz 瞬时电压环 (简化比例+前馈), 逐点修正占空比
 *  - 外环: 10ms RMS 环, 调节正弦调制深度使输出稳定在 220V
 *  - 锁相: 市电正常时输出相位跟踪市电过零, 保证 <4ms 无缝切换
 *  - TIM2 CH1/CH2 推挽 50kHz 驱动高频变压器升压, PI 稳定 380V 母线
 *  - TIM3 CH1 充电 BUCK (见 charger.c)
 */
#include "inverter.h"
#include "sensing.h"
#include "ups_config.h"
#include <math.h>

/* ---------------- 正弦表 (启动时计算, 省 Flash) ---------------- */
static uint16_t s_sine_table[INV_SINE_STEPS];   /* 0..INV_PWM_PERIOD */
static volatile uint16_t s_sine_idx = 0;
static volatile float    s_modulation = 0.0f;   /* 调制深度 0.0~0.95 */
static volatile bool     s_inv_active = false;
static uint32_t          s_soft_start_ms = 0;

/* RMS 外环 PI */
static PI_Controller_t s_rms_pi = {
    .kp = 0.002f, .ki = 0.05f,
    .integral = 0.0f, .out_min = 0.0f, .out_max = 0.95f, .prev_err = 0.0f
};

/* 升压母线 PI: 输出为推挽占空比 0~0.45 */
static PI_Controller_t s_boost_pi = {
    .kp = 0.0005f, .ki = 0.01f,
    .integral = 0.0f, .out_min = 0.0f, .out_max = 0.45f, .prev_err = 0.0f
};
static bool s_boost_active = false;

/* 锁相状态 */
static volatile int16_t s_phase_correction = 0;  /* 每次过零修正的正弦表偏移 */

/* HAL 句柄 (CubeMX 生成, 在 main.c 定义) */
extern void PWM_TIM1_SetDuty(uint16_t ch1, uint16_t ch2);
extern void PWM_TIM1_Start(void);
extern void PWM_TIM1_Stop(void);
extern void PWM_TIM2_SetDuty(uint16_t ch1, uint16_t ch2);
extern void PWM_TIM2_Start(void);
extern void PWM_TIM2_Stop(void);

/* ================= 逆变 ================= */

void Inverter_Init(void)
{
    /* 生成四分之一对称正弦表 */
    for (uint16_t i = 0; i < INV_SINE_STEPS; i++) {
        float angle = 2.0f * 3.14159265f * (float)i / (float)INV_SINE_STEPS;
        float s = sinf(angle);                     /* -1..1 */
        float duty = (s + 1.0f) * 0.5f;            /* 0..1 */
        s_sine_table[i] = (uint16_t)(duty * (float)INV_PWM_PERIOD);
    }
    s_inv_active = false;
    s_modulation = 0.0f;
}

void Inverter_Start(void)
{
    PI_Reset(&s_rms_pi);
    s_sine_idx = 0;
    s_modulation = 0.0f;
    s_soft_start_ms = 0;
    s_inv_active = true;
    PWM_TIM1_Start();
    g_ups.inverter_on = true;
}

void Inverter_Stop(void)
{
    s_inv_active = false;
    PWM_TIM1_Stop();      /* 关闭四路驱动, 输出互补无效电平 */
    g_ups.inverter_on = false;
}

/**
 * @brief TIM1 更新中断 (20kHz): 单极性 SPWM
 *        前半周期: CH1 调制, CH2 常低侧导通
 *        后半周期: CH2 调制, CH1 常低侧导通
 *        瞬时电压环对占空比做小幅前馈修正, 改善带载波形失真
 */
void Inverter_SPWM_ISR(void)
{
    if (!s_inv_active) return;

    uint16_t idx = s_sine_idx + (uint16_t)s_phase_correction;
    if (idx >= INV_SINE_STEPS) idx -= INV_SINE_STEPS;

    uint16_t base = s_sine_table[idx];
    /* 调制深度缩放, 中心对齐占空比围绕半周期点对称 */
    int32_t span = (int32_t)(base) - (int32_t)(INV_PWM_PERIOD / 2);
    span = (int32_t)(INV_PWM_PERIOD / 2) + (int32_t)(span * s_modulation);

    /* 瞬时电压环: 目标波形同相缩放 vs 实际瞬时值 */
    extern float Sensing_GetInstOutVolt(void);
    float target = s_modulation * 311.0f * ((float)base / INV_PWM_PERIOD * 2.0f - 1.0f);
    float err = target - Sensing_GetInstOutVolt();
    span += (int32_t)(err * 0.5f);   /* 比例增益, 已考虑母线电压前馈可再除 Vbus */

    if (span < 0) span = 0;
    if (span > (int32_t)INV_PWM_PERIOD) span = (int32_t)INV_PWM_PERIOD;

    bool first_half = (idx < INV_SINE_STEPS / 2);
    if (first_half) {
        PWM_TIM1_SetDuty((uint16_t)span, 0);
    } else {
        PWM_TIM1_SetDuty(0, (uint16_t)span);
    }

    s_sine_idx++;
    if (s_sine_idx >= INV_SINE_STEPS) s_sine_idx = 0;
}

/**
 * @brief 10ms: RMS 外环 + 软启动 + 锁相衰减
 */
void Inverter_CtrlTask(void)
{
    if (!s_inv_active) return;

    /* 软启动: 调制深度线性爬升上限 */
    if (s_soft_start_ms < INV_SOFT_START_MS) {
        s_soft_start_ms += TASK_CTRL_MS;
        float limit = 0.95f * ((float)s_soft_start_ms / (float)INV_SOFT_START_MS);
        if (s_modulation > limit) s_modulation = limit;
        s_rms_pi.out_max = limit;
    } else {
        s_rms_pi.out_max = 0.95f;
    }

    /* RMS 闭环 */
    float err = UPS_OUTPUT_VOLTAGE - g_ups.meas.out_volt_rms;
    s_modulation = PI_Run(&s_rms_pi, err, (float)TASK_CTRL_MS / 1000.0f);

    /* 锁相修正量缓慢回零, 避免阶跃 */
    if (s_phase_correction > 0) s_phase_correction--;
    else if (s_phase_correction < 0) s_phase_correction++;
}

/**
 * @brief 市电过零中断调用 (mains.c): 相位对齐
 * @param elapsed_ticks 自上个输出过零以来的 SPWM 步数
 */
void Inverter_PhaseLockOnMainsZC(void)
{
    if (!s_inv_active) return;
    /* 理想情况下市电过零时 s_sine_idx 应为 0 或 INV_SINE_STEPS/2
       偏差换算为修正步数, 逐周期衰减式对齐, 防止输出波形跳变 */
    int32_t err = (int32_t)s_sine_idx;
    if (err > (int32_t)(INV_SINE_STEPS / 2)) err -= INV_SINE_STEPS;
    /* 限幅: 每周期最多修正 ±2 步 (≈±3.6°), 4~5 个周期内平滑锁相 */
    if (err > 2)  s_phase_correction = -2;
    else if (err < -2) s_phase_correction = 2;
    else s_phase_correction = (int16_t)(-err);
}

bool Inverter_IsSynced(void)
{
    return (s_phase_correction == 0);
}

void Inverter_HwFaultISR(void)
{
    /* 硬件比较器已硬件封锁 PWM (BKIN), 这里只做软件记录 */
    Inverter_Stop();
    g_ups.fault_flags |= FAULT_INVERTER_HW;
}

/* ================= DC/DC 升压 ================= */

void Boost_Init(void)
{
    s_boost_active = false;
    PI_Reset(&s_boost_pi);
}

void Boost_Start(void)
{
    PI_Reset(&s_boost_pi);
    s_boost_active = true;
    PWM_TIM2_Start();
}

void Boost_Stop(void)
{
    s_boost_active = false;
    PWM_TIM2_Stop();
}

void Boost_CtrlTask(void)
{
    if (!s_boost_active) return;
    float err = BUS_VOLTAGE_TARGET - g_ups.meas.bus_volt;
    float duty = PI_Run(&s_boost_pi, err, (float)TASK_CTRL_MS / 1000.0f);
    /* 推挽两路相位差 180°, 各占空比 <45% 防变压器磁通饱和 */
    uint16_t cmp = (uint16_t)(duty * (72000000U / BOOST_PWM_FREQ_HZ));
    PWM_TIM2_SetDuty(cmp, cmp);
}
