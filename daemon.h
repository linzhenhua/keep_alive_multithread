#ifndef DAEMON_H
#define DAEMON_H

#include <stdio.h>

/*
* exit（0）：正常运行程序并退出程序；
* exit（1）：非正常运行导致退出程序；
*/

/*记录日志基本要求：时间，错误文件，错误行号，错误函数名，错误码，错误信息*/
/*
 * 函数说明：记录守护进程执行过程
 * 参数1：错误码
 * 参数2：错误信息
 * 参数3：文件名
 * 参数4：行号
 * 参数5：出错函数
 * 返回值：无
 * */
void _DaemonLog(const int error_code, const char *error_msg, const char *file_name, int line, const char *func);

/*
 * 函数说明：释放所有资源
 * 参数：无
 * 返回值：无
 * */
void ReleaseResource();

/*
 * 函数说明：重读配置文件
 * 参数：无
 * 返回值：无
 * */
void ReReadConfiguration();

/*
 * 函数说明：SIGHUP处理函数（重读配置文件）
 * 参数：未使用
 * 返回值：无
 * */
void Hup(int s);

/*
 * 函数说明：SIGPIPE处理函数（连续两次往关闭的socket文件描述符写触发SIGPIPE信号）
 * 参数：未使用
 * 返回值：无
 * */
void Plumber(int s);

/*
 * 函数说明：SIGTERM处理函数，（kill守护进程时触发）释放所有资源
 * 参数：未使用
 * 返回值：无
 * */
void Term(int s);

/*
 * 函数说明：启动一个守护进程
 * 编程规则：
 * 1、首先要做的是调用umask将文件模式创建屏蔽字设置为0
 * 2、调用fork，然后使父进程退出（exit）
 * 3、调用setsid以创建一个新会话
 * 4、将当前工作目录更改为根目录
 * 5、关闭不再需要的文件描述符
 * 6、某些守护进程打开/dev/null使其具有文件描述符0,1,2，这样任何一个试图读标准输入、
 * 写标准输出或标准出错的库例程都不会产生任何效果
 *
 * 参数：未使用
 * 返回值：无
 * */
void InitDaemon();

/*
 * 函数说明：读取配置文件
 * 参数1：本地ip
 * 参数2：本地port
 * 参数3：服务端ip
 * 参数4：服务端port
 * 返回值：无
 * */
void ReadConf(char *Local_Ip, char *Local_Port, char *Remote_Ip, char *Remote_Port);

/*
 * 函数说明：重新连接到联合电子服务器
 * 参数：未使用
 * 返回值：1：连接成功，-1：连接失败
 * */
int ReConnection();

/*
 * 函数说明：线程1的回调函数，用于每40s发送心跳包，维持长连接
 * 参数：无
 * 返回值：无
 * */
void* SendHeartBeat();

/*
 * 函数说明：线程2的回调函数，用于接收联合电子的数据，非心跳包就转发到农行
 * 参数：无
 * 返回值：无
 * */
void* RecvDataFromEServer();

/*
 * 函数说明：线程3的回调函数，用于接受农行的连接和接收农行的数据，并转发到联合电子
 * 参数：无
 * 返回值：无
 * */
void* SendDataToEServer();

/*
 * 函数说明：管理信号的线程，用于处理守护进程收到的信号
 * 参数：信号集
 * 返回值：无
 * */
void* SignalMgr(void *arg);

/*
 * 函数说明：保持长连接
 * 参数：无
 * 返回值：无
 * */
void KeepAlive();

#endif // DAEMON_H
