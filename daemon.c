#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/socket.h>

#include "bcd2hex.h"
#include "cJSON.h"

#define DaemonLog(error_code, error) \
    _DaemonLog(error_code, error, __FILE__, __LINE__, __FUNCTION__)

#define DIR "/home/dell/src/keep_alive/keep_alive_multithread/"
#define BUFFER_SIZE 1024

//服务端socket描述符
int g_server_fd = -1;
//客户端socket描述符
int g_client_fd = -1;
//连接到g_server_fd的socket文件描述符
int g_conn_fd = -1;
//日志文件描述符
FILE *g_file_fd = NULL;

//线程锁
pthread_mutex_t g_log_mutex;
pthread_mutex_t g_client_fd_mutex;
pthread_mutex_t g_heartbeat_mutex;

/*发送过来的数据保存到这里*/
char g_server_recv_buf[BUFFER_SIZE];
/*联合电子发送过来的数据保存到这里*/
char g_client_recv_buf[BUFFER_SIZE];

//发送心跳包计数，每发送一个心跳包，加1
int g_send_heartbeat_num = 0;
//接收心跳包计数，每接收一个心跳包，加1
int g_recv_heartbeat_num = 0;

/*
 * 函数说明：释放所有资源
 * 参数：无
 * 返回值：无
 * */
void ReleaseResource()
{
    if(g_server_fd != -1)
    {
        close(g_server_fd);
        g_server_fd = -1;
    }
    if(g_client_fd != -1)
    {
        close(g_client_fd);
        g_client_fd = -1;
    }
    if(g_file_fd != NULL)
    {
        fclose(g_file_fd);
        g_file_fd = NULL;
    }
}

/*
 * 函数说明：记录守护进程执行过程
 * 参数1：错误码
 * 参数2：错误信息
 * 参数3：文件名
 * 参数4：行号
 * 参数5：出错函数
 * 返回值：无
 * */
void _DaemonLog(const int error_code, const char *error_msg, const char *file_name, int line, const char *func)
{
    //加锁
    pthread_mutex_lock(&g_log_mutex);

    struct timeval tv;
    struct tm *now_time;

    memset(&tv, 0, sizeof(struct timeval));

    gettimeofday(&tv, NULL);

    now_time = localtime(&tv.tv_sec);

    char log_name[30] = {'\0'};

    //日志名称
    sprintf(log_name, "daemon_%04d%02d%02d.log", now_time->tm_year+1900, now_time->tm_mon+1, now_time->tm_mday);

    //打开文件
    if(NULL == g_file_fd)
    {
        g_file_fd = fopen(log_name, "a");
        if(NULL == g_file_fd)
        {
            exit(1);
        }
    }

    fprintf(g_file_fd, "%d-%02d-%02d-%02d-%02d-%02d-%.04d_%s_%d_%s_%d : %s\n",
            now_time->tm_year + 1900,
            now_time->tm_mon + 1,
            now_time->tm_mday,
            now_time->tm_hour,
            now_time->tm_min,
            now_time->tm_sec,
            (int)tv.tv_usec,
            file_name,
            line,
            func,
            error_code,
            error_msg);

    //fflush(g_file_fd);

    fclose(g_file_fd);
    g_file_fd = NULL;

    //解锁
    pthread_mutex_unlock(&g_log_mutex);
}

/*
 * 函数说明：重读配置文件
 * 参数：无
 * 返回值：无
 * */
