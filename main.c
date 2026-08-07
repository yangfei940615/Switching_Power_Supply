#include <stdio.h>

// 开关电源 Buck 变换器控制算法示例
typedef struct {
    float input_voltage;   // 输入电压 (V)
    float output_voltage;  // 目标输出电压 (V)
    float duty_cycle;      // 占空比 (0~1)
    float current;         // 电流 (A)
} BuckConverter;

// 计算占空比（简单比例控制）
float calculate_duty_cycle(BuckConverter *buck) {
    return buck->output_voltage / buck->input_voltage;
}

int main() {
    BuckConverter buck = {24.0, 12.0, 0.0, 0.0};
    buck.duty_cycle = calculate_duty_cycle(&buck);
    printf("Buck Converter Duty Cycle: %.2f%%\n", buck.duty_cycle * 100);
    return 0;
}
