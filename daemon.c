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

//�����socket������
int g_server_fd = -1;
//�ͻ���socket������
int g_client_fd = -1;
//���ӵ�g_server_fd��socket�ļ�������
int g_conn_fd = -1;
//��־�ļ�������
FILE *g_file_fd = NULL;

//�߳���
pthread_mutex_t g_log_mutex;
pthread_mutex_t g_client_fd_mutex;
pthread_mutex_t g_heartbeat_mutex;

/*���͹��������ݱ��浽����*/
char g_server_recv_buf[BUFFER_SIZE];
/*���ϵ��ӷ��͹��������ݱ��浽����*/
char g_client_recv_buf[BUFFER_SIZE];

//����������������ÿ����һ������������1
int g_send_heartbeat_num = 0;
//����������������ÿ����һ������������1
int g_recv_heartbeat_num = 0;

/*
 * ����˵�����ͷ�������Դ
 * ��������
 * ����ֵ����
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
 * ����˵������¼�ػ�����ִ�й���
 * ����1��������
 * ����2��������Ϣ
 * ����3���ļ���
 * ����4���к�
 * ����5��������
 * ����ֵ����
 * */
void _DaemonLog(const int error_code, const char *error_msg, const char *file_name, int line, const char *func)
{
    //����
    pthread_mutex_lock(&g_log_mutex);

    struct timeval tv;
    struct tm *now_time;

    memset(&tv, 0, sizeof(struct timeval));

    gettimeofday(&tv, NULL);

    now_time = localtime(&tv.tv_sec);

    char log_name[30] = {'\0'};

    //��־����
    sprintf(log_name, "daemon_%04d%02d%02d.log", now_time->tm_year+1900, now_time->tm_mon+1, now_time->tm_mday);

    //���ļ�
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

    //����
    pthread_mutex_unlock(&g_log_mutex);
}

