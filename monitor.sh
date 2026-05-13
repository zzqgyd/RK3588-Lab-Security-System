#!/bin/bash
# RK3588 硬件状态实时监控

watch -n 1 '
echo "=== NPU 利用率 ==="
cat /sys/kernel/debug/rknpu/load 2>/dev/null || echo "节点不存在"

echo ""
echo "=== RGA 负载 ==="
cat /sys/kernel/debug/rkrga/load 2>/dev/null || echo "节点不存在"

echo ""
echo "=== DDR 带宽 ==="
freq=$(cat /sys/class/devfreq/dmc/cur_freq 2>/dev/null)
load=$(cat /sys/class/devfreq/dmc/load 2>/dev/null)
if [ -n "$freq" ] && [ -n "$load" ]; then
    echo "频率: $(( freq / 1000000 )) MHz"
    echo "负载: ${load%%@*}%"
else
    echo "节点不存在"
fi

echo ""
echo "=== GPU ==="
gpu=$(cat /sys/devices/platform/fb000000.gpu/devfreq/fb000000.gpu/load 2>/dev/null)
if [ -n "$gpu" ]; then
    gpu_freq=${gpu#*@}
    gpu_freq=${gpu_freq%Hz}
    echo "频率: $(( gpu_freq / 1000000 )) MHz"
    echo "负载: ${gpu%%@*}%"
else
    echo "节点不存在"
fi

echo ""
echo "=== VPU 会话 ==="
dec_count=0
enc_count=0
while IFS= read -r line; do
    if echo "$line" | grep -q "device:"; then
        if echo "$line" | grep -q "rkvdec"; then
            dec_count=$((dec_count + 1))
        elif echo "$line" | grep -q "rkvenc"; then
            enc_count=$((enc_count + 1))
        fi
    fi
done < /proc/mpp_service/sessions-summary 2>/dev/null
echo "  解码: ${dec_count} 路, 编码: ${enc_count} 路"
echo ""
echo "=== CPU 温度: $(( $(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 0) / 1000 ))°C ==="
'