# Switching_Power_Supply

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

基于C语言开发的开关电源数字控制算法库，提供电源拓扑建模、PID闭环控制及PWM波形生成等核心功能。

## ✨ 核心特性

- **电源拓扑建模**：支持 Buck、Boost、Buck-Boost 等常用拓扑的数学模型
- **数字控制算法**：包含 PID、数字补偿器等核心控制模块
- **PWM 波形生成**：提供占空比计算与死区时间管理功能
- **嵌入式友好**：代码专为 MCU 平台优化，资源占用小
- **注释完整**：所有函数均提供详细的接口说明

## 🚀 快速开始

### 环境要求
- C 编译器（GCC / Clang / ARMCC）
- CMake 3.10+（可选）

### 编译与运行
```bash
git clone https://github.com/yangfei940615/Switching_Power_Supply.git
cd Switching_Power_Supply
make
./build/example_buck
