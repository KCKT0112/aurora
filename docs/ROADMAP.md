# Aurora 路线图

## 0.6 基础重构（已完成）

- [x] 独立内核、用户 ELF、initramfs 和系统目录打包
- [x] 用户 init 启动服务、普通应用限速重启
- [x] ABI 3 ELF note、严格加载校验、失败回收
- [x] 位图物理页回收、进程/页表/IRQ 生命周期
- [x] 静态授权清单、设备与管理 syscall 权限、服务来源校验
- [x] 等待/消息唤醒，减少轮询空转
- [x] 新桌面工作区、文件侧栏、最大化、隐藏状态保留
- [x] 终端、临时文件事务写入、系统监视器与受限应用重启
- [x] 用户态音频服务与 PC speaker 通知音
- [x] 宿主机安全解析测试、QEMU 权限/文件/生命周期/界面回归
- [x] 应用、系统目录、权限、驱动与更新设计文档

## 0.7 账号与交互基础（本轮）

- [x] root/user/guest、独立authd、加盐密码派生、失败退避
- [x] 内核进程UID/GID/session、注销后结束旧会话并清理UI/事务
- [x] 文件owner/group/mode、目录搜索、sticky、stat/chmod
- [x] 当前home、whoami/id/logout、按UID限制进程控制
- [x] SVG矢量图标、抗锯齿、柔和阴影、四边/四角缩放
- [x] make run宿主窗口缩放与默认全屏；普通/测试镜像隔离
- [x] 多账号跨会话QEMU回归、认证/DAC/几何宿主机测试

## 0.7.1 图形链路（本轮）

- [x] 光标离屏叠加，修复先擦除造成的闪烁空档
- [x] 唯一指针源、motion合并、按钮边沿保留
- [x] 原生多分辨率、2×画布、精确字号、圆角抗锯齿
- [x] 宿主尺寸传输与QEMU Standard VGA动态模式切换
- [x] 显式宿主单行粘贴、私有QMP端点、无自动提交
- [ ] 实际Cocoa多屏/缩放配置覆盖；Wayland宿主尺寸后端
- [ ] 按需surface内存、GPU/page flip/vsync与帧时间基准

## 0.8 WAV与启动稳定（本轮）

- [x] 启动前分辨率决策、移除运行时自动反馈循环
- [x] owner绑定只读二进制句柄与64KiB一次性IO授权
- [x] WAV用户态解析/转换、AC97 PCM、play/audio/stop
- [x] 用户完整24位WAV零断流帧数与输出频谱验证
- [x] 明确任务优先级，见[TASKS.md](TASKS.md)

## 下一阶段：Unix/Linux语义与微内核完善

- [ ] 用可撤销 endpoint/device/memory handles 替代静态权限位的粗粒度授权
- [ ] 专用服务UID、附加组、per-app私有存储与对象权限
- [ ] 文件描述符与open/read/write/close/stat，进程exec/wait与凭据继承
- [ ] driver manager、设备枚举服务、受限 MMIO/IRQ/DMA 授权与撤销
- [ ] 迁出 virtio-input / 声音 / 存储硬件逻辑，完成微内核边界
- [ ] virtio block、持久化文件系统、断电一致性测试
- [ ] virtio sound与通用PCM共享流、音量/混音（AC97与整数WAV已实现）
- [ ] 通用窗口注册、共享内存 surface、damage region列表、窗口任意像素surface
- [ ] UEFI/OVMF 回归，SMEP/SMAP、guard pages、栈保护和多核

## 更新与发行

- [ ] 版本化软件包、签名验证、安装事务、权限审核与配额
- [ ] 核心服务监督、故障恢复、可迁移状态 schema
- [ ] A/B 启动项、离线恢复与内核升级回滚
- [ ] 兼容用户态运行矩阵、系统 ABI 演进策略
- [ ] livepatch 安全点原型；完整内核 handover 独立研究与验证

不能用 boot-module 加载冒充运行时驱动安装，也不能用应用重启冒充无中断热更新。持久化、安全隔离与恢复机制先于开放第三方内核/设备权限。
