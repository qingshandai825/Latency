# Latency

实习期间的网络时延相关 Qt/C++ 项目源码，保留了原有的多个工程目录。

| 目录 | 内容 |
| --- | --- |
| `project_B/` | 包含转发模块的 Qt 工程（`projectB.pro`） |
| `receiver_window/` | 接收端 Qt 工程（`receiver_window.pro`） |
| `TSM/`、`TSM1/` | 发送端和控制器的两个工程版本（`two_process_sender.pro`） |
| `test_build_0712/` | 原有的源码检查脚本；脚本中保留了旧机器的绝对路径，运行前需调整 |

可以在 Qt Creator 中打开对应的 `.pro` 文件。工程使用 Qt 的 Core、GUI、Widgets、Network、Charts 等模块，部分代码需要 Concurrent 模块。`build/` 目录和 `.pro.user` 文件是本机生成内容，未纳入版本控制。