void ReReadConfiguration()
{
    FILE *fp = NULL;
    int file_len = 0;
    char *pszFileBuf = NULL;

    cJSON *pszJson = NULL;
    cJSON *pszItem = NULL;

    char pszRemoteIp[20] = {0};
    char pszRemotePort[10] = {0};
    char pszLocalIp[20] = {0};
    char pszLocalPort[10] = {0};

    fp = fopen("keep_alive.conf", "rb");
    if(NULL == fp)
    {
        DaemonLog(-1, "打开配置文件失败");
    }
    else
    {
        DaemonLog(0, "打开配置文件成功");
    }

    /*把文件指针移动到文件尾*/
    fseek(fp, 0, SEEK_END);
    /*获取文件内容长度*/
    file_len = ftell(fp);
    /*把文件指针移动到文件首*/
    fseek(fp, 0, SEEK_SET);

    pszFileBuf = (char *)malloc((file_len + 1) * sizeof(char));
    if(NULL == pszFileBuf)
    {
        DaemonLog(-1, "分配内存失败");
        fclose(fp);

        exit(1);
    }
    else
    {
        DaemonLog(0, "分配内存成功");
    }
    memset(pszFileBuf, 0, file_len + 1);

    fread(pszFileBuf, file_len, 1, fp);
    pszFileBuf[file_len] = '\0';
    fclose(fp);

    /*载入JSON字符串*/
    pszJson = cJSON_Parse(pszFileBuf);
    if(NULL == pszJson)
    {
        DaemonLog(-1, "载入JSON字符串失败");
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "载入JSON字符串成功");
    }

    pszItem = cJSON_GetObjectItem(pszJson, "Remote_IP");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "解析Remote_IP失败");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "解析Remote_IP成功");
    }
    memcpy(pszRemoteIp, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Remote_Port");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "解析Remote_Port失败");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "解析Remote_Port成功");
    }
    memcpy(pszRemotePort, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Local_IP");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "解析Local_IP失败");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "解析Local_IP成功");
    }
    memcpy(pszLocalIp, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Local_Port");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "解析Local_Port失败");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "解析Local_Port成功");
    }
    memcpy(pszLocalPort, pszItem->valuestring, strlen(pszItem->valuestring));

    /*test
    printf("pszRemoteIp: %s\n", pszRemoteIp);
    printf("pszRemotePort: %s\n", pszRemotePort);
    printf("pszLocalIp: %s\n", pszLocalIp);
    printf("pszLocalPort: %s\n", pszLocalPort);
    test*/

    cJSON_Delete(pszJson);
    pszJson = NULL;
    free(pszFileBuf);
    pszFileBuf = NULL;
}

/*
 * 编程规则：
 * 1、首先要做的是调用umask将文件模式创建屏蔽字设置为0
 * 2、调用fork，然后使父进程退出（exit）
 * 3、调用setsid以创建一个新会话
 * 4、将当前工作目录更改为根目录
 * 5、关闭不再需要的文件描述符
 * 6、某些守护进程打开/dev/null使其具有文件描述符0,1,2，这样任何一个试图读标准输入、
 * 写标准输出或标准出错的库例程都不会产生任何效果
 *
 * 函数说明：启动一个守护进程
 * 参数：未使用
 * 返回值：无
 * */
