/**
 * @file    bsp.h
 * @brief   板级支持包接口 (STM32F103 HAL 实现见 bsp.c)
 */
#ifndef BSP_H
#define BSP_H

#include "ups_types.h"

/* 系统初始化 */
void BSP_SystemClock_Init(void);
void BSP_GPIO_Init(void);
void BSP_ADC1_DMA_Init(uint16_t *buf, uint16_t len);
void BSP_TIM_PWM_Init(void);
void BSP_USART3_Init(uint32_t baud);
void BSP_EXTI_Init(void);
void BSP_WDG_Init(void);
void BSP_WDG_Feed(void);

/* GPIO 操作 */
void BSP_Relay(uint8_t pin, bool on);
void BSP_LED(uint8_t pin, bool on);
void BSP_Buzzer(bool on);
void BSP_Fan(bool on);
bool BSP_KeyPressed(uint8_t pin);

/* PWM 输出 */
void PWM_TIM1_SetDuty(uint16_t ch1, uint16_t ch2);
void PWM_TIM1_Start(void);
void PWM_TIM1_Stop(void);
void PWM_TIM2_SetDuty(uint16_t ch1, uint16_t ch2);
void PWM_TIM2_Start(void);
void PWM_TIM2_Stop(void);
void PWM_TIM3_SetDuty(uint16_t cmp);
void PWM_TIM3_Start(void);
void PWM_TIM3_Stop(void);

/* 串口 */
void BSP_UART3_Send(const uint8_t *data, uint16_t len);
uint8_t BSP_USART3_ReadByte(void);

#endif /* BSP_H */
