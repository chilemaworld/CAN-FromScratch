#!/bin/bash

# 脚本名称：stress_test_can.sh
# 功    能：模拟高频 CAN 报文洪水，测试 RingBuffer 稳定性
# 使用方法：./stress_test_can.sh [间隔时间ms]

INTERFACE="vcan0"
INTERVAL=${1:-1}  # 如果不传参数，默认间隔 1ms

echo "--------------------------------------------------"
echo "BSW Stress Test Tooling"
echo "Target Interface: $INTERFACE"
echo "Sending Interval: $INTERVAL ms"
echo "Press [Ctrl+C] to stop the flood."
echo "--------------------------------------------------"

# 检查接口是否存在
if ! ip link show $INTERFACE > /dev/null 2>&1; then
    echo "Error: Interface $INTERFACE not found!"
    exit 1
fi

# 使用 cangen 进行压力测试
# -g: 指定间隔时间 (ms)
# -i: 随机生成 CAN ID
# -L 8: 固定数据长度为 8 字节（内容会自动随机）
# -x: 如果想看发送的统计，可以加上这个，但压测时不建议
cangen $INTERFACE -g $INTERVAL -i -L 8