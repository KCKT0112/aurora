# 0.8 本轮交付

启动后分辨率锁定：重新启动 `make run` 后不再出现 Host viewport changed 日志，也不再有自动QEMU窗口观察线程。首次探测不到宿主分辨率时可指定 `make run RESOLUTION=2880x1800`；该模式也会保持不变。运行中的旧进程不会因修改脚本而自动消失，需退出旧QEMU再启动。

WAV：`play` 播放 `/home/user/Music` 中的首个WAV；当前文件为 `222.wav`，与用户原文件字节一致。支持Files双击，Terminal `audio`/`stop`，以及退出/注销停止。当前仍为RAM文件系统，重启后RAM修改丢失。

查看 `TASKS.md` 了解优先级和后续未完成工作，`AUDIO.md` 了解格式/权限/驱动限制，`VALIDATION.md` 了解实际测试证据。
