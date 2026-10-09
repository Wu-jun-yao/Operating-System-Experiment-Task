lab1 使用的提示词汇总


一、环境搭建阶段

1. 我在 Windows 上用 WSL2 做这个操作系统实验，请告诉我需要安装哪些工具，以及为什么需要交叉编译器和 QEMU 模拟器。
2. 请检查我的 WSL 环境中 RISC-V 交叉工具链和 QEMU 是否已经正确安装，并给出验证命令。

二、理解代码阶段

3. 请解释 `kern/init/entry.S` 中 `la sp, bootstacktop` 这条指令完成了什么操作，目的是什么。
4. 请解释 `kern/init/entry.S` 中 `tail kern_init` 这条指令完成了什么操作，为什么不使用 `call`。
5. 请解释 `tools/kernel.ld` 链接脚本中 `BASE_ADDRESS = 0x80200000` 和 `ENTRY(kern_entry)` 的作用。
6. 请说明 `kern_init()` 函数中 `memset(edata, 0, end - edata)` 这一行的作用，`edata` 和 `end` 分别代表什么。
7. 请梳理 `cprintf` 从调用到字符出现在屏幕上的完整调用链，并说明为什么内核不能直接操作硬件。

三、编译与运行阶段

8. 请解释 Makefile 中 `make`、`make qemu`、`make debug`、`make gdb` 四个目标分别做了什么。
9. `make qemu` 启动后，QEMU 的参数 `-machine virt`、`-nographic`、`-bios default`、`-device loader,file=...,addr=0x80200000` 各自是什么含义？
10. 运行内核后屏幕出现 `(THU.CST) os is loading ...` 之后就不再响应，这是否正常？

四、调试阶段

11. 请说明为什么调试内核需要同时使用 QEMU 和 GDB 两个程序，以及 `-S` 和 `-s` 两个参数的作用。
12. 请解释 GDB 命令 `x/6i 0x1000` 输出的那几条指令分别完成了什么功能，尤其是 `auipc`、`csrr a0, mhartid`、`ld t0, 24(t0)` 和 `jr t0`。
13. 执行 `si` 之前 `sp` 的值是 `0x8001bd80`，执行之后变成了 `0x80203000`，请解释这个变化说明了什么。
