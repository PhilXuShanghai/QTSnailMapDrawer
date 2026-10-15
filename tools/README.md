# RobTester

模拟机器人 TCP 服务端，用于 QTSnailMapDrawer 联调。

## 功能

- TCP 服务端，支持多客户端并发接入（每客户端独立线程）
- 协议：4 字节大端长度前缀 + 数据体
- 心跳：收到 `PING` 回复 `PONG`
- 客户端接入后主动推送 `hello` 消息，验证下行链路
- 业务数据：尝试按 JSON 解析并回 `ack`，非 JSON 也回 `ack`（带字节数）
- 支持 `TCP_NODELAY`，降低小包延迟
- GBK / UTF-8 混合编码兼容

## 参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--ip` | `192.168.105.10` | 监听 IP |
| `--port` | `9200` | 监听端口 |

## 运行

```bash
# 默认：监听 192.168.105.10:9200
python simulator.py

# 本机回环
python simulator.py --ip 127.0.0.1

# 自定义端口
python simulator.py --ip 127.0.0.1 --port 9300
```

## 依赖

无第三方依赖，仅需 Python 3.6+ 标准库（socket / struct / threading / json / argparse）。

## 联调

1. 启动 RobTester，确认监听地址与端口。
2. 在 QTSnailMapDrawer 中输入相同 IP 与端口，点击连接。
3. 观察 RobTester 控制台日志，确认 `hello`、`PING/PONG`、业务 `ack` 正常。