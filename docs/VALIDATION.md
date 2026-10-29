# 验证记录（0.8 / ABI4）

日期：2026-10-29。环境：macOS / x86_64-elf GCC / Limine 12.9.6 / QEMU 11.1.3，Q35、512 MiB、TCG、virtio-tablet、1440×900 标准 VGA、BIOS 引导。

## 可复现命令

```sh
make -j4 BUILD_ARCH=out/multiuser TEST_IMAGE=1 iso test
python3 tools/smoke_x64.py --iso out/multiuser/aurora.iso
make -j4 iso              # 最终普通镜像，不包含测试模块
```

QEMU 监控需要本地 Unix socket。受限沙箱可能拒绝 bind；需运行环境允许本地套接字，不能把该失败当成系统内核错误。

## 宿主机检查

- kernel/user 依赖边界；禁止用户代码引用 kernel/arch 头，禁止内核内嵌应用 ELF。
- C11 `-Wall -Wextra -Wshadow -Wstrict-prototypes -Werror` freestanding 构建。
- ASan/UBSan：实际 ELF 和 VFS 解析器；ELF 头溢出、非法入口、W+X、重叠页、错误 ABI、截断、4000 次头部随机变异。
- 文件树循环、路径组件、越界数据和重复名称校验；路径 normalize。
- 21项Python单测：镜像确定性/损坏、符号链接、超大文件、校验和正确的非法树、权限生成、自授 spawn、未知权限/越界安装路径、重复镜像、账号随机salt和SVG几何编译。

- SHA256空串/abc向量、PBKDF2与hashlib交叉值、完整100000轮账号验证、未知用户名/错误密码拒绝。
- owner/group/other与root DAC、sticky删除、resize最小尺寸/边界/四边组合；均运行ASan/UBSan。另检查共享US键盘映射的双Shift、CapsLock、符号和释放事件。

## QEMU 集成检查

- 未登录只启动6个系统服务；错误密码不启动应用；正确登录后3个用户应用启动。
- 10 次桌面采样中窗口始终已完成合成；绝对鼠标与键盘导航。
- Files 侧栏进入 Documents，双击 Welcome.txt 预览。
- 无授权进程尝试显存、IRQ、spawn、线程枚举、kill、音频设备、启动资源、peer grants、VFS、内核事件伪造和非法指针，全部拒绝。
- 16 次 spawn/exit 后空闲物理帧不变，证明槽位和页表/私有页可复用。
- 通过真实 IPC 验证 RAM 创建、读取、覆盖、删除；不完整事务保留旧内容；`/tmp/../system` 与 `/home/user/../../etc` 写入拒绝；有 task-control 权限仍不能终止 input。
- 终端 info、临时文件读写、beep，以及 Settings 测试音；音频 WAV 为实际 QEMU 捕获，检查足够振幅和交零次数。
- 最小化 Monitor 后等待周期刷新，窗口仍隐藏；恢复后双 Delete 停止 Terminal，init 重启，端点 TID 变化。
- 文件STAT属主与mode正确；owner chmod0400阻止自身写入，恢复0600后正常；guest不能chmod别人的文件，root可修改且仍不能写/system。
- 注销后旧窗口区域恢复为登录背景；root与guest依次登录，身份/home正确，root可读其他用户文件，普通用户被拒绝；guest不能删除其他用户的/tmp文件。
- root也不能通过UID隐式取得显存、spawn或会话修改权限。
- Files拖动右下角缩小/恢复，配合宿主机几何测试验证尺寸约束。
- 全程检查 CPU fault、kernel panic 和测试失败标志。

QEMU 11 的 PC-speaker WAV 后端可能在退出后仍保留零 RIFF/data 长度；测试仅按捕获文件长度补写两个长度字段，不生成或修改 PCM。产物可供播放器复核。

## 证据与范围

`out/multiuser/smoke/serial.log`、`qemu.log`、`*.ppm`、`audio.wav` 为测试原始产物；`docs/workspace.png`、`docs/terminal.png`、`docs/login.png`、`docs/resized.png`、`docs/root-session.png`、`docs/guest-session.png`为本轮截图。

自动截图探针验证有限像素和交互结果，不是所有视觉状态的证明。CPU 统计是采样近似，不是性能基准。未验证真机、UEFI/OVMF、SMP、长期压力、DMA 故障、持久化断电恢复、任意第三方程序或内核热切换。GitHub workflow 仅定义宿主机编译/测试，尚未在远程 CI 执行。

## 宿主显示

已依据本机QEMU help及QEMU官方文档确认Cocoa的zoom-to-fit/full-screen/left-command-key参数；客体仍1440×900，宿主窗口缩放不等于guest动态分辨率。GUI窗口和全屏属于宿主桌面状态，主回归使用无头QEMU；真实宿主外观需以make run观察。

## 0.7.1 图形与宿主通道回归

使用 `out/graphics/aurora.iso`：

- `tools/graphics_smoke.py` 同次启动经实际COM2协议切换1920×1080、2880×1800、1280×800、3840×2160、1440×900，校验截图原生尺寸。
- 输入半个测试密码，跨两次模式切换后补全并登录，验证登录状态不丢失。
- 2880×1800下12个拖拽采样都检测到光标，没有合成前清空光标的中间帧；这是采样证据，不是vsync或所有时序证明。
- 越界尺寸消息不会改变显示模式。新增普通进程调用SYS_DISPLAY_MODE拒绝检查。
- ASan/UBSan覆盖2×clip、区域present、圆角覆盖、图标和字体；固定长度消息校验；mailbox相邻motion合并但保留点击/键盘/源边界。
- `tools/runner_smoke.py` 使用实际启动器和 `paste.py --stdin` 登录，不读取用户剪贴板；验证未自动Enter、换行拒绝、私有端点和退出清理。
- `smoke_x64.py` 原有多用户、权限、文件、音频、窗口回归继续通过。

macOS窗口尺寸helper能编译，执行环境没有可见宿主显示，实际Cocoa窗口跟随尚需本机桌面观察；无头测试验证的是尺寸提示到客体真实显示模式的完整路径。

## 0.8 启动与WAV验证

- 已删除自动QEMU窗口观察器；源码策略检查确保run不导入resize监视器，只有启动前探针和显式模式命令。此前用户报告的每次减少64px/循环跳回是反馈缺陷，不能仅靠debounce宣称解决。
- VFS真实ring3测试新增：pread偏移/EOF/溢出、私有路径拒绝、close后失效、chmod后拒绝、覆盖后版本失效、8句柄配额、跨owner读取拒绝、一次性token不可重放/错误peer不可复制。
- ASan/UBSan验证WAV范围/格式/对齐/样本转换及5000次头部变异，并直接验证用户文件：48000Hz、24位、双声道、2874403帧。
- `python3 tools/audio_smoke.py --iso out/wav/aurora.iso --full` 捕获AC97实际PCM，检查左右440/660Hz测试频谱、8位mono22050→48000重采样，播放用户完整WAV并收到 `WAV complete frames=2874403 underruns=0`。另测stop与logout。
- 通知音和原有多用户/图形/文件回归继续通过，日志分别在 `out/wav/audio-smoke-full/`（完整文件）、`out/wav/audio-smoke/`、`out/wav/smoke/`。录制音频为实际输出，不合成验证样本；只在QEMU WAV头长度留零时补齐长度。

CoreAudio物理扬声器体验及真机AC97未在工具环境验证；录音不支持警告不等于PCM-out失败。WAV解析/转换虽经过负向测试，尚非独立安全审计。
