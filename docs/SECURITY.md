# 权限与多用户模型（0.7 / ABI 4）

Aurora 采用 Unix/Linux 的用户、组、属主与权限思想，继续保持微内核方向。用户身份和组件能力是两个独立检查：UID 0 可绕过普通 DAC 权限，但不会自动获得显存、IRQ、启动、认证或停止系统服务的权限。

## 账号与凭据

当前有 root（UID/GID 0，`/root`）、user（1000，`/home/user`）、guest（1001，`/home/guest`）。guest 是另一个普通账号，不是免密码访客。进程携带不可变 `{uid,gid,session}`；`SYS_INFO`、线程快照和 `SYS_PEER_CREDENTIALS` 读取真实身份。服务通过内核查询消息发送者身份，不采信 IPC payload 自称的 UID。

init 以 root 启动系统服务，用户应用由当前已认证会话派生凭据。可信清单必须显式声明 `identity: root|session`，无会话时不能启动 session 应用。只有 authd 拥有 `CAP_SESSION` 并能调用 `SYS_SESSION_SET`。未提供通用 setuid、sudo、su、真实/有效/保存 UID 三元组、附加组或动态 useradd，不能宣称已实现完整 POSIX 凭据语义。

系统支持多个账号轮流使用一个本地图形会话。注销时内核终止全部旧会话进程、阻止死进程继续获取服务授权、清空显示与输入队列，通知 init/VFS/display。VFS 取消未完成事务，display 擦除模型、密码和输入状态；新应用以新的 TID 和 session generation 启动。已提交文件在 RAM 中保留，按 owner/group/mode 隔离；整机重启后消失。

## 登录与密码

认证由独立 ring 3 authd 执行。使用随机128位 salt、PBKDF2-HMAC-SHA256（100000轮）、固定长度比较；SHA/PBKDF2 与 hashlib/标准向量交叉验证。失败统一提示，未知用户名也执行密码派生。失败后全局退避从2秒递增至32秒；PBKDF2可被调度抢占，但认证服务串行处理。

正常镜像首次构建由宿主机安全随机源生成三个不同密码。`.local/accounts.json` 保存 verifier，`.local/initial-passwords.txt` 仅在首次生成时保存初始密码；目录0700、文件0600，并在 `.gitignore` 排除。既有账号文件不会被重建覆盖。使用 `python3 tools/accounts.py --set-password root` 交互设置新密码，再 `make iso`；旧初始密码文件可能已过时。

`accounts.bin` 作为独立启动模块只读映射给 authd，不进入 VFS 镜像。ISO上保留 verifier 用于引导，拥有宿主机或ISO读取权限的攻击者仍可离线尝试密码；当前不是加密磁盘或安全启动。显示/输入服务属于可信登录路径。密码在输入、消费后的IPC邮箱、正常发送/接收临时副本中擦除。UI掩码长度仍暴露密码长度。

测试镜像显式使用 root-test/user-test/guest-test；只允许用于自动验证，`make smoke` 默认放在独立的 `out/test/`。普通 `make iso` 不打包测试程序或测试账号。不要发布测试ISO。

## 授权表

| 组件 | 身份 | 能力 |
|---|---|---|
| init | root | spawn, inspect, service |
| authd | root | session, service |
| display | root | framebuffer, timer, audio, service |
| input | root | input, service |
| vfsd | root | service；可映射initramfs |
| audiod | root | audio_device, fs_read, service |
| files | 当前用户 | fs_read, fs_write, audio |
| taskmgr | 当前用户 | inspect, task_control |
| terminal | 当前用户 | fs_read, fs_write, inspect, audio |

用户不能自行增权。SYS_KILL 同时检查能力、UID所有权与目标是否系统服务；非root只能控制同UID普通应用，root仍不能停止核心服务。普通用户可观察系统服务状态和自己的应用，不可枚举其他用户应用的私有统计。IPC sender由内核设置；跨会话普通进程发送被拒绝，内核消息类型禁止用户伪造。authd只接收display的登录请求，注销额外允许当前Terminal。

## 文件权限

文件格式v2加入UID/GID。VFS执行目录逐级execute、目录list的read+execute、文件read/write、父目录create/delete权限。owner优先于group，group优先于other；不把不属于自己的权限位合并。`/tmp` 为root:root 01777，sticky规则禁止普通用户删除其他用户文件。默认新文件0600、目录0700（等价于私有默认umask策略，尚无可设置umask）。

`stat`返回属主、主组、mode、类型、长度；`chmod`只允许属主或root，并拒绝setuid/setgid及普通文件sticky。系统只读挂载策略优先，root也不能修改 `/system`。`/home`、`/root`、`/tmp`、`/etc` 的RAM修改仍须DAC通过；普通用户不能写root拥有的 `/etc`。

事务在BEGIN及COMMIT都验证当前身份和写权限；同路径活动写事务阻止删除/重建，权限变化在提交时重新检查。未完成写入保留旧内容，死进程或注销前的请求不能在新会话继续执行。

## 已知边界

这是正在演进的操作系统原型。没有附加组、ACL、per-app沙箱、配额、持久化账号修改、密码恢复、联网认证、签名启动、IOMMU、SMEP/SMAP或内核栈guard。所有系统服务暂时使用root身份，后续应增加专用service UID并保留显式能力。FNV仅检测镜像损坏，不是签名。IPC仍是有界邮箱而非可撤销对象能力，服务洪泛和全局登录退避可造成拒绝服务。密码派生实现已测但未经过独立安全审计。

## 宿主集成边界

QMP粘贴是宿主主动开发操作，只传单行可打印ASCII，不自动提交。端点位于0700临时目录，无网络监听，脚本不记录剪贴板。宿主resize走另一个私有socket/COM2，只能表达有上限的宽高；此提示没有改变用户凭据或获得其他设备访问的含义。显示模式syscall仍检查framebuffer能力。详见HOST_INTEGRATION.md。

## 文件委托与PCM

audiod的fs_read用于从VFS读取显式委托句柄；当前实现不接受路径请求，也不直接open文件。VFS保留original source身份，每次pread核验存活、UID/GID/session、read权限及内容版本。内核只允许VFS来源的FS_FD_RESULT回复绕过音频“请求能力”门禁，其他用户不能伪造该例外。

SYS_IO_COPY必须匹配接收进程给当前sender的一次性token、长度和固定IO窗口，成功即消耗。DMA页由内核持有；用户PCM只会复制到这些页，无法提交总线地址。退出/注销清理源身份和句柄，停止播放；播放数据已进入DMA队列的权限撤销有有限缓冲延迟。