void InitDaemon()
{
    int i, fd;
    pid_t pid;

    /*转为后台进程*/
    if( (pid = fork()) < 0 )
    {
        DaemonLog(-1, "创建守护进程失败");
        exit(1);
    }
    else if(0 != pid)
    {
        DaemonLog(0, "父进程退出");
        exit(0);
    }

    /*开启新的会话组，成为会话组长和进程组长*/
    setsid();

    /*使其不再是会话组长，不能开启终端*/
    if( (pid = fork()) < 0 )
    {
        DaemonLog(-1, "创建守护进程失败");
        exit(1);
    }
    else if(0 != pid)
    {
        DaemonLog(0, "父进程退出");
        exit(0);
    }

    /*关闭已经打开的文件描述符，避免浪费系统资源*/
    for(i = 0; i < NOFILE; ++i)
    {
        close(i);
    }

    /*更改工作目录*/
    int res;
    res = chdir(DIR);
    if( res < 0 )
    {
        DaemonLog(-1, "更改工作目录失败");
        exit(1);
    }
    else
    {
        DaemonLog(0, "更改工作目录成功");
    }

    /*重设文件掩码，使文件操作权限不再受父进程影响*/
    umask(0);

    /*重定向输入输出*/
    fd = open("/dev/null", O_RDWR);
    if(fd < 0)
    {
        DaemonLog(-1, "重定向输入输出失败");
        exit(1);
    }
    else
    {
        DaemonLog(0, "重定向输入输出成功");
    }

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

/*
 * 函数说明：读取配置文件
 * 参数1：本地ip
 * 参数2：本地port
 * 参数3：服务端ip
 * 参数4：服务端port
 * 返回值：无
 * */
void ReadConf(char *local_ip, char *local_port, char *remote_ip, char *remote_port)
{
    FILE *fp = NULL;
    int file_len = 0;
    char *file_buf = NULL;

    cJSON *json = NULL;
    cJSON *item = NULL;

    fp = fopen("keep_alive.conf", "rb");
    if(NULL == fp)
    {
        DaemonLog(-1, "打开配置文件失败");
        exit(1);
    }
    else
    {
        DaemonLog(0, "打开配置文件成功");
    }

    /*把文件指针移动到文件尾*/
    fseek(fp, 0, SEEK_END);
    /*获取文件内容长度*/
    file_len = ftell(fp);
    /*把文件指针移动到文件首*/
    fseek(fp, 0, SEEK_SET);

    file_buf = (char *)malloc((file_len + 1) * sizeof(char));
    if(NULL == file_buf)
    {
        DaemonLog(-1, "分配内存失败");
        fclose(fp);

        exit(1);
    }
    else
    {
        DaemonLog(0, "分配内存成功");
    }
    memset(file_buf, 0, file_len + 1);

    fread(file_buf, file_len, 1, fp);
    file_buf[file_len] = '\0';
    fclose(fp);

    /*载入JSON字符串*/
    json = cJSON_Parse(file_buf);
    if(NULL == json)
    {
        DaemonLog(-1, "载入JSON字符串失败");
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "载入JSON字符串成功");
    }

    item = cJSON_GetObjectItem(json, "Remote_IP");
    if(NULL == item)
    {
        DaemonLog(-1, "解析Remote_IP失败");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "解析Remote_IP成功");
    }
    memcpy(remote_ip, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Remote_Port");
    if(NULL == item)
    {
        DaemonLog(-1, "解析Remote_Port失败");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "解析Remote_Port成功");
    }
    memcpy(remote_port, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Local_IP");
    if(NULL == item)
    {
        DaemonLog(-1, "解析Local_IP失败");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "解析Local_IP成功");
    }
    memcpy(local_ip, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Local_Port");
    if(NULL == item)
    {
        DaemonLog(-1, "解析Local_Port失败");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "解析Local_Port成功");
    }
    memcpy(local_port, item->valuestring, strlen(item->valuestring));

    cJSON_Delete(json);
    json = NULL;
    free(file_buf);
    file_buf = NULL;
}

/*
 * 函数说明：重新连接到联合电子服务器
 * 参数：无
 * 返回值：1：连接成功，-1：连接失败
 * */
int ReConnection()
{
    //重新连接到联合电子
    DaemonLog(0, "守护进程将在10S后重新连接"); 

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    //线程休眠10s再发送
    select(0, NULL, NULL, NULL, &tv);

    char server_ip[20] = {0};
    char server_port[10] = {0};
    char client_ip[20] = {0};
    char client_port[10] = {0};

    ReadConf(server_ip, server_port, client_ip, client_port);

    struct sockaddr_in client_address;
    bzero(&client_address, sizeof(client_address));
    client_address.sin_family = AF_INET;
    inet_pton(AF_INET, client_ip, &client_address.sin_addr);
    client_address.sin_port = htons(atoi(client_port));
    
    //加锁
    pthread_mutex_lock(&g_client_fd_mutex);

    //重新连接前，需要关闭g_client_fd，避免出现过多的CLOSE_WAIT状态
    if(g_conn_fd != -1)
    {
        close(g_conn_fd);
    }
    close(g_client_fd);

    g_client_fd = socket(PF_INET, SOCK_STREAM, 0);
    if( -1 == g_client_fd )
    {
        DaemonLog(-1, "重新创建socket失败");

        return -1;
    }
    else
    {
        DaemonLog(0, "重新创建socket成功");
    }

    int conn_res = connect(g_client_fd, (struct sockaddr *)&client_address, sizeof(client_address));
    if(conn_res != -1)
    {
        DaemonLog(0, "重新连接到联合电子服务器成功");
    }
    else
    {
        DaemonLog(-1, "重新连接到联合电子服务器失败");

        return -1;
    }

    //解锁
    pthread_mutex_unlock(&g_client_fd_mutex);

    return 1;
}

