# Aurora 应用开发规范（ABI 4）

## 支持范围

当前 SDK 面向静态链接、单线程、带UID/GID/session、独立 ring 3 的 C11 应用。支持模型视图、服务 IPC、临时文件和基本系统信息。没有 POSIX 兼容层、动态链接器、fork/exec、通用堆分配、像素 surface 或任意窗口注册。第三方应用不应直接使用内核/架构头文件。

使用 `abi/aurora/` 和 `libs/user/include/aurora/` 的公开头。项目实现放在 `apps/<name>/`，硬件或系统服务放在 `servers/<name>/`。共享解析逻辑放入可宿主机测试的 `libs/core/`。每个新组件提供 README，列出职责、权限、协议、错误和资源上限。

```c
#include <aurora/model.h>

int umain(uint64_t argument)
{
    (void)argument;
    system_info_t info;
    if (sys_info(&info) < 0 || info.abi_version != AURORA_ABI_VERSION)
        return 1;
    user_log("example: online");
    for (;;) {
        ipc_message_t message;
        while (sys_recv(&message) == 0) {
            /* 校验 from、type、request、长度，再处理。 */
        }
        sys_wait(100);
    }
}
```

入口由 `libs/user/start.S` 提供，返回值作为退出状态传递给内核（当前内核只记录退出，不提供 waitpid 状态）。链接脚本生成 `PT_NOTE`、RX text、R rodata、RW data。ABI note 来自 runtime，不能省略；更改 ABI 时必须同时更改版本和汇编 note，并运行加载器测试。

## 构建与授权

新增自带组件需要增加 image ID、Makefile 的 `<name>_SRCS`、`config/system.json` 条目及 `tools/policy.py` 的合法 ID 映射。init 决定是否启动和重启；公开服务和 UI view 也要增加明确路由。这是构建期接入流程，不是已实现的运行期插件安装 API。

清单包含 name、path、source、grants、identity（root或session）；生成的 `.json` 是包信息，不是应用自授权限的入口。内核启动策略根据同一清单编译。新增权限要有内核/服务端拒绝测试；不要因为是“系统应用”就授予所有权限。

## 系统调用

x86_64 `int 0x80`：rax=编号，rdi/rsi/rdx=参数，返回有符号 int64。负值为失败，枚举见 `security.h`；旧接口部分错误仍返回 -1，不要假设所有失败都细分。`SYS_RECV` 空队列返回 `-EAGAIN`。容量、指针、权限都由内核检查，但调用者仍需校验返回值。

使用 `sys_service(IMAGE_*)` 查询当前端点，不能硬编码 TID；服务重启后端点会变。`sys_getppid()` 是父进程，不是桌面。`sys_wait()` 在邮箱非空时立即返回，在空时等待消息或截止时间。处理循环设置消息预算，避免洪泛饿死渲染/其他工作。

## IPC 与模型视图

消息固定 128 字节，payload 80 字节；禁用跨进程裸指针。发送者 `from` 由内核注入，接收端须核验对应服务。使用 `user_send` 进行有上限的 backpressure 重试，最终失败必须保留原状态或显示错误，不能无限重试。

| 消息 | 字段 |
|---|---|
| TABLE_BEGIN | arg0=view, arg1=row count, arg3=transaction；data=table_header_t |
| TABLE_ROW | arg0=view, arg1=row index, arg3=transaction；data=table_row_t |
| TABLE_END | arg0=view, arg1=row count, arg3=transaction |
| VIEW_SELECT/ACTIVATE | arg0=view, arg1=row |
| VIEW_PATH | arg0=view；data=NUL 结尾绝对路径 |
| VIEW_BACK/REFRESH | arg0=view |
| VIEW_KEY | arg0=view, arg1=原始 PS/2 set-1 code；bit8=extended |

`model_publish()` 为新客户端统一构造事务。最多 16 行；表头 title 47 字节、caption 31 字节；行 label 23 字节、value 39 字节。display 原子提交完整行集合，维护自己的 focus、选中行、滚动和窗口状态。后台数据刷新不能改变窗口是否最小化。现有 VIEW_* 为自带应用保留，通用第三方 view 注册待设计。

## 文件和音频协议