/*
 * ����˵�����ض������ļ�
 * ��������
 * ����ֵ����
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
        DaemonLog(-1, "�������ļ�ʧ��");
    }
    else
    {
        DaemonLog(0, "�������ļ��ɹ�");
    }

    /*���ļ�ָ���ƶ����ļ�β*/
    fseek(fp, 0, SEEK_END);
    /*��ȡ�ļ����ݳ���*/
    file_len = ftell(fp);
    /*���ļ�ָ���ƶ����ļ���*/
    fseek(fp, 0, SEEK_SET);

    pszFileBuf = (char *)malloc((file_len + 1) * sizeof(char));
    if(NULL == pszFileBuf)
    {
        DaemonLog(-1, "�����ڴ�ʧ��");
        fclose(fp);

        exit(1);
    }
    else
    {
        DaemonLog(0, "�����ڴ�ɹ�");
    }
    memset(pszFileBuf, 0, file_len + 1);

    fread(pszFileBuf, file_len, 1, fp);
    pszFileBuf[file_len] = '\0';
    fclose(fp);

    /*����JSON�ַ���*/
    pszJson = cJSON_Parse(pszFileBuf);
    if(NULL == pszJson)
    {
        DaemonLog(-1, "����JSON�ַ���ʧ��");
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "����JSON�ַ����ɹ�");
    }

    pszItem = cJSON_GetObjectItem(pszJson, "Remote_IP");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "����Remote_IPʧ��");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "����Remote_IP�ɹ�");
    }
    memcpy(pszRemoteIp, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Remote_Port");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "����Remote_Portʧ��");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "����Remote_Port�ɹ�");
    }
    memcpy(pszRemotePort, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Local_IP");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "����Local_IPʧ��");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "����Local_IP�ɹ�");
    }
    memcpy(pszLocalIp, pszItem->valuestring, strlen(pszItem->valuestring));

    pszItem = cJSON_GetObjectItem(pszJson, "Local_Port");
    if(NULL == pszItem)
    {
        DaemonLog(-1, "����Local_Portʧ��");
        cJSON_Delete(pszJson);
        pszJson = NULL;
        free(pszFileBuf);
        pszFileBuf = NULL;
    }
    else
    {
        DaemonLog(0, "����Local_Port�ɹ�");
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
 * ��̹���
 * 1������Ҫ�����ǵ���umask���ļ�ģʽ��������������Ϊ0
 * 2������fork��Ȼ��ʹ�������˳���exit��
 * 3������setsid�Դ���һ���»Ự
 * 4������ǰ����Ŀ¼����Ϊ��Ŀ¼
 * 5���رղ�����Ҫ���ļ�������
 * 6��ĳЩ�ػ����̴�/dev/nullʹ������ļ�������0,1,2�������κ�һ����ͼ����׼���롢
 * д��׼������׼����Ŀ����̶���������κ�Ч��
 *
 * ����˵��������һ���ػ�����
 * ������δʹ��
 * ����ֵ����
 * */
void InitDaemon()
{
    int i, fd;
    pid_t pid;

    /*תΪ��̨����*/
    if( (pid = fork()) < 0 )
    {
        DaemonLog(-1, "�����ػ�����ʧ��");
        exit(1);
    }
    else if(0 != pid)
    {
        DaemonLog(0, "�������˳�");
        exit(0);
    }

    /*�����µĻỰ�飬��Ϊ�Ự�鳤�ͽ����鳤*/
    setsid();

    /*ʹ�䲻���ǻỰ�鳤�����ܿ����ն�*/
    if( (pid = fork()) < 0 )
    {
        DaemonLog(-1, "�����ػ�����ʧ��");
        exit(1);
    }
    else if(0 != pid)
    {
        DaemonLog(0, "�������˳�");
        exit(0);
    }

    /*�ر��Ѿ��򿪵��ļ��������������˷�ϵͳ��Դ*/
    for(i = 0; i < NOFILE; ++i)
    {
        close(i);
    }

    /*���Ĺ���Ŀ¼*/
    int res;
    res = chdir(DIR);
    if( res < 0 )
    {
        DaemonLog(-1, "���Ĺ���Ŀ¼ʧ��");
        exit(1);
    }
    else
    {
        DaemonLog(0, "���Ĺ���Ŀ¼�ɹ�");
    }

    /*�����ļ����룬ʹ�ļ�����Ȩ�޲����ܸ�����Ӱ��*/
    umask(0);

    /*�ض����������*/
    fd = open("/dev/null", O_RDWR);
    if(fd < 0)
    {
        DaemonLog(-1, "�ض����������ʧ��");
        exit(1);
    }
    else
    {
        DaemonLog(0, "�ض�����������ɹ�");
    }

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

/*
 * ����˵������ȡ�����ļ�
 * ����1������ip
 * ����2������port
 * ����3�������ip
 * ����4�������port
 * ����ֵ����
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
        DaemonLog(-1, "�������ļ�ʧ��");
        exit(1);
    }
    else
    {
        DaemonLog(0, "�������ļ��ɹ�");
    }

    /*���ļ�ָ���ƶ����ļ�β*/
    fseek(fp, 0, SEEK_END);
    /*��ȡ�ļ����ݳ���*/
    file_len = ftell(fp);
    /*���ļ�ָ���ƶ����ļ���*/
    fseek(fp, 0, SEEK_SET);

    file_buf = (char *)malloc((file_len + 1) * sizeof(char));
    if(NULL == file_buf)
    {
        DaemonLog(-1, "�����ڴ�ʧ��");
        fclose(fp);

        exit(1);
    }
    else
    {
        DaemonLog(0, "�����ڴ�ɹ�");
    }
    memset(file_buf, 0, file_len + 1);

    fread(file_buf, file_len, 1, fp);
    file_buf[file_len] = '\0';
    fclose(fp);

    /*����JSON�ַ���*/
    json = cJSON_Parse(file_buf);
    if(NULL == json)
    {
        DaemonLog(-1, "����JSON�ַ���ʧ��");
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "����JSON�ַ����ɹ�");
    }

    item = cJSON_GetObjectItem(json, "Remote_IP");
    if(NULL == item)
    {
        DaemonLog(-1, "����Remote_IPʧ��");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "����Remote_IP�ɹ�");
    }
    memcpy(remote_ip, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Remote_Port");
    if(NULL == item)
    {
        DaemonLog(-1, "����Remote_Portʧ��");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "����Remote_Port�ɹ�");
    }
    memcpy(remote_port, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Local_IP");
    if(NULL == item)
    {
        DaemonLog(-1, "����Local_IPʧ��");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "����Local_IP�ɹ�");
    }
    memcpy(local_ip, item->valuestring, strlen(item->valuestring));

    item = cJSON_GetObjectItem(json, "Local_Port");
    if(NULL == item)
    {
        DaemonLog(-1, "����Local_Portʧ��");
        cJSON_Delete(json);
        json = NULL;
        free(file_buf);
        file_buf = NULL;
        exit(1);
    }
    else
    {
        DaemonLog(0, "����Local_Port�ɹ�");
    }
    memcpy(local_port, item->valuestring, strlen(item->valuestring));

    cJSON_Delete(json);
    json = NULL;
    free(file_buf);
    file_buf = NULL;
}

