/**
 * @file    main.c
 * @brief   1KVA UPS 主程序: 外设初始化 + 协作式任务调度
 *
 * 任务时序 (SysTick 1ms 驱动):
 *   1ms   : Sensing_FastTask / Protection_FastTask
 *   10ms  : Sensing_CtrlTask / Mains_CtrlTask / Inverter_CtrlTask
 *           Boost_CtrlTask / Charger_CtrlTask / FSM_CtrlTask / HMI_KeyScan
 *   50ms  : Comm_Task
 *   100ms : Charger_SlowTask / Battery_SlowTask / Battery_SelfTestTask
 *   250ms : HMI_Task
 *
 * 中断:
 *   TIM1 UPDATE 20kHz : Inverter_SPWM_ISR  (最高优先级)
 *   TIM1 BKIN         : Inverter_HwFaultISR (硬件过流, 异步最高)
 *   EXTI0             : Mains_ZC_ISR (市电过零)
 *   USART3 RXNE       : Comm_RxByte
 */
#include "ups_config.h"
#include "ups_types.h"
#include "sensing.h"
#include "inverter.h"
#include "charger.h"
#include "mains.h"
#include "protection.h"
#include "battery.h"
#include "ups_fsm.h"
#include "comm.h"
#include "hmi.h"

/* ---- 全局上下文 ---- */
UPS_Context_t g_ups;

/* ---- ADC DMA 缓冲 (sensing.c 引用) ---- */
uint16_t g_adc_dma_buf[ADC_NUM_CHANNELS];

/* ---- SysTick 节拍 ---- */
static volatile uint32_t s_tick_ms = 0;

uint32_t HAL_GetTick(void)
{
    return s_tick_ms;
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}

/* CubeMX 生成的初始化 (bsp.c / stm32cube 工程提供) */
extern void BSP_SystemClock_Init(void);
extern void BSP_GPIO_Init(void);
extern void BSP_ADC1_DMA_Init(uint16_t *buf, uint16_t len);
extern void BSP_TIM_PWM_Init(void);
extern void BSP_USART3_Init(uint32_t baud);
extern void BSP_EXTI_Init(void);
extern void BSP_WDG_Init(void);
extern void BSP_WDG_Feed(void);

int main(void)
{
    /* ---- 硬件初始化 ---- */
    BSP_SystemClock_Init();          /* HSE 8M -> PLL -> 72MHz */
    BSP_GPIO_Init();                 /* 继电器/LED/按键/蜂鸣器/风扇 */
    BSP_ADC1_DMA_Init(g_adc_dma_buf, ADC_NUM_CHANNELS);
    BSP_TIM_PWM_Init();              /* TIM1 逆变 / TIM2 升压 / TIM3 充电 */
    BSP_USART3_Init(COMM_BAUDRATE);
    BSP_EXTI_Init();                 /* 市电过零 + 输出过零 */
    BSP_WDG_Init();                  /* 独立看门狗 1s, 主循环喂狗 */

    /* ---- 软件模块初始化 ---- */
    Sensing_Init();
    Inverter_Init();
    Boost_Init();
    Charger_Init();
    Mains_Init();
    Protection_Init();
    Battery_Init();
    Comm_Init();
    HMI_Init();
    FSM_Init();

    /* ---- 任务调度 ---- */
    uint32_t last_fast = 0, last_ctrl = 0, last_comm = 0, last_slow = 0, last_hmi = 0;

    for (;;) {
        uint32_t now = HAL_GetTick();

        if ((uint32_t)(now - last_fast) >= TASK_FAST_MS) {
            last_fast = now;
            Sensing_FastTask();
            Protection_FastTask();
        }
        if ((uint32_t)(now - last_ctrl) >= TASK_CTRL_MS) {
            last_ctrl = now;
            Sensing_CtrlTask();
            Mains_CtrlTask();
            Boost_CtrlTask();
            Inverter_CtrlTask();
            Charger_CtrlTask();
            FSM_CtrlTask();
            HMI_KeyScan();
        }
        if ((uint32_t)(now - last_comm) >= TASK_COMM_MS) {
            last_comm = now;
            Comm_Task();
        }
        if ((uint32_t)(now - last_slow) >= TASK_SLOW_MS) {
            last_slow = now;
            Charger_SlowTask();
            Battery_SlowTask();
            Battery_SelfTestTask();
        }
        if ((uint32_t)(now - last_hmi) >= TASK_HMI_MS) {
            last_hmi = now;
            HMI_Task();
        }

        BSP_WDG_Feed();
    }
}

/* ================= 中断向量挂接 ================= */

/** TIM1 更新中断: 20kHz SPWM 波形控制 */
void TIM1_UP_IRQHandler(void)
{
    /* TIM1->SR &= ~TIM_SR_UIF;  (BSP 层清标志) */
    Inverter_SPWM_ISR();
}

/** TIM1 刹车中断: 硬件过流/短路 */
void TIM1_BRK_IRQHandler(void)
{
    Inverter_HwFaultISR();
}

/** EXTI0: 市电过零 */
void EXTI0_IRQHandler(void)
{
    Mains_ZC_ISR();
}

/** USART3 接收中断 */
void USART3_IRQHandler(void)
{
    extern uint8_t BSP_USART3_ReadByte(void);
    Comm_RxByte(BSP_USART3_ReadByte());
}
