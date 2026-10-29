# WAV播放与PCM输出

## 使用

macOS `make run`默认CoreAudio输出；静音测试使用 `make run AUDIO_BACKEND=none`。QEMU实例需要AC97设备，启动器自动加入。旧实例需要退出后重新构建并启动。

- Terminal：`play` 播放当前用户Music目录中的首个 `.wav`；`play /absolute/path.wav`或相对路径指定文件。
- `audio`显示输入格式和播放进度，`stop`停止自己发起的播放（root可停止其他发起者）。
- Files：在Music目录双击/Enter打开WAV，Space请求停止自己发起的播放。
- 注销或发起应用退出会停止播放、撤销相关读取句柄。单次只播放一条WAV流，忙时返回明确失败；通知音仍保留PC speaker后端。

本机历史验证文件（不随公开仓库分发）：`/home/user/Music/222.wav`，17251692字节、48000Hz、24位、双声道、2874403帧（约59.88秒）。源码文件未修改。打包上限提高为单文件32MiB、总镜像仍64MiB；不再将这份文件排除在镜像之外。

## 格式

支持little-endian RIFF/WAVE整数PCM：8位无符号，16/24/32位有符号；1或2声道；8000–96000Hz。支持规范PCM extensible GUID，valid bits必须等于容器位数，声道mask限mono/stereo或0。校验RIFF/chunk范围、奇数字节padding、block align、byte rate和完整帧；跳过LIST/JUNK等未知元数据。最多256个chunk，拒绝重复fmt/data、float、压缩、RIFX、RF64及更多声道。

输出固定为48kHz双声道s16。mono复制到左右；24/32位降低到16位；非48k输入做有界整数线性重采样。没有dither、限幅器、音量UI、多流混音或无损高位深输出承诺。转换发生在用户态audiod。

## 文件与权限

应用以自身UID打开只读句柄，VFS验证目录遍历和文件read权限。随后显式将该句柄委托给当前audiod端点。audiod首先核验句柄原始发起TID与请求者一致，不能凭调用者给出的路径用root身份打开任意文件。

句柄为单调ID，绑定owner TID、original source TID、UID/GID/session、节点与内容版本。每owner最多8个、全服务64个。pread重新检查发起者存活、文件read权限和内容版本；覆盖/删除使旧句柄失败，chmod降低权限会阻止后续读取。此语义比POSIX open后仍保有权限更严格，是明确的当前策略。并发write快照和unlink后继续读未实现。

bulk读取每次≤64KiB：接收进程申请固定私有I/O窗口，并授予精确VFS端点一个一次性token和长度上限；内核逐页校验并复制，不向VFS暴露任意可写用户地址，不传递物理地址。token成功复制后消耗，也可显式撤销。进程退出时窗口作为私有页回收。

## 硬件与资源

当前支持QEMU ICH AC97（8086:2415）。内核仅负责有界PCM复制、32个固定4KiB DMA页和描述符、启动/停止和完成计数。应用没有DMA地址控制权；只有audiod有CAP_AUDIO_DEVICE。预填充后启动，最多31个在队列中，tick轮询回收；剩余缓存不应重复播放，结束时报告完成帧数与underrun数。

这是当前硬件机制留在ring0的过渡实现，不是已经完成的用户态驱动或通用AC97真机认证。后续应在设备/内存能力与DMA撤销机制完成后迁到driver服务。

QEMU CoreAudio仅支持播放，AC97初始化会尝试创建PCM-in/microphone-in。启动器把这两条已知录音警告汇总一次，完整stderr保存到 `out/<target>/qemu.log`。任何PCM-out失败、未知错误仍原样显示；没有把输出故障静默过滤。测试使用WAV录制后端，通过输出频谱与帧数验证，而不是只检查“播放请求成功”。

公开克隆没有个人Music文件时，测试镜像会生成6秒48kHz/24位双声道合成音；audio-smoke根据实际测试文件计算完成帧数。普通镜像不会凭空生成用户音乐，用户可自行添加本地WAV再构建。