/*
 * ����˵�����������ӵ����ϵ��ӷ�����
 * ��������
 * ����ֵ��1�����ӳɹ���-1������ʧ��
 * */
int ReConnection()
{
    //�������ӵ����ϵ���
    DaemonLog(0, "�ػ����̽���10S����������"); 

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    //�߳�����10s�ٷ���
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
    
    //����
    pthread_mutex_lock(&g_client_fd_mutex);

    //��������ǰ����Ҫ�ر�g_client_fd��������ֹ����CLOSE_WAIT״̬
    if(g_conn_fd != -1)
    {
        close(g_conn_fd);
    }
    close(g_client_fd);

    g_client_fd = socket(PF_INET, SOCK_STREAM, 0);
    if( -1 == g_client_fd )
    {
        DaemonLog(-1, "���´���socketʧ��");

        return -1;
    }
    else
    {
        DaemonLog(0, "���´���socket�ɹ�");
    }

    int conn_res = connect(g_client_fd, (struct sockaddr *)&client_address, sizeof(client_address));
    if(conn_res != -1)
    {
        DaemonLog(0, "�������ӵ����ϵ��ӷ������ɹ�");
    }
    else
    {
        DaemonLog(-1, "�������ӵ����ϵ��ӷ�����ʧ��");

        return -1;
    }

    //����
    pthread_mutex_unlock(&g_client_fd_mutex);

    return 1;
}

/*
 * ����˵����SIGHUP���������ض������ļ���
 * ������δʹ��
 * ����ֵ����
 * */
void Hup(int s)
{
    DaemonLog(0, "�������¶�ȡ�����ļ�");

    /*Learn the new rules*/
    ReReadConfiguration();

    DaemonLog(0, "���¶�ȡ�����ļ��ɹ�");
}

/*
 * ����˵����SIGPIPE�������������������رյ�socket�ļ�������д����SIGPIPE�źţ�
 * ������δʹ��
 * ����ֵ����
 * */
void Plumber(int s)
{
    DaemonLog(-1, "���رյ�socket�ļ�������д����");

    int res = ReConnection();
    if(res != 1)
    {
        DaemonLog(-1, "���ӵ����ϵ��ӷ�����ʧ�ܣ����̽���");
        exit(1);
    }
}

/*
 * ����˵����SIGTERM����������kill�ػ�����ʱ�������ͷ�������Դ
 * ������δʹ��
 * ����ֵ����
 * */
void Term(int s)
{
    ReleaseResource();

    DaemonLog(0, "���̽���\n");

    exit(0);
}

