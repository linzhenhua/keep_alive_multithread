#ifndef DAEMON_H
#define DAEMON_H

#include <stdio.h>

/*
* exit��0�����������г����˳�����
* exit��1�������������е����˳�����
*/

/*��¼��־����Ҫ��ʱ�䣬�����ļ��������кţ����������������룬������Ϣ*/
/*
 * ����˵������¼�ػ�����ִ�й���
 * ����1��������
 * ����2��������Ϣ
 * ����3���ļ���
 * ����4���к�
 * ����5����������
 * ����ֵ����
 * */
void _DaemonLog(const int error_code, const char *error_msg, const char *file_name, int line, const char *func);

/*
 * ����˵�����ͷ�������Դ
 * ��������
 * ����ֵ����
 * */
void ReleaseResource();

/*
 * ����˵�����ض������ļ�
 * ��������
 * ����ֵ����
 * */
void ReReadConfiguration();

/*
 * ����˵����SIGHUP�����������ض������ļ���
 * ������δʹ��
 * ����ֵ����
 * */
void Hup(int s);

/*
 * ����˵����SIGPIPE���������������������رյ�socket�ļ�������д����SIGPIPE�źţ�
 * ������δʹ��
 * ����ֵ����
 * */
void Plumber(int s);

/*
 * ����˵����SIGTERM������������kill�ػ�����ʱ�������ͷ�������Դ
 * ������δʹ��
 * ����ֵ����
 * */
void Term(int s);

/*
 * ����˵��������һ���ػ�����
 * ��̹���
 * 1������Ҫ�����ǵ���umask���ļ�ģʽ��������������Ϊ0
 * 2������fork��Ȼ��ʹ�������˳���exit��
 * 3������setsid�Դ���һ���»Ự
 * 4������ǰ����Ŀ¼����Ϊ��Ŀ¼
 * 5���رղ�����Ҫ���ļ�������
 * 6��ĳЩ�ػ����̴�/dev/nullʹ������ļ�������0,1,2�������κ�һ����ͼ����׼���롢
 * д��׼������׼�����Ŀ����̶���������κ�Ч��
 *
 * ������δʹ��
 * ����ֵ����
 * */
void InitDaemon();

/*
 * ����˵������ȡ�����ļ�
 * ����1������ip
 * ����2������port
 * ����3�������ip
 * ����4�������port
 * ����ֵ����
 * */
void ReadConf(char *Local_Ip, char *Local_Port, char *Remote_Ip, char *Remote_Port);

/*
 * ����˵�����������ӵ����ϵ��ӷ�����
 * ������δʹ��
 * ����ֵ��1�����ӳɹ���-1������ʧ��
 * */
int ReConnection();

/*
 * ����˵�����߳�1�Ļص�����������ÿ40s������������ά�ֳ�����
 * ��������
 * ����ֵ����
 * */
void* SendHeartBeat();

/*
 * ����˵�����߳�2�Ļص����������ڽ������ϵ��ӵ����ݣ�����������ת����ũ��
 * ��������
 * ����ֵ����
 * */
void* RecvDataFromEServer();

/*
 * ����˵�����߳�3�Ļص����������ڽ���ũ�е����Ӻͽ���ũ�е����ݣ���ת�������ϵ���
 * ��������
 * ����ֵ����
 * */
void* SendDataToEServer();

/*
 * ����˵���������źŵ��̣߳����ڴ����ػ������յ����ź�
 * �������źż�
 * ����ֵ����
 * */
void* SignalMgr(void *arg);

/*
 * ����˵�������ֳ�����
 * ��������
 * ����ֵ����
 * */
void KeepAlive();

#endif // DAEMON_H