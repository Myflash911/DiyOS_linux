#自己动手自作一个操作系统
### Usage
```bash
cd DiyOS
chmod +x build_env.sh start_env.sh stop_env.sh
# step 1. build env
./build_env.sh
# step2. start env
./start_env.sh
# step3. open chrome and visit http://127.0.0.1:6080
# step4. open terminal
cd /root/workspace/DiyOS
make
```
### 说明
* 参考《Orange's一个操作系统的实现》、《30天自制操作系统》

* 这个操作系统目前只能通过软盘引导，同时为了方便编程，使用fat12格式化的软盘（fat文件系统头部信息已经写入boot.asm中），使用mount -o loop,rw a.img /xxx可以挂在，并且将其他程序写入软盘；相对的，linux 加载0扇区bootsec后，接着用bootsec加载1-4扇区的setup程序，setup继续引导剩余的全部扇区到内存，但是这样写入组织好的多个文件到软盘操作起来不是很方便，所以这里借用fat12格式的文件系统。

* 为了方便使用c语言，系统内核和未来的应用程序均使用ELF格式的可执行文件，这样就可以在Linux中编写程序。

* 运行环境：bochs release 2.6.7
* bochs调试断点：1.0x7c00 boot.bin入口地址；2.0x90100 loader.bin入口地址；3.0x400 kernel.bin入口地址

### kernel架构图

![img](http://aducode.github.io/images/2015-09-02/kernel.png)

###文件系统结构图

![img](http://aducode.github.io/images/2015-09-02/fs.png)

### 目前进度:

1. boot loader完成;
2. 微内核架构kernel，主要处理进程调度;
3. 自定义系统中断函数sendrecv，用于进程间消息通信;
4. 屏幕输出，完成用户级别输出（printf）;
5. 键盘输入;
6. 简单的文件系统，文件操作接口open close read write等;
7. 内存管理，fork进程;
8. tty设备归入文件系统，由文件系统接口统一管理;
9. 支持创建目录，树形文件系统;
10. 文件重命名、目录操作(创建目录、删除空目录);
11. 进程运行时目录,chdir;
12. 一些测试用例;
13. 共用数据结构hashmap;
14. abstract file system layer分离，修改过后的代码经过单元测试;
15. 非阻塞的receive接收广播方式，添加sleep函数，进程可以让出cpu时间片；

### 近期计划：
0. ~~【afs分支】抽象出文件系统层，方便挂载异构文件系统，使逻辑更清晰（逻辑目前还有错误，需要修改，通过测试用例）~~;
1. 软盘驱动，首先需要了解x86架构下的软盘驱动指令;
2. 解析fat12文件系统，以便mount;
3. 挂载/卸载文件系统，实现软盘数据向硬盘的拷贝;
4. ~~粗粒度内核锁(只供系统进程TASK使用)~~，由于系统比较简单，系统进程一次只处理一个消息，所以每个系统调用都是原子性的，不需要这个级别的锁;
5. 通用的数据结构，hashmap 链表、红黑树;
6. 动态的文件结构（现在创建文件/文件夹会固定2048个扇区solt，不足时会浪费空间，多余时无法处理）;
7. 动态内存管理 malloc free;
8. 实现exec，为操作系统编写应用程序;
9. 脚本（类似bash）：设计脚本语言，解析器;
10. 启动时修改显示模式，进入VGA模式控制台模式;
11. 添加鼠标驱动，支持图形化处理;
12. GUI图形操作界面，窗口管理器;

### 问题：

* 操作系统调试很困难，打log的方式有时候都不可取，因为打log会操作文件系统，产生系统中断，会影响现场,目前做法是，printk系统调用，尽可能少的输出调试信息;
* 微内核架构，所有操作都是异步的方式，编码调试非常困难,printf log追踪都不行，IO操作会改变进程执行顺序;
* 目前的问题是打开stdin stdout时会死锁阻塞进程，但是当调用printk系统中断时，死锁又会解除导致问题无从查找;