/*
 * ����˵�����߳�1�Ļص�����������ÿ40s������������ά�ֳ�����
 * ��������
 * ����ֵ����
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
            DaemonLog(-1, "����������ʧ��");
        }
        else
        {
            DaemonLog(0, "�����������ɹ�");
            ++g_send_heartbeat_num;
        }

        //�߳�����40s�ٷ���
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
                //��������5�������������ϵ��Ӷ�û�з���������������
                int res = ReConnection();
                if(res != 1)
                {
                    DaemonLog(-1, "���ӵ����ϵ��ӷ�����ʧ�ܣ����̽���");
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
 * ����˵�����߳�2�Ļص����������ڽ������ϵ��ӵ����ݣ�����������ת����ũ��
 * ��������
 * ����ֵ����
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
                DaemonLog(-1, "�������ϵ��ӵ����ݳ���");
            }
        }
        else if( 0 == recv_res )
        {
            DaemonLog(-1, "���ϵ��ӹر�����");

            int res = ReConnection();
            if(res != 1)
            {
                DaemonLog(-1, "���ӵ����ϵ��ӷ�����ʧ�ܣ����̽���");
                exit(1);
            }
        }
        else
        {
            DaemonLog(0, "�������ϵ��ӵ��������");
        
            DaemonLog(0, "�յ����ϵ��ӵ����ݣ�");
            char hex[1024] = {'\0'};
            EbcToAsc(g_client_recv_buf, hex, recv_res);
            DaemonLog(0, hex);
            /*�ж��Ƿ������������Ǿͺ��ԣ����Ǿ�ת����conn_fd*/
            int strncmp_res = strncmp("30303030", hex, 8);
            if(0 == strncmp_res)
            {
                pthread_mutex_lock(&g_heartbeat_mutex);
                ++g_recv_heartbeat_num;
                pthread_mutex_unlock(&g_heartbeat_mutex);
                DaemonLog(0, "�յ�һ��������");
            }
            else
            {
                /*������ת����ũ�еĿͻ���g_conn_fd*/ 
                int send_res = send(g_conn_fd, hex, strlen(hex), 0);
                if(send_res <= 0)
                {
                    DaemonLog(-1, "���͵�ũ�����ݳ���");
                }
                else
                {
                    DaemonLog(0, "���͵�ũ�����ݳɹ�");
                }
            }
        }
    }

    pthread_exit(NULL);

    return NULL;
}

/*
 * ����˵�����߳�3�Ļص����������ڽ���ũ�е����Ӻͽ���ũ�е����ݣ���ת�������ϵ���
 * ��������
 * ����ֵ����
 * */
void* SendDataToEServer()
{
    /*����ͻ��˵�ַ*/
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    char client_address_ip[INET_ADDRSTRLEN + 1];
    unsigned int client_address_port;
    char port[10];

    int recv_res;

    while(1)
    {
        memset(&client_address, '\0', sizeof(client_address));

        /*���ܿͻ�������*/
        g_conn_fd = accept(g_server_fd, (struct sockaddr *)&client_address, &client_address_len);
        if(-1 == g_conn_fd)
        {
            DaemonLog(-1, "����˽��ܿͻ�����ʧ��");
            continue;
        }
        else
        {
            DaemonLog(0, "����˽��ܿͻ����ӳɹ�");
        }
        
        memset(client_address_ip, '\0', sizeof(client_address_ip));
        inet_ntop(AF_INET, &client_address.sin_addr, client_address_ip, INET_ADDRSTRLEN);
        DaemonLog(0, "���ӵ��Ŀͻ���IPΪ��");
        DaemonLog(0, client_address_ip);

        memset(port, '\0', sizeof(port));
        client_address_port = ntohs(client_address.sin_port);
        sprintf(port, "%d", client_address_port);
        DaemonLog(0, "���ӵ��Ŀͻ���portΪ��");
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
                DaemonLog(-1, "����ũ�е����ݳ���");
            }
        }
        else if(0 == recv_res)
        {
            DaemonLog(-1, "ũ�йر�������");
            close(g_conn_fd);
            continue;
        }
        else
        {
            DaemonLog(0, "����ũ�е��������");
            DaemonLog(0, "���յ�ũ�е�����Ϊ��");
            DaemonLog(0, g_server_recv_buf);

            /*������ת�������ϵ���*/
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
            DaemonLog(0, "���͵����ϵ������ݵĳ���Ϊ��");
            DaemonLog(0, len_str);
            int send_res = send(g_client_fd, len_str, strlen(len_str), 0);
            if(send_res < 0)
            {
                DaemonLog(-1, "���͵����ϵ������ݳ��ȳ���");
            }
            else
            {
                DaemonLog(0, "���͵����ϵ������ݳ��ȳɹ�");
            }
            DaemonLog(0, "���͵����ϵ��ӵ�����Ϊ��");
            DaemonLog(0, g_server_recv_buf);

            AscToEbc(g_server_recv_buf, recv_res);

            send_res = send(g_client_fd, g_server_recv_buf, len, 0);
            if( send_res < 0 )
            {
                DaemonLog(-1, "���͵����ϵ������ݳ���");
            }
            else
            {
                DaemonLog(0, "���͵����ϵ������ݳɹ�");
            }
        }
    }

    pthread_exit(NULL);

    return NULL;
}

