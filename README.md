# hios

1. Bootloader 编写
2. 加载磁盘中代码到内存，并跳转执行
3. 内存规划，并跳转到 setup.s
4. 设置GDT，IDT，进入32bit保护模式
5. 添加 head.s, 跳转到 main() 执行
6. VGA 屏幕打印
7. 虚拟内存，支持内存分页
8. 实现中断
9. 添加 system_call
10. 进程 0 转变成用户态，fork()创建子进程 1，时钟中断进程调度
11. 处理信号，实现 wait(), exit() 函数
12. 键盘输入，实现 sleep(), getline(), printf()

