/**
 * @file    sensing.c
 * @brief   ADC 采样与电量计算实现
 * @note    ADC1 DMA 循环扫描 7 通道, TIM1 更新事件触发 (20kHz)
 *          交流量: 去直流偏置 -> 滑动平均 -> 每半个工频周期累积平方和求 RMS
 */
#include "sensing.h"
#include "ups_config.h"
#include <math.h>
#include <string.h>

/* DMA 原始缓冲区 (由 HAL_ADC_Start_DMA 填充) */
extern uint16_t g_adc_dma_buf[ADC_NUM_CHANNELS];

static float s_ch_filtered[ADC_NUM_CHANNELS];      /* 滑动平均后的电压值(V) */
static float s_ch_avg[ADC_NUM_CHANNELS][ADC_SAMPLE_AVG];
static uint8_t s_avg_idx = 0;

/* 半周期 RMS 累积器 */
static float s_sq_out_v = 0.0f, s_sq_out_i = 0.0f, s_sq_mains_v = 0.0f;
static uint16_t s_half_cycle_samples = 0;
static float s_inst_out_volt = 0.0f;   /* 供逆变电压环使用的瞬时值 */

/* NTC 10K B=3950 分压计算 */
float Sensing_NTC_ToTemp(float v_ntc)
{
    /* 上拉 10K 到 3.3V, NTC 接地 */
    if (v_ntc >= ADC_VREF - 0.01f) return -40.0f;
    float r_ntc = 10000.0f * v_ntc / (ADC_VREF - v_ntc);
    if (r_ntc <= 0.0f) return 125.0f;
    float inv_t = 1.0f / 298.15f + logf(r_ntc / 10000.0f) / 3950.0f;
    return (1.0f / inv_t) - 273.15f;
}

void Sensing_Init(void)
{
    memset(s_ch_avg, 0, sizeof(s_ch_avg));
    memset(s_ch_filtered, 0, sizeof(s_ch_filtered));
    /* HAL_ADCEx_Calibration_Start(&hadc1); 上电校准在 main 中完成 */
}

/**
 * @brief 1ms 快环: 读取 DMA 缓冲, 滑动平均滤波
 *        交流瞬时值(去偏置)累积到半周期 RMS 缓冲
 */
void Sensing_FastTask(void)
{
    for (uint8_t ch = 0; ch < ADC_NUM_CHANNELS; ch++) {
        float v = (float)g_adc_dma_buf[ch] * (ADC_VREF / ADC_FULL_SCALE);
        s_ch_avg[ch][s_avg_idx] = v;
        float sum = 0.0f;
        for (uint8_t i = 0; i < ADC_SAMPLE_AVG; i++) sum += s_ch_avg[ch][i];
        s_ch_filtered[ch] = sum / ADC_SAMPLE_AVG;
    }
    s_avg_idx = (s_avg_idx + 1) % ADC_SAMPLE_AVG;

    /* 交流量去直流偏置 */
    float out_v_ac  = (s_ch_filtered[ADC_CH_OUT_VOLT]   - AC_SIGNAL_BIAS) * SCALE_OUT_VOLT;
    float out_i_ac  = (s_ch_filtered[ADC_CH_OUT_CURR]   - AC_SIGNAL_BIAS) * SCALE_OUT_CURR;
    float mains_v_ac = (s_ch_filtered[ADC_CH_MAINS_VOLT] - AC_SIGNAL_BIAS) * SCALE_MAINS_VOLT;

    s_inst_out_volt = out_v_ac;

    s_sq_out_v  += out_v_ac * out_v_ac;
    s_sq_out_i  += out_i_ac * out_i_ac;
    s_sq_mains_v += mains_v_ac * mains_v_ac;
    s_half_cycle_samples++;
}

/**
 * @brief 10ms 控制环: 每半工频周期(10ms@50Hz)结算一次 RMS
 */
void Sensing_CtrlTask(void)
{
    UPS_Measure_t *m = &g_ups.meas;

    if (s_half_cycle_samples > 0) {
        float n = (float)s_half_cycle_samples;
        m->out_volt_rms   = sqrtf(s_sq_out_v / n);
        m->out_curr_rms   = sqrtf(s_sq_out_i / n);
        m->mains_volt_rms = sqrtf(s_sq_mains_v / n);
        s_sq_out_v = s_sq_out_i = s_sq_mains_v = 0.0f;
        s_half_cycle_samples = 0;
    }

    /* 直流量 */
    m->bus_volt = s_ch_filtered[ADC_CH_BUS_VOLT] * SCALE_BUS_VOLT;
    m->bat_volt = s_ch_filtered[ADC_CH_BAT_VOLT] * SCALE_BAT_VOLT;
    m->chg_curr = s_ch_filtered[ADC_CH_CHG_CURR] * SCALE_CHG_CURR;
    m->heatsink_temp = Sensing_NTC_ToTemp(s_ch_filtered[ADC_CH_NTC_TEMP]);

    /* 功率与负载率 */
    m->apparent_power = m->out_volt_rms * m->out_curr_rms;
    m->load_percent   = (m->apparent_power / (float)UPS_RATED_POWER_VA) * 100.0f;
    if (m->load_percent > 300.0f) m->load_percent = 300.0f; /* 限幅防溢出 */
}

/**
 * @brief 逆变电压环读取输出电压瞬时值
 */
float Sensing_GetInstOutVolt(void)
{
    return s_inst_out_volt;
}
