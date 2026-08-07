/**
 * @file    ups_fsm.c
 * @brief   UPS 主状态机实现
 *
 * 状态迁移图:
 *   POWER_ON -> STANDBY (自检通过)
 *   STANDBY -> LINE_MODE (市电正常 + 开机键)
 *   LINE_MODE <-> BATTERY_MODE (市电异常消抖后切换, 逆变已热备锁相, <4ms)
 *   LINE/BATTERY -> BYPASS (过载可恢复场景)
 *   任意 -> FAULT (故障锁存) -> STANDBY (故障清除+复位键)
 *   BATTERY_MODE -> SHUTDOWN (电池耗尽) -> 市电恢复自动重启
 *
 * 继电器动作时序 (转电池): 断输入继电器 -> 2ms 死区 -> 逆变接管输出
 *               (转市电): 确认锁相同步 -> 合旁路 -> 2ms -> 停逆变
 */
#include "ups_fsm.h"
#include "ups_config.h"
#include "inverter.h"
#include "charger.h"
#include "battery.h"

extern uint32_t HAL_GetTick(void);
/* GPIO 抽象 (bsp.c) */
extern void BSP_Relay(uint8_t pin, bool on);
extern void BSP_Fan(bool on);

static void Enter_State(UPS_State_t s);

static void Enter_State(UPS_State_t s)
{
    g_ups.prev_state = g_ups.state;
    g_ups.state = s;
    g_ups.state_enter_ms = HAL_GetTick();
}

void FSM_Init(void)
{
    Enter_State(UPS_STATE_POWER_ON);
    BSP_Relay(PIN_RELAY_INPUT, false);
    BSP_Relay(PIN_RELAY_BYPASS, false);
    BSP_Relay(PIN_RELAY_OUTPUT, false);
}

void FSM_CtrlTask(void)
{
    uint32_t now = HAL_GetTick();
    bool mains_ok = (g_ups.mains_status == MAINS_GOOD);

    /* 风扇: 逆变运行或温度>45℃ 开启 */
    BSP_Fan(g_ups.inverter_on || g_ups.meas.heatsink_temp > 45.0f);

    /* 最高优先级: 故障锁存 */
    if (g_ups.fault_flags != FAULT_NONE && g_ups.state != UPS_STATE_FAULT &&
        g_ups.state != UPS_STATE_SHUTDOWN) {
        Inverter_Stop();
        Boost_Stop();
        /* 市电正常时转旁路维持输出, 否则全断 */
        BSP_Relay(PIN_RELAY_BYPASS, mains_ok && (g_ups.fault_flags & FAULT_OVERLOAD));
        BSP_Relay(PIN_RELAY_INPUT, false);
        Enter_State(UPS_STATE_FAULT);
        return;
    }

    switch (g_ups.state) {

    case UPS_STATE_POWER_ON:
        /* 自检: 采样稳定 500ms, 母线预充 */
        if (now - g_ups.state_enter_ms >= 500U) {
            Boost_Start();   /* 建立母线 */
            Enter_State(UPS_STATE_STANDBY);
        }
        break;

    case UPS_STATE_STANDBY:
        if (mains_ok) {
            Charger_Enable();
            /* 等待开机键 (HMI 置位 inverter 请求) 或默认自动开机 */
            Inverter_Start();              /* 逆变热备, 锁相市电 */
            BSP_Relay(PIN_RELAY_INPUT, true);
            BSP_Relay(PIN_RELAY_BYPASS, true);
            BSP_Relay(PIN_RELAY_OUTPUT, true);
            Enter_State(UPS_STATE_LINE_MODE);
        }
        break;

    case UPS_STATE_LINE_MODE:
        if (g_ups.self_test_running || !mains_ok) {
            /* 转电池: 先断市电通路, 逆变无缝接管 (已锁相) */
            BSP_Relay(PIN_RELAY_BYPASS, false);
            BSP_Relay(PIN_RELAY_INPUT, false);
            Charger_Disable();
            Enter_State(UPS_STATE_BATTERY_MODE);
        }
        break;

    case UPS_STATE_BATTERY_MODE:
        if (g_ups.self_test_running) {
            /* 自检中: 10s 后由 battery 模块清除标志, 自动回市电 */
            if (!g_ups.self_test_running) break;   /* 防御性, 实际走下一分支 */
        }
        if (mains_ok && !g_ups.self_test_running) {
            /* 转市电: 必须锁相同步才能合旁路 */
            if (Inverter_IsSynced()) {
                BSP_Relay(PIN_RELAY_INPUT, true);
                BSP_Relay(PIN_RELAY_BYPASS, true);
                Enter_State(UPS_STATE_LINE_MODE);
                Charger_Enable();
            }
        }
        break;

    case UPS_STATE_BYPASS:
        /* 旁路: 逆变关闭, 市电直通; 条件消除后回 LINE */
        if (!mains_ok) {
            BSP_Relay(PIN_RELAY_BYPASS, false);
            Inverter_Start();
            Enter_State(UPS_STATE_BATTERY_MODE);
        }
        break;

    case UPS_STATE_FAULT:
        /* 故障锁存: 等待人工复位 (HMI 长按开关键清 fault_flags) */
        if (g_ups.fault_flags == FAULT_NONE) {
            Enter_State(UPS_STATE_STANDBY);
        }
        break;

    case UPS_STATE_SHUTDOWN:
        Inverter_Stop();
        Boost_Stop();
        Charger_Disable();
        BSP_Relay(PIN_RELAY_INPUT, false);
        BSP_Relay(PIN_RELAY_BYPASS, false);
        BSP_Relay(PIN_RELAY_OUTPUT, false);
        /* 市电恢复 -> 自动重启 (冷启动延时 10s 等电池回压) */
        if (mains_ok && (now - g_ups.state_enter_ms) >= 10000U) {
            g_ups.fault_flags = FAULT_NONE;
            Enter_State(UPS_STATE_POWER_ON);
        }
        break;

    default:
        Enter_State(UPS_STATE_POWER_ON);
        break;
    }

    /* 电池耗尽 -> 关机流程 */
    if ((g_ups.fault_flags & FAULT_BAT_LOW) && g_ups.state != UPS_STATE_SHUTDOWN) {
        Enter_State(UPS_STATE_SHUTDOWN);
    }

    /* 运行计时 */
    if (g_ups.state != UPS_STATE_POWER_ON) {
        g_ups.run_seconds = (now / 1000U);
    }
}

void FSM_RequestShutdown(void)
{
    Enter_State(UPS_STATE_SHUTDOWN);
}

const char *FSM_StateName(UPS_State_t s)
{
    static const char *names[] = {
        "POWER_ON", "STANDBY", "LINE_MODE",
        "BATTERY_MODE", "BYPASS", "FAULT", "SHUTDOWN"
    };
    return (s <= UPS_STATE_SHUTDOWN) ? names[s] : "UNKNOWN";
}