/*
 * ����˵���������źŵ��̣߳����ڴ����ػ������յ����ź�
 * �������źż�
 * ����ֵ����
 * */
void* SignalMgr(void *arg)
{
    sigset_t *set = (sigset_t *)arg;
    int res, sig;

    while(1)
    {
        //sigwait���Զ�����źż�������״̬
        res = sigwait(set, &sig);
        if( 0 != res )
        {
            DaemonLog(-1, "�����ź�ʧ��");
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
 * ����˵�������ֳ�����
 * ��������
 * ����ֵ����
 * */
void KeepAlive()
{
    int res;

    /*
     * �����ļ���ĵ�ַ
     * �ͻ���IP��Local_IP
     * �ͻ��˶˿ڣ�Local_Port
     * �����IP��Remote_IP
     * �����Port��Remote_Port
     * */
    char server_ip[20] = {0};
    char server_port[10] = {0};
    char client_ip[20] = {0};
    char client_port[10] = {0};

    /*
     * ��ȡ�����ļ����IP�Ͷ˿�
     * ���е����ݷ��͵�server_ip,server_port
     * ���ϵ��ӵ�IP�Ͷ˿�client_ip,client_port
     * */
    ReadConf(server_ip, server_port, client_ip, client_port);

    DaemonLog(0, "�����ip��");
    DaemonLog(0, server_ip);
    DaemonLog(0, "�����port��");
    DaemonLog(0, server_port);
    DaemonLog(0, "���ϵ���ip��");
    DaemonLog(0, client_ip);
    DaemonLog(0, "���ϵ���port��");
    DaemonLog(0, client_port);

    /*����server_address,��Ϊ����˼����Ƿ������ݹ���*/
    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip, &server_address.sin_addr);
    server_address.sin_port = htons( atoi(server_port) );

    /*����client_address,��Ϊ�ͻ��������������ϵ��ӵķ�����*/
    struct sockaddr_in client_address;
    bzero(&client_address, sizeof(client_address));
    client_address.sin_family = AF_INET;
    inet_pton(AF_INET, client_ip, &client_address.sin_addr);
    client_address.sin_port = htons( atoi(client_port) );

    /*����һ������˼���server_ip,server_port*/
    /*����socket*/
    g_server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(g_server_fd != -1)
    {
        DaemonLog(0, "���������socket�ɹ�");
    }
    else
    {
        DaemonLog(-1, "���������socketʧ��");

        ReleaseResource();

        exit(1);
    }

    int reuse = 1;
    /*��bind֮ǰ���ø��ö˿ڣ����⴦��time_wait״̬�Ķ˿ڲ���ʹ��*/
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /*��socket*/
    res = bind(g_server_fd, (struct sockaddr *)&server_address, sizeof(server_address));
    if(res != -1)
    {
        DaemonLog(0, "�󶨷����socket�ɹ�");
    }
    else
    {
        DaemonLog(-1, "�󶨷����socketʧ��");

        ReleaseResource();

        exit(1);
    }

    /*����socket*/
    res = listen(g_server_fd, 5);
    if(res != -1)
    {
        DaemonLog(0, "���������socket�ɹ�");
    }
    else
    {
        DaemonLog(-1, "���������socketʧ��");

        ReleaseResource();

        exit(1);
    }

    /*����һ���ͻ���socket����client_ip,client_port*/
    /*����socket*/
    g_client_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(g_client_fd != -1)
    {
        DaemonLog(0, "�����ͻ���socket�ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����ͻ���socketʧ��");

        ReleaseResource();

        exit(1);
    }
    res = connect(g_client_fd, (struct sockaddr *)&client_address, sizeof(client_address));
    if(res != -1)
    {
        DaemonLog(0, "�������ϵ��ӷ������ɹ�");
    }
    else
    {
        DaemonLog(-1, "�������ϵ��ӷ�����ʧ��");

        ReleaseResource();

        exit(1);
    }

    //�����ź�����
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGTERM);

    if( 0 == pthread_sigmask(SIG_BLOCK, &set, NULL) )
    {
        DaemonLog(0, "�����ź�����ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����ź�����ʧ��");

        ReleaseResource();

        exit(1);
    }

    pthread_attr_t attr;

    //��ʼ���߳����Զ���
    if( 0 == pthread_attr_init(&attr) )
    {
        DaemonLog(0, "��ʼ���߳����Զ���ɹ�");
    }
    else
    {
        DaemonLog(-1, "��ʼ���߳����Զ���ʧ��");

        ReleaseResource();

        exit(1);
    }

    //�����߳�Ϊ�����߳�
    if( 0 == pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) )
    {
        DaemonLog(0, "�����߳�����Ϊ�����̳߳ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����߳�����Ϊ�����߳�ʧ��");

        ReleaseResource();

        exit(1);
    }

    //���ڹ����źŵ��߳�,����Ϊ�����߳�
    pthread_t signal_mgr;

    if( 0 == pthread_create(&signal_mgr, &attr, SignalMgr, (void *)&set) )
    {
        DaemonLog(0, "�����źŹ����̳߳ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����źŹ����߳�ʧ��");

        ReleaseResource();

        exit(1);
    }

    //�����߳����Զ���
    if( 0 == pthread_attr_destroy(&attr) )
    {
        DaemonLog(0, "�����߳����Զ���ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����߳����Զ���ʧ��");

        ReleaseResource();

        exit(1);
    }

    pthread_t tid1, tid2, tid3;

    //�߳�1:��������������
    if( 0 == pthread_create(&tid1, NULL, SendHeartBeat, NULL) ) 
    {
        DaemonLog(0, "���������������̳߳ɹ�");
    }
    else
    {
        DaemonLog(-1, "���������������߳�ʧ��");

        ReleaseResource();

        exit(1);
    }

    //�߳�2:�������ϵ��ӵ����ݣ�����������ת����ũ��
    if( 0 == pthread_create(&tid2, NULL, RecvDataFromEServer, NULL) )
    {
        DaemonLog(0, "�����������ϵ������ݵ��̳߳ɹ�");
    }
    else
    {
        DaemonLog(-1, "�����������ϵ������ݵ��߳�ʧ��");

        ReleaseResource();

        exit(1);
    }

    //�߳�3:����ũ�е����Ӻͽ���ũ�е����ݣ���ת�������ϵ���
    if( 0 == pthread_create(&tid3, NULL, SendDataToEServer, NULL) )
    {
        DaemonLog(0, "��������ũ�е����Ӻͽ���ũ�е����ݵ��̳߳ɹ�");
    }
    else
    {
        DaemonLog(-1, "��������ũ�е����Ӻͽ���ũ�е����ݵ��߳�ʧ��");

        ReleaseResource();

        exit(1);
    }

    if( 0 == pthread_join(tid1, NULL) )
    {
        DaemonLog(0, "�߳�1�˳��ɹ�");
    }
    else
    {
        DaemonLog(-1, "�߳�1�˳�ʧ��");
    }

    if( 0 == pthread_join(tid2, NULL) )
    {
        DaemonLog(0, "�߳�2�˳��ɹ�");
    }
    else
    {
        DaemonLog(-1, "�߳�2�˳�ʧ��");
    }

    if( 0 == pthread_join(tid3, NULL) )
    {
        DaemonLog(0, "�߳�3�˳��ɹ�");
    }
    else
    {
        DaemonLog(-1, "�߳�3�˳�ʧ��");
    }
    
    ReleaseResource();
}



