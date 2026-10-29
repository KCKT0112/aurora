# 系统目录规范（布局v1 / 镜像v2）

宿主源码 `rootfs/`、生成的 `out/<target>/rootfs/` 和运行中的 `/` 是不同层次。源码只保存配置与数据，构建将 ELF 与包清单放入 staging tree，再生成 initramfs 和 ISO。应用更新不会要求把 ELF 重新链接进内核。

| 系统路径 | 职责 | 当前运行时状态 |
|---|---|---|
| `/system/kernel/` | 可独立替换的内核 ELF | 内核在 ISO；启动内核不额外复制到 VFS 镜像 |
| `/system/boot/` | initramfs 等引导资源 | ISO 实体，VFS 内保留目录，避免镜像递归 |
| `/system/servers/` | init、display、vfsd、audiod | 只读 |
| `/system/drivers/` | 自带用户态驱动组件与清单 | input 协议服务；完整驱动安装待实现 |
| `/system/apps/<id>/` | 自带 app.elf 与 app.json | 只读 |
| `/system/lib/` | 未来共享运行库 | 当前静态链接，目录保留 |
| `/system/share/doc/` | 随系统文档 | 只读 |
| `/etc/` | passwd/group公开身份表、系统配置、os-release | root可RAM修改普通配置；账号身份以authd启动资源为准 |
| `/home/<name>/` | user与guest的私有目录，0700 | 当前用户RAM覆盖写入，重启丢失 |
| `/root/` | root私有home，0700 | 仅root访问 |
| `/tmp/` | 临时文件，root:root 01777 | sticky限制跨用户删除；重启丢失 |
| `/var/log/` | 将来的服务日志 | 目录保留，当前日志在串口 |
| `/var/lib/packages/` | 将来的安装包数据库 | 目录保留，无包管理器 |
| `/run/` | 运行期服务与会话状态 | 目录保留；发现服务通过 syscall |
| `/dev/` | 未来受限设备代理 | 目录保留，不暴露原始端口/设备节点 |
| `/volumes/` | 未来可移动卷挂载点 | 目录保留，无挂载功能 |

未来第三方包采用 `/var/lib/packages/<reverse-dns-id>/<version>/` 的只读版本目录，用户配置归属 `/home/<user>/.config/<id>/`、数据归属 `.local/share/<id>/`，缓存归属 `.cache/<id>/`。当前打包器跳过隐藏条目，以上私有目录只是未来规范，不能依赖其运行时存在。

## 镜像与限制

格式 v2 为32字节header、72字节节点（包含UID/GID/mode）、连续数据区；节点按父在前顺序编码。最多 256 节点，单组件名 39 UTF-8 字节，单文件32MiB，总镜像 64 MiB。VFS 为追加元数据预留 24 个槽，启动时拒绝没有预留空间的镜像。文件名 UTF-8 可保存，但 UI 字体目前仅有限字符覆盖，中文名称可能显示缺字。

绝对路径 IPC 限制 79 字节（含 NUL 的缓冲区为 80），单目录 UI 最多 12 子项，超过时返回明确错误，不截断为不完整目录。预览最多 16 行，每行 62 字节；长行折行，二进制/非 ASCII 字节替换为 `?`。这是文本预览，不是通用文件读句柄接口。

RAM 层有 24 个数据槽，单文件最多 4096 字节，可在可写区域且DAC通过时覆盖或创建文件；删除非空目录拒绝。写事务 5 秒超时、顺序 chunk、完整 COMMIT 才替换文件。没有磁盘驱动、fsync、断电一致性或持久化保证。目录树与容量限制必须由服务端执行，UI 不能扩大边界。

## Linux权限语义

目录遍历需要execute，列目录需要read+execute；打开文本预览需要file read。创建文件需要parent write+execute，覆盖需要file write，删除需要parent write+execute且sticky条件通过。默认新目录0700、文件0600，属主和主组来自不可变进程凭据。root越过DAC，但不能越过只读 `/system`。`stat`展示元数据，`chmod`允许属主/root设置mode；无chown、ACL、附加组、setuid/setgid或可配置umask。

文件在logout后保留，权限隔离仍生效；logout只取消未完成事务。`accounts.bin`位于ISO的system/boot中，独立给authd映射，不打包进VFS，因此不能通过cat或文件管理器读取。

## 只读二进制句柄

OPEN/PREAD/CLOSE可读取原始文件字节，不受16行预览限制。全局64个句柄、每owner最多8个，ID不复用；64KiB一次性IO授权约束服务写入接收进程的范围。文件覆盖/删除会使已有句柄失效；chmod后每次读取重新检查read；非POSIX unlink存活inode语义。可明确委托audiod，保留原始用户/会话/TID约束，详见AUDIO.md。