/*
 * 函数说明：SIGHUP处理函数（重读配置文件）
 * 参数：未使用
 * 返回值：无
 * */
void Hup(int s)
{
    DaemonLog(0, "正在重新读取配置文件");

    /*Learn the new rules*/
    ReReadConfiguration();

    DaemonLog(0, "重新读取配置文件成功");
}

/*
 * 函数说明：SIGPIPE处理函数（连续两次往关闭的socket文件描述符写触发SIGPIPE信号）
 * 参数：未使用
 * 返回值：无
 * */
void Plumber(int s)
{
    DaemonLog(-1, "往关闭的socket文件描述符写数据");

    int res = ReConnection();
    if(res != 1)
    {
        DaemonLog(-1, "连接到联合电子服务器失败，进程结束");
        exit(1);
    }
}

/*
 * 函数说明：SIGTERM处理函数，（kill守护进程时触发）释放所有资源
 * 参数：未使用
 * 返回值：无
 * */
void Term(int s)
{
    ReleaseResource();

    DaemonLog(0, "进程结束\n");

    exit(0);
}

/*
 * 函数说明：线程1的回调函数，用于每40s发送心跳包，维持长连接
 * 参数：无
 * 返回值：无
 * */
void* SendHeartBeat()
{
    int send_res;
    struct timeval tv;

    tv.tv_sec = 40;
    tv.tv_usec = 0;

    while(1)
    {
        send_res = send(g_client_fd, "0000", strlen("0000"), 0);
        if(send_res <= 0)
        {
            DaemonLog(-1, "发送心跳包失败");
        }
        else
        {
            DaemonLog(0, "发送心跳包成功");
            ++g_send_heartbeat_num;
        }

        //线程休眠40s再发送
        select(0, NULL, NULL, NULL, &tv);

        tv.tv_sec = 40;
        tv.tv_usec = 0;

        if( 5 == g_send_heartbeat_num )
        {
            if(0 != g_recv_heartbeat_num )
            {
                pthread_mutex_lock(&g_heartbeat_mutex);
                g_recv_heartbeat_num = 0;
                pthread_mutex_unlock(&g_heartbeat_mutex);

                g_send_heartbeat_num = 0;

                continue;
            }
            else
            {
                //连续发送5个心跳包，联合电子都没有返回心跳包，重连
                int res = ReConnection();
                if(res != 1)
                {
                    DaemonLog(-1, "连接到联合电子服务器失败，进程结束");
                    exit(1);
                }
                pthread_mutex_lock(&g_heartbeat_mutex);
                g_recv_heartbeat_num = 0;
                pthread_mutex_unlock(&g_heartbeat_mutex);

                g_send_heartbeat_num = 0;
            }
        }
    }

    pthread_exit(NULL);

    return NULL;
}

/*
 * 函数说明：线程2的回调函数，用于接收联合电子的数据，非心跳包就转发到农行
 * 参数：无
 * 返回值：无
 * */
