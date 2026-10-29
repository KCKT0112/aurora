# Aurora 0.7 架构

## 启动与职责

```text
Limine → kernel → init
                   ├─ display → window manager + compositor
                   ├─ authd   → password verification + session authority
                   ├─ input   → PS/2 parsing + semantic input
                   ├─ vfsd    → immutable system tree + RAM overlay
                   ├─ audiod  → bounded tone requests
                   ├─ files
                   ├─ taskmgr
                   └─ terminal
```

内核 ELF 不包含用户程序。Limine 按生成的配置加载独立模块；内核只允许从可信启动清单启动已知镜像。每个 ELF 带 `PT_NOTE` Aurora ABI 版本，加载器拒绝不匹配版本、动态链接、非法入口、W+X 与重叠页。用户 init 拥有启动权限，桌面拥有显存、计时事件和通知音请求权限。认证前仅启动系统服务；authd建立会话后init才启动应用。应用通过 `SYS_SERVICE(image_id)` 查询当前端点；父进程现在是 init，不再把父 PID 当成显示服务。

`config/system.json` 是启动授权的唯一可编辑来源；`tools/policy.py` 生成内核授权表，`stage_system.py` 生成系统文件和清单。应用自己的 JSON 不能自授权限。该清单目前属于构建期可信策略，不是签名或运行时安装机制。

## 内存

每进程独立低半区页表；内核高半区共享且仅 supervisor 可访问。4 KiB 页，代码 RX、数据/栈 RW+NX、只读数据 R+NX。用户布局为镜像 `0x400000..0xf000000`、64 KiB 栈顶 `0x10000000`、VFS 只读资源 `0x30000000`、显存 `0x40000000`。栈下未映射，内核栈尚无 guard page。

物理分配器使用选定 usable extent 的位图，容量上限 4 GiB；零填充分配，返回 0 表示 OOM。它仍不使用所有离散内存区，不支持交换。用户页用软件 `VMM_OWNED` 标识，销毁地址空间时回收私有页和页表，不回收显存、启动模块或共享高半区。失败的加载同样释放已分配资源。用户缓冲区检查规范地址、溢出、逐级 U/S 与 R/W 权限，并拒绝 huge-page descent。

## 调度与生命周期

单 CPU 抢占式 round-robin，12 个线程/进程槽位，16 KiB 静态内核栈，一进程一线程。PIT 100 Hz 提供采样和超时。`SYS_WAIT(ticks)` 在邮箱为空时阻塞，有消息或截止时间唤醒，最长 60 秒；`SYS_PAUSE` 保留为 1 tick wait。取消了旧实现中在 syscall 内 `sti; hlt` 的嵌套调度路径。

退出/异常标记 DEAD，下一次从其他线程进入内核时回收地址空间和槽位，清除 IRQ 订阅。TID 单调递增，槽位复用不复用旧 TID。每进程保存不可变UID/GID/session；注销终止旧会话全部应用，清空输入/display队列、取消VFS事务，再以新会话身份启动新进程。init 在应用退出后按最短 2 秒间隔重启 Files、Monitor、Terminal；不重启 stateful 核心服务，因为目前尚无可靠状态恢复协议。监视器只能停止普通应用，系统服务即使面对有 task-control 权限的调用者也不能被终止。

CPU 百分比来自 tick 差值，短任务可能采样不到；内存数字是私有 ELF+栈映射页数，不包含共享启动资源、MMIO、页表和全局内核栈，不是完整 RSS/PSS。

## IPC 与用户协议

固定 128 字节消息，32 条 mailbox。`from` 始终由内核填写；用户不能投递内核事件类型。邮箱满返回 `-EAGAIN`，库最多重试 32 次并 yield，禁止无限忙等。发送成功可唤醒等待线程，但没有同步 RPC 优先级继承或流量公平保证。

显示服务按当前服务端点及视图所有者检查发送者。BEGIN/ROW/END 使用 `arg3` 事务号、逐行 bitmask，缺行或交错事务不会提交半张表。应用只发布模型；独立像素 surface 与通用窗口创建尚未实现。

VFS 自启动模块只读映射initramfs v2，校验树和UID/GID/mode后提供LIST/READ、STAT/CHMOD与RAM事务。服务从内核取得发送者凭据，逐级检查目录权限并执行sticky删除规则。authd只映射独立accounts启动模块，VFS不可读取密码派生值。内核控制 VFS 服务访问，VFS 自己控制可写路径与容量。输入只接受内核原始事件；display 只接受 input 的语义事件。音频设备调用只授予audiod；应用通过显式只读文件委托请求WAV播放，解析、转换和重采样在用户态。内核保留AC97有界DMA机制。

## 微内核边界与现有限制

架构相关的 PCI、virtio tablet、PIT/PIC、PS/2 原始字节读取和 PC speaker端口与AC97 DMA机制仍在ring0；启动 splash 也仍使用内核 UI 栅格器。用户态输入服务负责协议解释，audiod 负责音频策略，但这不等于完整用户态硬件驱动。

目标是把设备分配、IRQ 路由、受限 MMIO/DMA 句柄留给内核机制，由 driver manager 授权用户态驱动。没有 IOMMU、DMA 撤销和可撤销对象能力前，不开放任意设备映射。详细迁移与更新规则见 `DRIVERS_AND_UPDATES.md`。

当前无 SMP、KPTI、SMEP/SMAP、栈保护、网络、持久化文件系统或真机认证。认证已支持三个静态账号和单图形会话，尚无动态用户管理与完整POSIX凭据。BIOS/QEMU 是验证目标，ISO 含 UEFI 启动文件，但未等价于 UEFI 实测通过。

## 0.7.1 显示路径

QEMU启动器通过只含尺寸的COM2固定长度帧发送宿主viewport提示。内核做有界解析后发给display，display调用受CAP_FRAMEBUFFER限制的SYS_DISPLAY_MODE；架构层确认Standard VGA硬件并验证显存、尺寸，再设置DISPI。其他硬件不启用此入口。

显存仅映射给display，映射大小使用经设备确认的aperture（最多64MiB）；软件画布为本地私有内存。UI surface记录逻辑尺寸与整数scale，像素stride为实际物理尺寸。字体图集覆盖精确1×/2×字号。模式切换重布局但保留模型、身份与未提交登录输入。

光标只在canvas合成；显示服务不再在耗时绘制前擦除可见光标。同源同按钮的相邻motion可合并，按键/点击边沿不得跨越。PS/2只在没有tablet时承担相对指针职责。

0.8已删除宿主窗口的自动监听；启动器只在启动前确定显示模式，运行中只有显式resize请求。二进制文件由VFS服务管理owner绑定句柄，一次性I/O窗口限制跨进程复制；audiod不凭root路径绕过用户权限。
