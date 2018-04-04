#include <stdio.h>
#include <pthread.h>

#include "daemon.h"

extern pthread_mutex_t g_log_mutex;
extern pthread_mutex_t g_client_fd_mutex;
extern pthread_mutex_t g_heartbeat_mutex;

int main(int argc, char **argv)
{
    //初始化线程锁
    if( 0 != pthread_mutex_init(&g_log_mutex, NULL) )
    {
        printf("创建线程锁失败\n");

        return -1;
    }

    if( 0 != pthread_mutex_init(&g_client_fd_mutex, NULL) )
    {
        printf("创建线程锁失败\n");

        return -1;
    }

    if( 0 != pthread_mutex_init(&g_heartbeat_mutex, NULL) )
    {
        printf("创建线程锁失败\n");

        return -1;
    }

    /*开启守护进程*/
    InitDaemon();

    KeepAlive();

    //释放锁资源
    pthread_mutex_destroy(&g_log_mutex);
    pthread_mutex_destroy(&g_client_fd_mutex);

    return 0;
}
