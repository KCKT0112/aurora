# 0.8.0-dev — 2026-10-29

- 根据实际宿主反馈删除启动后的自动窗口尺寸监听，消除反馈缩放循环；启动前选定分辨率，运行时仅显式resize。
- 增加macOS启动前NSScreen探针；已有显式分辨率参数继续支持。
- owner绑定VFS二进制句柄、pread/close、显式audio委托/撤销、64KiB一次性跨进程复制授权。
- 新增用户态WAV解析、PCM位深/声道转换、线性重采样和AC97有界DMA输出。
- 适配用户17.3MB、48kHz/24位/双声道WAV，完整2874403帧播放验证、零underrun。
- Terminal play/audio/stop、Files双击WAV；退出/注销停止播放。
- CoreAudio默认输出，录音通道警告只汇总一次，完整保留stderr；不隐藏播放错误。
- 新增任务优先级清单，后续重点持久化、完整fd语义与驱动隔离。

# 0.7.1-dev — 2026-10-29

- 多分辨率与2×原生绘制；宿主窗口尺寸→有界COM2提示→受限Standard VGA模式切换。
- 修复可见framebuffer提前擦除光标的合成空档，改为canvas光标叠加后提交。
- tablet与PS/2选择唯一指针源；保留SYN_REPORT按钮边沿，合并相邻motion。
- 圆角覆盖抗锯齿、精确字号图集、跳过阴影不可见内部区域、静态模型不重复重绘。
- 私有运行目录与QMP端点，`make paste`显式发送单行文本且不自动Enter。
- 加入动态分辨率、光标、粘贴启动器、绘制与消息协议回归。

# 0.7.0-dev — 2026-10-29

- ABI4将UID/GID/session接入进程、系统信息和peer credential查询；普通进程不能修改身份。
- 新增authd、密码登录、root/user/guest账号、失败退避、注销与旧会话资源清理。
- 首次构建私有随机密码，普通镜像与公开测试账号镜像隔离；不覆盖已有本地账号。
- initramfs v2（72字节节点）记录UID/GID；VFS执行DAC、逐级目录访问与sticky，增加stat/chmod。
- 终端增加id/whoami/logout/stat/chmod，home与提示符按当前账号；Monitor显示属主。
- 账号切换清理旧模型、窗口位置、按键和未提交事务；已提交RAM文件按用户隔离保留。
- make run默认宿主全屏与zoom-to-fit，RUN_FULLSCREEN=off窗口模式。
- SVG矢量源、覆盖抗锯齿、柔和衰减阴影、四边/四角resize、方向光标与最小尺寸。
- 增加认证/DAC/resize宿主机测试和user→root→guest QEMU回归。
- 明确沿用Unix/Linux接口思想、保留微内核方向；没有声称Linux二进制兼容。

# 0.6.0-dev — 2026-10-29

从原0.5桌面原型接续，保留原字体、绘图库、启动路径和用户媒体，重构启动/安全/桌面/文件服务基础。

- Makefile 收敛为通用用户程序规则；生成独立内核、用户 ELF、启动模块、initramfs 和系统 staging tree。
- ABI 3 增加显式授权、服务发现、系统信息、阻塞等待、受限进程控制、音频调用和VFS资源映射；ELF通过版本 note验证。
- 新增用户init和audiod；desktop移除spawn权限；程序退出后释放私有页、页表、邮箱、IRQ订阅及槽位。
- 物理分配从bump改为可回收位图；处理OOM、严格页映射和ELF输入验证。
- 统一启动权限清单并生成内核授权；UI更新按视图所有者核验，完整事务提交，拒绝内核事件伪造。
- 系统树调整到system/etc/home/var/tmp/run/dev/volumes；原音乐文件保留，打包不再静默跳过大媒体。
- VFS加入路径规范化、RAM覆盖写入、mkdir/remove、事务与容量限制。
- 新Terminal与System Monitor，支持基础命令、采样统计、受限应用重启。
- 新浅色工作区、侧栏、应用栏、统一窗口控制、最大化、滚动和深色终端；隐藏窗口不会被周期发布唤回。
- 增加宿主机ASan/UBSan、Python与边界测试、ring3权限和文件服务测试、16次资源回收测试、QEMU交互和PCM捕获。
- 更新全部组件README，新增权限、目录、应用、UI、驱动/更新设计与验证记录。

这是可运行的基础重构，不是完整可发行系统。完整用户态硬件驱动、持久化存储、通用应用surface、中文字体/输入、运行时包安装、PCM声卡和内核无中断替换仍待实现。

工作开始时该目录没有Git元数据。修改前源码快照保存于本机 `/tmp/aurora-before-20261029.tar.gz`，不是长期备份或仓库历史；请后续纳入版本控制。
