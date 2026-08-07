/**
 * @file    ups_config.h
 * @brief   1KVA UPS 全局配置：技术规格、硬件映射、保护阈值
 * @target  STM32F103RCT6 (72MHz, 48KB RAM, 256KB Flash)
 * @topology 高频在线互动式: 市电 -> DC/DC升压(24V->380V) -> 全桥SPWM逆变 -> 220VAC
 */
#ifndef UPS_CONFIG_H
#define UPS_CONFIG_H

/*--------------------------------------------------------------------------
 * 整机规格
 *--------------------------------------------------------------------------*/
#define UPS_RATED_POWER_VA        1000U    /* 额定视在功率 1KVA */
#define UPS_RATED_POWER_W         800U     /* 额定有功功率 800W (PF=0.8) */
#define UPS_OUTPUT_VOLTAGE        220.0f   /* 输出电压 (VAC RMS) */
#define UPS_OUTPUT_FREQ           50.0f    /* 输出频率 (Hz) */
#define UPS_OUTPUT_FREQ_TOL       0.5f     /* 输出频率精度 ±Hz */

/*--------------------------------------------------------------------------
 * 电池组 (2 x 12V/9Ah 串联)
 *--------------------------------------------------------------------------*/
#define BAT_NOMINAL_VOLTAGE       24.0f
#define BAT_FLOAT_VOLTAGE         27.2f    /* 浮充电压 */
#define BAT_BOOST_VOLTAGE         28.8f    /* 均充/快速充电电压 */
#define BAT_CHARGE_CURRENT_MAX    2.0f     /* 最大充电电流 (A) */
#define BAT_LOW_WARNING           21.6f    /* 低电量告警 (V) */
#define BAT_LOW_SHUTDOWN          20.4f    /* 低电量关机 (V) */
#define BAT_OVERVOLTAGE           30.0f    /* 电池过压 (V) */
#define BAT_CAPACITY_AH           9.0f     /* 电池容量 (Ah) */

/*--------------------------------------------------------------------------
 * 直流母线 (DC BUS)
 *--------------------------------------------------------------------------*/
#define BUS_VOLTAGE_TARGET        380.0f   /* 母线目标电压 (VDC) */
#define BUS_VOLTAGE_MIN           320.0f   /* 母线欠压阈值 */
#define BUS_VOLTAGE_MAX           420.0f   /* 母线过压阈值 */

/*--------------------------------------------------------------------------
 * 市电检测窗口
 *--------------------------------------------------------------------------*/
#define MAINS_VOLT_LOW            165.0f   /* 市电欠压 -> 转电池 (VAC) */
#define MAINS_VOLT_HIGH           275.0f   /* 市电过压 -> 转电池 (VAC) */
#define MAINS_VOLT_RECOVERY       10.0f    /* 恢复回差 (V) */
#define MAINS_FREQ_LOW            45.0f
#define MAINS_FREQ_HIGH           55.0f
#define MAINS_FAIL_DEBOUNCE_MS    20U      /* 市电异常消抖 (ms) -> 转换时间 <10ms 要求 */
#define MAINS_GOOD_DEBOUNCE_MS    3000U    /* 市电恢复确认 (ms)，避免抖动反复切换 */

/*--------------------------------------------------------------------------
 * 保护阈值
 *--------------------------------------------------------------------------*/
#define PROT_OVERLOAD_WARN_PCT    105U     /* 过载告警 (%) */
#define PROT_OVERLOAD_125_MS      30000U   /* 125% 负载允许 30s */
#define PROT_OVERLOAD_150_MS      1000U    /* 150% 负载允许 1s */
#define PROT_OVERLOAD_200_MS      100U     /* 200% 负载允许 100ms */
#define PROT_SHORT_CIRCUIT_US     50U      /* 短路硬件保护 (由比较器触发, 软件确认) */
#define PROT_TEMP_WARN            75.0f    /* 散热器过温告警 (℃) */
#define PROT_TEMP_SHUTDOWN        90.0f    /* 过温关机 (℃) */
#define PROT_OUTPUT_VOLT_LOW      195.0f   /* 输出欠压保护 */
#define PROT_OUTPUT_VOLT_HIGH     250.0f   /* 输出过压保护 */

/*--------------------------------------------------------------------------
 * 功率器件 PWM 配置
 *--------------------------------------------------------------------------*/
