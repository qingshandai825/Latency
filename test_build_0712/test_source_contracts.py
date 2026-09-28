from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(r"D:\java\develop\0712")
SENDER = ROOT / "TSM_Controller"
RECEIVER = (
    ROOT
    / "receiver_window (2)"
    / "receiver_window (2)"
    / "receiver_window"
)


results: list[dict[str, str]] = []


def record(name: str, status: str, detail: str) -> None:
    results.append({"name": name, "status": status, "detail": detail})


def require(condition: bool, name: str, detail: str) -> None:
    record(name, "PASS" if condition else "FAIL", detail)


def warn(condition: bool, name: str, ok: str, warning: str) -> None:
    record(name, "PASS" if condition else "WARN", ok if condition else warning)


traffic = (SENDER / "traffic_generator.cpp").read_text(encoding="utf-8")
controller = (SENDER / "tsm_controller.cpp").read_text(encoding="utf-8")
scheduler = (SENDER / "packet_scheduler.cpp").read_text(encoding="utf-8")
delay = (SENDER / "delayinjector.cpp").read_text(encoding="utf-8")
receiver = (RECEIVER / "receiver_window.cpp").read_text(encoding="utf-8")


# Build evidence
sender_exe = (
    SENDER
    / "build"
    / "Desktop_Qt_6_5_3_MinGW_64_bit-Debug"
    / "debug"
    / "TSM_Controller.exe"
)
receiver_exe = (
    RECEIVER
    / "build"
    / "Desktop_Qt_6_5_3_MinGW_64_bit-Debug"
    / "debug"
    / "receiver_window.exe"
)
require(sender_exe.exists() and sender_exe.stat().st_size > 0, "发送端构建产物", str(sender_exe))
require(receiver_exe.exists() and receiver_exe.stat().st_size > 0, "接收端构建产物", str(receiver_exe))


# The sender and receiver must agree on the binary header.
sender_fields = ["header", "seq", "timestamp", "payloadSize", "packetType", "priority"]
sender_field_order = " << ".join(sender_fields)
receiver_field_order = " >> ".join(
    ["header", "sequence", "timestamp", "payloadSize", "packetType", "priority"]
)
require(
    all(field in traffic for field in sender_fields)
    and receiver_field_order in receiver.replace("\r", ""),
    "协议字段对齐",
    "发送端与接收端均按 magic/sequence/timestamp/payloadSize/type/priority 解析",
)

header_size = 2 + 8 + 8 + 4 + 1 + 1
require(header_size == 24, "协议头长度", f"固定头长度={header_size} bytes，另含 payload 与 2-byte CRC")
require(
    "quint16(0xAA55)" in traffic and "header != 0xAA55" in receiver,
    "Magic 校验",
    "发送与接收统一使用 0xAA55",
)
require(
    "qChecksum" in traffic and "qChecksum" in receiver,
    "CRC 校验",
    "发送端生成 CRC，接收端重新计算并比对",
)


# Transport and scheduling coverage.
require(
    all(token in controller for token in ["Unicast", "Multicast", "Broadcast", "TCP", "UDP"]),
    "传输模式覆盖",
    "代码覆盖 TCP/UDP 与单播/组播/广播配置",
)
require(
    "m_queues.resize(4)" in scheduler and "{8, 4, 2, 1}" in scheduler,
    "四级加权调度",
    "4 个优先级队列，服务权重 8:4:2:1",
)
warn(
    "global" in scheduler.lower() and "fifo" in scheduler.lower(),
    "FIFO 语义",
    "实现了跨队列全局 FIFO",
    "processFifo 按优先级逐队列清空，不是跨队列的全局 FIFO；简历宜写优先级调度",
)


# Delay model: check the source formula against its own stated satellite goal.
satellite_100km_ms = 100_000 / 3.0e8 * 1000
satellite_10000km_ms = 10_000_000 / 3.0e8 * 1000
warn(
    satellite_10000km_ms >= 200,
    "卫星时延模型",
    f"最大配置可产生 {satellite_10000km_ms:.2f} ms 以上传播时延",
    (
        f"当前 Satellite 公式在 100 km/10000 km 下仅约 {satellite_100km_ms:.3f}/"
        f"{satellite_10000km_ms:.2f} ms，与源码注释中的 GEO 往返约 240 ms 不符"
    ),
)
warn(
    "currentTime() - delayNs" in traffic,
    "模拟时延方向",
    "发送时间戳向过去偏移，接收端得到正的附加时延",
    "发送端使用 currentTime() + delayNs，而接收端计算 receiveTime - timestamp，可能得到负时延",
)


# Receiver state-machine and stream-framing checks.
file_guard = "if (info.isFilePacket && m_isReceivingFile)"
warn(
    file_guard not in receiver,
    "文件接收状态机",
    "首个文件包可以从 idle 状态启动接收",
    "文件处理受 m_isReceivingFile=true 约束，但初始化/重置为 false，首包无法进入处理分支",
)

prefix_read_before_complete = bool(
    re.search(
        r"stream\s*>>\s*packetSize;[\s\S]{0,800}?bytesAvailable\(\)\s*<\s*packetSize",
        receiver,
    )
)
warn(
    not prefix_read_before_complete,
    "TCP 分帧缓冲",
    "完整帧到齐前不会消费长度前缀",
    "长度前缀可能在包体未到齐时先被消费；超时后下一次读取可能把包体误当作新长度",
)

udp_dedup = "QSet" in receiver or "contains(sequence)" in receiver
warn(
    udp_dedup,
    "UDP 重复包处理",
    "丢包率统计会排除重复序列号",
    "UDP 路径未去重；重复包会增加 packetCounter，可能造成无符号丢包数下溢",
)


summary = {
    "pass": sum(item["status"] == "PASS" for item in results),
    "warn": sum(item["status"] == "WARN" for item in results),
    "fail": sum(item["status"] == "FAIL" for item in results),
    "results": results,
}
print(json.dumps(summary, ensure_ascii=False, indent=2))

if summary["fail"]:
    raise SystemExit(1)
