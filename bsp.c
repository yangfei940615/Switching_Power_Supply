/**
 * @file    bsp.c
 * @brief   板级支持包: STM32F103RCT6 HAL 层实现
 * @note    依赖 STM32CubeF1 HAL 库; 引脚定义见 ups_config.h
 */
#include "bsp.h"
#include "ups_config.h"
#include "stm32f1xx_hal.h"

static ADC_HandleTypeDef  hadc1;
static DMA_HandleTypeDef  hdma_adc1;
static TIM_HandleTypeDef  htim1, htim2, htim3;
static UART_HandleTypeDef huart3;
static IWDG_HandleTypeDef hiwdg;

/* ================= 时钟: HSE 8M -> PLL x9 -> 72MHz ================= */
void BSP_SystemClock_Init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;        /* 8M * 9 = 72M */
    osc.LSIState = RCC_LSI_ON;            /* 独立看门狗时钟 */
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;   /* 36MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;   /* 72MHz (ADC/ TIM1) */
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000U);   /* 1ms SysTick */
    HAL_NVIC_SetPriority(SysTick_IRQn, 3, 0);
}

/* ================= GPIO ================= */
void BSP_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* 继电器 PB5/PB6/PB7, 蜂鸣器 PB8, 风扇 PB9: 推挽输出 */
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, g.Pin, GPIO_PIN_RESET);

    /* LED PC0~PC3 */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOC, &g);

    /* 按键 PC10/PC11: 上拉输入, 按下为低 */
    g.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &g);

    /* ADC 输入 PA0~PA6: 模拟 */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
            GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    g.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &g);
}

void BSP_Relay(uint8_t pin, bool on)
{
    HAL_GPIO_WritePin(GPIOB, (uint16_t)(1U << pin),
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_LED(uint8_t pin, bool on)
{
    HAL_GPIO_WritePin(GPIOC, (uint16_t)(1U << pin),
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_Buzzer(bool on)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_Fan(bool on)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool BSP_KeyPressed(uint8_t pin)
{
    return HAL_GPIO_ReadPin(GPIOC, (uint16_t)(1U << (pin & 0x0FU))) == GPIO_PIN_RESET;
}

/* ================= ADC1 + DMA 循环扫描 ================= */
void BSP_ADC1_DMA_Init(uint16_t *buf, uint16_t len)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_adc1);

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = len;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef ch = {0};
    ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    /* 通道号 = 引脚号 (PA0=ADC_IN0 ... PA6=ADC_IN6), rank 顺序即 config 定义顺序 */
    for (uint32_t i = 0; i < len; i++) {
        ch.Channel = i;
        ch.Rank = i + 1;
        HAL_ADC_ConfigChannel(&hadc1, &ch);
    }

    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)buf, len);
}

/* ================= PWM: TIM1 逆变 / TIM2 升压 / TIM3 充电 ================= */
void BSP_TIM_PWM_Init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* ---- TIM1: 中心对齐 20kHz, CH1/CH1N (PA8/PB13), CH2/CH2N (PA9/PB14) ---- */
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
    htim1.Init.Period = INV_PWM_PERIOD;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);

    /* 死区 + 刹车 (硬件过流比较器接 BKIN PB12) */
    TIM_BreakDeadTimeConfigTypeDef bd = {0};
    bd.DeadTime = (uint8_t)(INV_DEADTIME_NS * 72U / 1000U);  /* 72MHz -> 每级13.9ns */
    bd.BreakState = TIM_BREAK_ENABLE;
    bd.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd);

    /* PA8/PA9 复用推挽, PB13/PB14 互补输出 */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &g);
    /* BKIN PB12 输入 */
    g.Pin = GPIO_PIN_12;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &g);

    /* 更新中断: 最高软件优先级 */
    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
    HAL_NVIC_SetPriority(TIM1_BRK_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(TIM1_BRK_IRQn);

    /* ---- TIM2: 50kHz 推挽 CH1(PA0 不可用!) ----
       注意: PA0 已用作 ADC, TIM2 重映射到 PA15/PB3 或改用 TIM4.
       此处选用 TIM4 (PB6 冲突继电器!) -> 最终方案: TIM2 部分重映射 CH1->PA15 CH2->PB3 */
    __HAL_AFIO_REMAP_TIM2_PARTIAL_1();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = (72000000U / BOOST_PWM_FREQ_HZ) - 1U;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim2);
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_2);
    /* PA15/PB3 需先禁 JTAG 释放引脚 */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
    g.Pin = GPIO_PIN_15;
    g.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOB, &g);

    /* ---- TIM3 CH1 (PA6 冲突 ADC!) -> 完全重映射到 PC6 ---- */
    __HAL_AFIO_REMAP_TIM3_ENABLE();
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = (72000000U / CHG_PWM_FREQ_HZ) - 1U;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim3);
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    g.Pin = GPIO_PIN_6;
    g.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOC, &g);
}

void PWM_TIM1_SetDuty(uint16_t ch1, uint16_t ch2)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ch1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ch2);
}

void PWM_TIM1_Start(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
    __HAL_TIM_MOE_ENABLE(&htim1);
}

void PWM_TIM1_Stop(void)
{
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
}

void PWM_TIM2_SetDuty(uint16_t ch1, uint16_t ch2)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ch1);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ch2);
}

void PWM_TIM2_Start(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
}

void PWM_TIM2_Stop(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_2);
}

void PWM_TIM3_SetDuty(uint16_t cmp)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, cmp);
}

void PWM_TIM3_Start(void) { HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1); }
void PWM_TIM3_Stop(void)  { HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1); }

/* ================= USART3: PB10 TX / PB11 RX ================= */
void BSP_USART3_Init(uint32_t baud)
{
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_11;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    huart3.Instance = USART3;
    huart3.Init.BaudRate = baud;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);

    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void BSP_UART3_Send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)data, len, 100);
}

uint8_t BSP_USART3_ReadByte(void)
{
    return (uint8_t)(huart3.Instance->DR & 0xFFU);
}

/* ================= EXTI: PB0 市电过零 / PB1 输出过零 ================= */
void BSP_EXTI_Init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode = GPIO_MODE_IT_RISING_FALLING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

/* ================= 独立看门狗 (1s) ================= */
void BSP_WDG_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;   /* LSI 40kHz / 32 = 1.25kHz */
    hiwdg.Init.Reload = 1250;                    /* 1s */
    HAL_IWDG_Init(&hiwdg);
}

void BSP_WDG_Feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

/* ================= HAL 中断回调桥接 =================
   注意: main.c 中的裸 IRQHandler 与 HAL 回调二选一, 本工程 main.c
   直接实现 IRQHandler, 若在 HAL 工程中使用请将 IRQHandler 删除并
   改用以下回调 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    extern void Inverter_SPWM_ISR(void);
    if (htim->Instance == TIM1) Inverter_SPWM_ISR();
}