#define INV_PWM_FREQ_HZ           20000U   /* 逆变全桥载波 20kHz (TIM1) */
#define INV_PWM_PERIOD            (36000000U / INV_PWM_FREQ_HZ)  /* 中心对齐: 72M/2/20k */
#define INV_DEADTIME_NS           500U     /* 死区时间 */
#define INV_SINE_STEPS            200U     /* 正弦表每周期点数 */
#define INV_SOFT_START_MS         3000U    /* 逆变软启动时间 */

#define BOOST_PWM_FREQ_HZ         50000U   /* DC/DC 升压 50kHz (TIM2 CH1/CH2 推挽) */
#define CHG_PWM_FREQ_HZ           50000U   /* 充电 BUCK 50kHz (TIM3 CH1) */

/*--------------------------------------------------------------------------
 * ADC 通道映射 (ADC1, DMA 循环扫描)
 *--------------------------------------------------------------------------*/
#define ADC_CH_BUS_VOLT           0   /* PA0 直流母线电压 (分压 1/150) */
#define ADC_CH_BAT_VOLT           1   /* PA1 电池电压 (分压 1/11) */
#define ADC_CH_OUT_VOLT           2   /* PA2 输出电压 (差分采样 1/150) */
#define ADC_CH_OUT_CURR           3   /* PA3 输出电流 (电流互感器+偏置) */
#define ADC_CH_MAINS_VOLT         4   /* PA4 市电电压 (差分采样 1/150) */
#define ADC_CH_NTC_TEMP           5   /* PA5 散热器 NTC */
#define ADC_CH_CHG_CURR           6   /* PA6 充电电流 (采样电阻+运放) */
#define ADC_NUM_CHANNELS          7

/* 采样比例换算系数 (实际值 = ADC电压 * SCALE) */
#define ADC_VREF                  3.3f
#define ADC_FULL_SCALE            4095.0f
#define SCALE_BUS_VOLT            150.0f
#define SCALE_BAT_VOLT            11.0f
#define SCALE_OUT_VOLT            150.0f
#define SCALE_OUT_CURR            3.0f     /* A/V */
#define SCALE_MAINS_VOLT          150.0f
#define SCALE_CHG_CURR            2.5f     /* A/V */
#define AC_SIGNAL_BIAS            1.65f    /* 交流信号直流偏置 (V) */

#define ADC_SAMPLE_AVG            8U       /* 每通道滑动平均点数 */

/*--------------------------------------------------------------------------
 * GPIO 映射
 *--------------------------------------------------------------------------*/
/* 继电器驱动 */
#define PIN_RELAY_INPUT           5   /* PB5  市电输入继电器 */
#define PIN_RELAY_BYPASS          6   /* PB6  旁路继电器 */
#define PIN_RELAY_OUTPUT          7   /* PB7  输出继电器 */
/* 过零检测 */
#define PIN_MAINS_ZC              0   /* PB0  EXTI0 市电过零 */
#define PIN_INV_SYNC_ZC           1   /* PB1  EXTI1 输出过零(同步校验) */
/* 人机界面 */
#define PIN_LED_LINE              0   /* PC0  市电指示灯(绿) */
#define PIN_LED_INV               1   /* PC1  逆变指示灯(绿) */
#define PIN_LED_FAULT             2   /* PC2  故障指示灯(红) */
#define PIN_LED_BAT_LOW           3   /* PC3  电池低电位灯(黄) */
#define PIN_BUZZER                8   /* PB8  蜂鸣器 PWM */
#define PIN_FAN                   9   /* PB9  风扇控制 */
#define PIN_KEY_ONOFF             10  /* PC10 开关键 */
#define PIN_KEY_TEST              11  /* PC11 自检/消音键 */
/* 通信 */
#define COMM_UART_INSTANCE        3   /* USART3: PB10(TX) PB11(RX), 2400 8N1 */

#define COMM_BAUDRATE             2400U

/*--------------------------------------------------------------------------
 * 任务调度周期
 *--------------------------------------------------------------------------*/
#define TASK_FAST_MS              1U     /* 保护/波形控制快环 */
#define TASK_CTRL_MS              10U    /* 闭环控制/状态机 */
#define TASK_SLOW_MS              100U   /* 电池/温度/风扇 */
#define TASK_HMI_MS               250U   /* 显示/蜂鸣器 */
#define TASK_COMM_MS              50U    /* 通信协议处理 */

#endif /* UPS_CONFIG_H */