void* RecvDataFromEServer()
{
    int recv_res;

    while(1)
    {
        memset(g_client_recv_buf, '\0', BUFFER_SIZE);

        recv_res = recv(g_client_fd, g_client_recv_buf, BUFFER_SIZE, 0);
        if(recv_res < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            else
            {
                DaemonLog(-1, "接收联合电子的数据出错");
            }
        }
        else if( 0 == recv_res )
        {
            DaemonLog(-1, "联合电子关闭连接");

            int res = ReConnection();
            if(res != 1)
            {
                DaemonLog(-1, "连接到联合电子服务器失败，进程结束");
                exit(1);
            }
        }
        else
        {
            DaemonLog(0, "接收联合电子的数据完毕");
        
            DaemonLog(0, "收到联合电子的数据：");
            char hex[1024] = {'\0'};
            EbcToAsc(g_client_recv_buf, hex, recv_res);
            DaemonLog(0, hex);
            /*判断是否是心跳包，是就忽略，不是就转发到conn_fd*/
            int strncmp_res = strncmp("30303030", hex, 8);
            if(0 == strncmp_res)
            {
                pthread_mutex_lock(&g_heartbeat_mutex);
                ++g_recv_heartbeat_num;
                pthread_mutex_unlock(&g_heartbeat_mutex);
                DaemonLog(0, "收到一个心跳包");
            }
            else
            {
                /*把数据转发到农行的客户端g_conn_fd*/ 
                int send_res = send(g_conn_fd, hex, strlen(hex), 0);
                if(send_res <= 0)
                {
                    DaemonLog(-1, "发送到农行数据出错");
                }
                else
                {
                    DaemonLog(0, "发送到农行数据成功");
                }
            }
        }
    }

    pthread_exit(NULL);

    return NULL;
}

/*
 * 函数说明：线程3的回调函数，用于接受农行的连接和接收农行的数据，并转发到联合电子
 * 参数：无
 * 返回值：无
 * */
void* SendDataToEServer()
{
    /*定义客户端地址*/
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    char client_address_ip[INET_ADDRSTRLEN + 1];
    unsigned int client_address_port;
    char port[10];

    int recv_res;

    while(1)
    {
        memset(&client_address, '\0', sizeof(client_address));

        /*接受客户端连接*/
        g_conn_fd = accept(g_server_fd, (struct sockaddr *)&client_address, &client_address_len);
        if(-1 == g_conn_fd)
        {
            DaemonLog(-1, "服务端接受客户连接失败");
            continue;
        }
        else
        {
            DaemonLog(0, "服务端接受客户连接成功");
        }
        
        memset(client_address_ip, '\0', sizeof(client_address_ip));
        inet_ntop(AF_INET, &client_address.sin_addr, client_address_ip, INET_ADDRSTRLEN);
        DaemonLog(0, "连接到的客户端IP为：");
        DaemonLog(0, client_address_ip);

        memset(port, '\0', sizeof(port));
        client_address_port = ntohs(client_address.sin_port);
        sprintf(port, "%d", client_address_port);
        DaemonLog(0, "连接到的客户端port为：");
        DaemonLog(0, port);

        memset(g_server_recv_buf, '\0', BUFFER_SIZE);
        recv_res = recv(g_conn_fd, g_server_recv_buf, BUFFER_SIZE, 0);
        if(recv_res < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            else
            {
                DaemonLog(-1, "接收农行的数据出错");
            }
        }
        else if(0 == recv_res)
        {
            DaemonLog(-1, "农行关闭了连接");
            close(g_conn_fd);
            continue;
        }
        else
        {
            DaemonLog(0, "接收农行的数据完毕");
            DaemonLog(0, "接收到农行的数据为：");
            DaemonLog(0, g_server_recv_buf);

            /*把数据转发到联合电子*/
            int len = recv_res / 2;
            char len_str[10] = {'\0'};
            sprintf(len_str, "%4d", len);
            for(int i = 0; i < 4; ++i)
            {
                if(' ' == len_str[i])
                {
                    len_str[i] = '0';
                }
            }
            DaemonLog(0, "发送到联合电子数据的长度为：");
            DaemonLog(0, len_str);
            int send_res = send(g_client_fd, len_str, strlen(len_str), 0);
            if(send_res < 0)
            {
                DaemonLog(-1, "发送到联合电子数据长度出错");
            }
            else
            {
                DaemonLog(0, "发送到联合电子数据长度成功");
            }
            DaemonLog(0, "发送到联合电子的数据为：");
            DaemonLog(0, g_server_recv_buf);

            AscToEbc(g_server_recv_buf, recv_res);

            send_res = send(g_client_fd, g_server_recv_buf, len, 0);
            if( send_res < 0 )
            {
                DaemonLog(-1, "发送到联合电子数据出错");
            }
            else
            {
                DaemonLog(0, "发送到联合电子数据成功");
            }
        }
    }

    pthread_exit(NULL);

    return NULL;
}

