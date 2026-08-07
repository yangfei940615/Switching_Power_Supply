/**
 * @file    ups_types.h
 * @brief   UPS 全局数据类型定义
 */
#ifndef UPS_TYPES_H
#define UPS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/** UPS 运行状态机 */
typedef enum {
    UPS_STATE_POWER_ON = 0,   /**< 上电初始化/自检 */
    UPS_STATE_STANDBY,        /**< 待机(输出关闭, 可充电) */
    UPS_STATE_LINE_MODE,      /**< 市电模式(旁路输出+充电, 逆变热备同步) */
    UPS_STATE_BATTERY_MODE,   /**< 电池模式(逆变输出) */
    UPS_STATE_BYPASS,         /**< 静态旁路(过载转旁路等) */
    UPS_STATE_FAULT,          /**< 故障锁存 */
    UPS_STATE_SHUTDOWN        /**< 关机流程 */
} UPS_State_t;

/** 故障位 (bitmask, 锁存) */
typedef enum {
    FAULT_NONE            = 0,
    FAULT_OVERLOAD        = (1U << 0),   /**< 过载超时 */
    FAULT_SHORT_CIRCUIT   = (1U << 1),   /**< 输出短路 */
    FAULT_OVER_TEMP       = (1U << 2),   /**< 散热器过温 */
    FAULT_BUS_OVERVOLT    = (1U << 3),   /**< 母线过压 */
    FAULT_BUS_UNDERVOLT   = (1U << 4),   /**< 母线欠压 */
    FAULT_BAT_OVERVOLT    = (1U << 5),   /**< 电池过压 */
    FAULT_BAT_LOW         = (1U << 6),   /**< 电池耗尽关机 */
    FAULT_OUTPUT_VOLT     = (1U << 7),   /**< 输出电压异常 */
    FAULT_INVERTER_HW     = (1U << 8),   /**< 逆变桥硬件保护(H桥过流) */
    FAULT_FAN_FAIL        = (1U << 9),   /**< 风扇故障 */
    FAULT_EEPROM          = (1U << 10)   /**< 参数存储错误 */
} UPS_Fault_t;

/** 警告位 (bitmask, 不锁存) */
typedef enum {
    WARN_NONE             = 0,
    WARN_OVERLOAD         = (1U << 0),   /**< 负载 >105% */
    WARN_BAT_LOW          = (1U << 1),   /**< 电池低电位 */
    WARN_OVER_TEMP        = (1U << 2),   /**< 温度偏高 */
    WARN_MAINS_ABNORMAL   = (1U << 3),   /**< 市电电压/频率异常 */
    WARN_BAT_REPLACE      = (1U << 4),   /**< 电池自检测试失败, 建议更换 */
    WARN_COMM_LOSS        = (1U << 5)
} UPS_Warning_t;

/** 市电质量状态 */
typedef enum {
    MAINS_UNKNOWN = 0,
    MAINS_GOOD,          /**< 电压频率均在窗口内 */
    MAINS_VOLT_LOW,      /**< 欠压 */
    MAINS_VOLT_HIGH,     /**< 过压 */
    MAINS_FREQ_ABNORMAL, /**< 频率异常 */
    MAINS_LOST           /**< 掉电 */
} MainsStatus_t;

/** 充电阶段 */
typedef enum {
    CHG_IDLE = 0,    /**< 不充电 */
    CHG_CC,          /**< 恒流(均充) */
    CHG_CV,          /**< 恒压 */
    CHG_FLOAT        /**< 浮充 */
} ChargeStage_t;

/** 实时测量数据 (由 sensing 模块每周期更新) */
typedef struct {
    float mains_volt_rms;    /**< 市电电压 RMS (V) */
    float mains_freq;        /**< 市电频率 (Hz) */
    float out_volt_rms;      /**< 输出电压 RMS (V) */
    float out_curr_rms;      /**< 输出电流 RMS (A) */
    float out_freq;          /**< 输出频率 (Hz) */
    float bus_volt;          /**< 直流母线电压 (V) */
    float bat_volt;          /**< 电池电压 (V) */
    float chg_curr;          /**< 充电电流 (A) */
    float heatsink_temp;     /**< 散热器温度 (℃) */
    float load_percent;      /**< 负载率 (%) */
    float apparent_power;    /**< 视在功率 (VA) */
} UPS_Measure_t;

/** UPS 全局运行时状态 (单例) */
typedef struct {
    UPS_State_t    state;
    UPS_State_t    prev_state;
    uint32_t       fault_flags;      /* UPS_Fault_t bitmask, 锁存 */
    uint32_t       warning_flags;    /* UPS_Warning_t bitmask */
    MainsStatus_t  mains_status;
    ChargeStage_t  chg_stage;
    float          bat_soc;          /**< 电池剩余电量 (%) */
    uint16_t       est_runtime_min;  /**< 预估后备时间 (分钟) */
    bool           buzzer_mute;
    bool           inverter_on;
    bool           self_test_running;
    uint32_t       state_enter_ms;   /**< 进入当前状态的系统时刻 */
    uint32_t       run_seconds;      /**< 累计运行秒数 */
    UPS_Measure_t  meas;
} UPS_Context_t;

/** 通用 PI 控制器 */
typedef struct {
    float kp;
    float ki;
    float integral;
    float out_min;
    float out_max;
    float prev_err;
} PI_Controller_t;

extern UPS_Context_t g_ups;

static inline float PI_Run(PI_Controller_t *pi, float err, float dt)
{
    pi->integral += err * dt;
    /* 抗积分饱和 */
    float out = pi->kp * err + pi->ki * pi->integral;
    if (out > pi->out_max) { out = pi->out_max; pi->integral -= err * dt; }
    if (out < pi->out_min) { out = pi->out_min; pi->integral -= err * dt; }
    pi->prev_err = err;
    return out;
}

static inline void PI_Reset(PI_Controller_t *pi)
{
    pi->integral = 0.0f;
    pi->prev_err = 0.0f;
}

#endif /* UPS_TYPES_H */
