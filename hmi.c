/**
 * @file    hmi.c
 * @brief   人机界面实现
 *
 * 蜂鸣器音型 (250ms 节拍):
 *   电池模式    : 每 4s 响 1 拍
 *   电池低电位  : 每 1s 响 1 拍
 *   过载        : 每 0.5s 响 1 拍
 *   故障        : 长鸣
 *   自检中      : 每 2s 响 1 拍
 * 按键:
 *   ON/OFF 长按 2s : 开/关机请求
 *   TEST  短按     : 消音 / 故障状态长按 3s 清除故障
 */
#include "hmi.h"
#include "ups_config.h"
#include "ups_fsm.h"
#include "battery.h"

extern uint32_t HAL_GetTick(void);
extern void BSP_LED(uint8_t pin, bool on);
extern void BSP_Buzzer(bool on);
extern bool BSP_KeyPressed(uint8_t pin);

static uint32_t s_beat = 0;          /* 250ms 节拍计数 */
static uint32_t s_onoff_hold_ms = 0;
static uint32_t s_test_hold_ms = 0;

void HMI_Init(void)
{
    s_beat = 0;
    g_ups.buzzer_mute = false;
}

void HMI_Task(void)
{
    s_beat++;
    bool beep = false;

    /* ---- LED ---- */
    BSP_LED(PIN_LED_LINE,   g_ups.mains_status == MAINS_GOOD);
    BSP_LED(PIN_LED_INV,    g_ups.inverter_on);
    BSP_LED(PIN_LED_FAULT,  g_ups.fault_flags != FAULT_NONE);
    BSP_LED(PIN_LED_BAT_LOW, g_ups.warning_flags & WARN_BAT_LOW);

    /* ---- 蜂鸣器 ---- */
    if (g_ups.buzzer_mute && g_ups.fault_flags == FAULT_NONE) {
        beep = false;
    } else if (g_ups.fault_flags != FAULT_NONE) {
        beep = true;                                  /* 故障长鸣 */
    } else if (g_ups.warning_flags & WARN_OVERLOAD) {
        beep = (s_beat % 2U) == 0;                    /* 0.5s 间隔 */
    } else if (g_ups.warning_flags & WARN_BAT_LOW) {
        beep = (s_beat % 4U) == 0;                    /* 1s 间隔 */
    } else if (g_ups.state == UPS_STATE_BATTERY_MODE) {
        beep = (s_beat % 16U) == 0;                   /* 4s 间隔 */
    } else if (g_ups.self_test_running) {
        beep = (s_beat % 8U) == 0;                    /* 2s 间隔 */
    }
    BSP_Buzzer(beep);
}

void HMI_KeyScan(void)
{
    /* ON/OFF 长按 2s */
    if (BSP_KeyPressed(PIN_KEY_ONOFF)) {
        s_onoff_hold_ms += TASK_CTRL_MS;
        if (s_onoff_hold_ms >= 2000U) {
            s_onoff_hold_ms = 0;
            if (g_ups.state == UPS_STATE_STANDBY) {
                /* 状态机 STANDBY 分支会自动开机 */
            } else {
                FSM_RequestShutdown();
            }
        }
    } else {
        s_onoff_hold_ms = 0;
    }

    /* TEST 键 */
    if (BSP_KeyPressed(PIN_KEY_TEST)) {
        s_test_hold_ms += TASK_CTRL_MS;
        if (s_test_hold_ms >= 3000U && g_ups.fault_flags != FAULT_NONE) {
            g_ups.fault_flags = FAULT_NONE;           /* 故障复位 */
            s_test_hold_ms = 0;
        }
    } else {
        if (s_test_hold_ms > 0 && s_test_hold_ms < 3000U) {
            /* 短按: 消音 / 市电模式下触发自检 */
            if (g_ups.warning_flags != WARN_NONE) {
                g_ups.buzzer_mute = true;
            } else if (g_ups.state == UPS_STATE_LINE_MODE) {
                Battery_StartSelfTest();
            }
        }
        s_test_hold_ms = 0;
    }
}