/*
 * 函数说明：管理信号的线程，用于处理守护进程收到的信号
 * 参数：信号集
 * 返回值：无
 * */
void* SignalMgr(void *arg)
{
    sigset_t *set = (sigset_t *)arg;
    int res, sig;

    while(1)
    {
        //sigwait会自动解除信号集的阻塞状态
        res = sigwait(set, &sig);
        if( 0 != res )
        {
            DaemonLog(-1, "处理信号失败");
        }
        else
        {
            switch(sig)
            {
                case SIGHUP:
                    Hup(sig);
                    break;
                case SIGPIPE:
                    Plumber(sig);
                    break;
                case SIGTERM:
                    Term(sig);
                    break;
                default:
                    break;
            }
        }
    }

    return NULL;
}

/*
 * 函数说明：保持长连接
 * 参数：无
 * 返回值：无
 * */
void KeepAlive()
{
    int res;

    /*
     * 配置文件里的地址
     * 客户端IP：Local_IP
     * 客户端端口：Local_Port
     * 服务端IP：Remote_IP
     * 服务端Port：Remote_Port
     * */
    char server_ip[20] = {0};
    char server_port[10] = {0};
    char client_ip[20] = {0};
    char client_port[10] = {0};

    /*
     * 读取配置文件里的IP和端口
     * 中行的数据发送到server_ip,server_port
     * 联合电子的IP和端口client_ip,client_port
     * */
    ReadConf(server_ip, server_port, client_ip, client_port);

    DaemonLog(0, "服务端ip：");
    DaemonLog(0, server_ip);
    DaemonLog(0, "服务端port：");
    DaemonLog(0, server_port);
    DaemonLog(0, "联合电子ip：");
    DaemonLog(0, client_ip);
    DaemonLog(0, "联合电子port：");
    DaemonLog(0, client_port);

    /*创建server_address,作为服务端监听是否有数据过来*/
    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip, &server_address.sin_addr);
    server_address.sin_port = htons( atoi(server_port) );

    /*创建client_address,作为客户端用于连接联合电子的服务器*/
    struct sockaddr_in client_address;
    bzero(&client_address, sizeof(client_address));
    client_address.sin_family = AF_INET;
    inet_pton(AF_INET, client_ip, &client_address.sin_addr);
    client_address.sin_port = htons( atoi(client_port) );

    /*创建一个服务端监听server_ip,server_port*/
    /*建立socket*/
    g_server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(g_server_fd != -1)
    {
        DaemonLog(0, "建立服务端socket成功");
    }
    else
    {
        DaemonLog(-1, "建立服务端socket失败");

        ReleaseResource();

        exit(1);
    }

    int reuse = 1;
    /*在bind之前设置复用端口，避免处于time_wait状态的端口不能使用*/
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /*绑定socket*/
    res = bind(g_server_fd, (struct sockaddr *)&server_address, sizeof(server_address));
    if(res != -1)
    {
        DaemonLog(0, "绑定服务端socket成功");
    }
    else
    {
        DaemonLog(-1, "绑定服务端socket失败");

        ReleaseResource();

        exit(1);
    }

    /*监听socket*/
    res = listen(g_server_fd, 5);
    if(res != -1)
    {
        DaemonLog(0, "监听服务端socket成功");
    }
    else
    {
        DaemonLog(-1, "监听服务端socket失败");

        ReleaseResource();

        exit(1);
    }

    /*创建一个客户端socket连接client_ip,client_port*/
    /*建立socket*/
    g_client_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(g_client_fd != -1)
    {
        DaemonLog(0, "建立客户端socket成功");
    }
    else
    {
        DaemonLog(-1, "建立客户端socket失败");

        ReleaseResource();

        exit(1);
    }
    res = connect(g_client_fd, (struct sockaddr *)&client_address, sizeof(client_address));
    if(res != -1)
    {
        DaemonLog(0, "连接联合电子服务器成功");
    }
    else
    {
        DaemonLog(-1, "连接联合电子服务器失败");

        ReleaseResource();

        exit(1);
    }

    //设置信号掩码
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGTERM);

    if( 0 == pthread_sigmask(SIG_BLOCK, &set, NULL) )
    {
        DaemonLog(0, "设置信号掩码成功");
    }
    else
    {
        DaemonLog(-1, "设置信号掩码失败");

        ReleaseResource();

        exit(1);
    }

    pthread_attr_t attr;

    //初始化线程属性对象
    if( 0 == pthread_attr_init(&attr) )
    {
        DaemonLog(0, "初始化线程属性对象成功");
    }
    else
    {
        DaemonLog(-1, "初始化线程属性对象失败");

        ReleaseResource();

        exit(1);
    }

    //设置线程为脱离线程
    if( 0 == pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) )
    {
        DaemonLog(0, "设置线程属性为脱离线程成功");
    }
    else
    {
        DaemonLog(-1, "设置线程属性为脱离线程失败");

        ReleaseResource();

        exit(1);
    }

    //用于管理信号的线程,设置为脱离线程
    pthread_t signal_mgr;

    if( 0 == pthread_create(&signal_mgr, &attr, SignalMgr, (void *)&set) )
    {
        DaemonLog(0, "创建信号管理线程成功");
    }
    else
    {
        DaemonLog(-1, "创建信号管理线程失败");

        ReleaseResource();

        exit(1);
    }

    //销毁线程属性对象
    if( 0 == pthread_attr_destroy(&attr) )
    {
        DaemonLog(0, "销毁线程属性对象成功");
    }
    else
    {
        DaemonLog(-1, "销毁线程属性对象失败");

        ReleaseResource();

        exit(1);
    }

    pthread_t tid1, tid2, tid3;

    //线程1:主动发送心跳包
    if( 0 == pthread_create(&tid1, NULL, SendHeartBeat, NULL) ) 
    {
        DaemonLog(0, "创建发送心跳包线程成功");
    }
    else
    {
        DaemonLog(-1, "创建发送心跳包线程失败");

        ReleaseResource();

        exit(1);
    }

    //线程2:接收联合电子的数据，非心跳包就转发到农行
    if( 0 == pthread_create(&tid2, NULL, RecvDataFromEServer, NULL) )
    {
        DaemonLog(0, "创建接收联合电子数据的线程成功");
    }
    else
    {
        DaemonLog(-1, "创建接收联合电子数据的线程失败");

        ReleaseResource();

        exit(1);
    }

    //线程3:接受农行的连接和接收农行的数据，并转发到联合电子
    if( 0 == pthread_create(&tid3, NULL, SendDataToEServer, NULL) )
    {
        DaemonLog(0, "创建接受农行的连接和接收农行的数据的线程成功");
    }
    else
    {
        DaemonLog(-1, "创建接受农行的连接和接收农行的数据的线程失败");

        ReleaseResource();

        exit(1);
    }

    if( 0 == pthread_join(tid1, NULL) )
    {
        DaemonLog(0, "线程1退出成功");
    }
    else
    {
        DaemonLog(-1, "线程1退出失败");
    }

    if( 0 == pthread_join(tid2, NULL) )
    {
        DaemonLog(0, "线程2退出成功");
    }
    else
    {
        DaemonLog(-1, "线程2退出失败");
    }

    if( 0 == pthread_join(tid3, NULL) )
    {
        DaemonLog(0, "线程3退出成功");
    }
    else
    {
        DaemonLog(-1, "线程3退出失败");
    }
    
    ReleaseResource();
}