LIST/READ 的 arg0 是请求号，data 是 NUL 结尾路径；响应维持请求号，客户端校验源端点和请求号。LIST_BEGIN/ENTRY/END、PREVIEW_BEGIN/LINE/END 在 `abi.h` 定义。二者是目录/文本预览协议，不是任意二进制 read API。

写入：WRITE_BEGIN(arg0=request,arg1=总字节,data=路径)，顺序 WRITE_DATA(arg1=offset,arg2=字节数≤80,data=数据)，最后 WRITE_COMMIT。BEGIN 成功不单独 ACK，服务最终返回 FS_RESULT(arg1=0/错误)。BEGIN 拒绝会立即返回 FS_RESULT。客户端可以先发完有界事务再收最终响应，须丢弃同一失败请求的后续错误。请求号不能在未结束事务中复用。MKDIR/REMOVE 使用路径和请求号，返回 FS_RESULT。

音频：向 IMAGE_AUDIO 发送 AUDIO_PLAY(arg0=request,arg1=Hz,arg2=时长 tick)，得到 AUDIO_RESULT(arg1=0 或负错误)。频率40–8000、时长1–200 tick，正在播放时返回 busy。此 API 只是通知音临时协议，未来 PCM stream 将用独立版本，不复用此消息的含义。

## 用户与文件权限接口

`SYS_DISPLAY_MODE(width,height,info)`仅display的framebuffer能力可用；普通应用和root普通应用均无权调用。

`SYS_INFO`包含真实uid/gid/session；THREAD_LIST各行包含uid。普通应用不能改变身份；只有authd拥有会话切换权。系统服务可通过 `sys_peer_credentials(tid,&credentials)` 从内核取得实时发送者凭据，死进程会返回错误。避免在服务端长期缓存TID授权。UID0与能力独立，任何用户都不能绕过内核对设备、受保护服务的授权。

STAT(arg0=request,data=path) → STAT_RESULT(data=fs_stat_t)，含uid/gid/mode/type/size。CHMOD(arg0=request,arg1=mode,data=path) → FS_RESULT；需要fs_write，且调用者是属主或root；只读挂载优先。模式只接受0000–1777，普通文件禁止sticky，未支持setuid/setgid。路径错误可返回FS_ERROR。

登录为可信display→authd的80字节login_request_t；普通应用不能发送登录请求或读取authd资源。Terminal可请求注销当前会话。用户应用在注销时会结束，不能依赖退出清理来提交事务。服务必须拒绝旧会话已死亡的调用者。

## 质量要求

C11 + warnings-as-errors，边界代码使用减法式范围检查。解析器必须在宿主机运行 ASan/UBSan 负向测试，跨权限行为必须在 QEMU ring 3 验证。提交前 `make test`，涉及启动/协议/界面/调度时 `make smoke`；截图与文档一起更新。不要在热路径分配内存，不能把“没有崩溃”当成权限或数据一致性的完整证明。

## 二进制文件与WAV接口

`aurora/file.h`提供file_open/pread/close/delegate/revoke。file_client RPC等待期间可以通过回调处理其他消息；不要丢弃输入或注销事件。pread最多64KiB，无隐式文件偏移。OPEN返回正handle和文件长度；PREAD返回字节数，EOF为0；负值为通用错误。

FS_OPEN(arg0=req,data=path)，FS_PREAD(arg1=fd,arg2=offset,arg3=IO token,data[0]=count)，FS_CLOSE(arg1=fd)，统一返回FS_FD_RESULT(arg0=req,arg1=status,arg2=value)。DELEGATE(arg1=fd,arg2=audiod TID)返回新的受限handle；CLAIM由audiod验证original source；REVOKE由原始source撤销。

SYS_IO_BUFFER映射私有64KiB窗口；SYS_IO_GRANT(peer,length)创建一次性token，length=0撤销；SYS_IO_COPY(target,token,{source,length})只在target授予当前peer的范围内逐页复制。无任意跨进程地址或物理内存映射能力。

AUDIO_WAV(arg0=req,arg1=委托handle)，AUDIO_STOP，AUDIO_STATUS→AUDIO_INFO(data=audio_info_t)。PCM设备接口SYS_PCM_WRITE/SYS_PCM_CONTROL只对CAP_AUDIO_DEVICE开放；普通客户端应只用文件委托和audio IPC。输入格式/错误/生命周期见AUDIO.md。
