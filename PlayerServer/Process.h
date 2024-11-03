#pragma once
#include "Function.h"
#include <memory.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

class CProcess
{
public:
    CProcess() {
        m_func = NULL;
        memset(pipes, 0, sizeof(pipes));
    }
    ~CProcess() {
        if (m_func != NULL) {
            delete m_func;
            m_func = NULL;
        }
    }

    template<typename _FUNCTION_, typename... _ARGS_>
    int SetEntryFunction(_FUNCTION_ func, _ARGS_&&... args)
    {
        m_func = new CFunction<_FUNCTION_, _ARGS_...>(func, args...);
        return 0;
    }

    int CreateSubProcess() {
        if (m_func == NULL) return -1;
        //创建管道
        int ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, pipes);//AF_LOCAL表示本地通信
        if (ret == -1)return -2;
        //创建子进程
        pid_t pid = fork();
        if (pid == -1) return -3;
        if (pid == 0) {
            //这里是子进程
            close(pipes[1]);//关闭写端
            pipes[1] = 0;
            ret = (*m_func)();
            exit(0);
        }
        //这里是主进程
        close(pipes[0]);//关闭读端
        pipes[0] = 0;
        m_pid = pid;
        return 0;
    }

    //主进程完成，发送fd给子进程
    int SendFD(int fd)
    {
        struct msghdr msg = { 0 };
        iovec iov[2];
        char buf[2][10] = { "edoyun", "jueding" };
        iov[0].iov_base = buf[0];
        iov[0].iov_len = sizeof(buf[0]);
        iov[1].iov_base = buf[1];
        iov[1].iov_len = sizeof(buf[1]);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        //下面的数据才是需要传递的
        //cmsghdr *cmsg = new cmsghdr;
        //bzero(cmsg, sizeof(cmsghdr));
        cmsghdr* cmsg = (cmsghdr*)calloc(1, CMSG_LEN(sizeof(int)));//calloc分配内存，并初始化为0
        if (cmsg == NULL) return -1;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        *(int*)CMSG_DATA(cmsg) = fd;//将fd写入cmsg
        msg.msg_control = cmsg;
        msg.msg_controllen = cmsg->cmsg_len;
        ssize_t ret = sendmsg(pipes[1], &msg, 0);
        //delete cmsg;
        free(cmsg);
        if (ret == -1) {
            return -2;
        }
        return 0;
    }

    int SendSocket(int fd, const sockaddr_in* addrin)
    {
        struct msghdr msg = { 0 };
        iovec iov;
        char buf[20] = "";
        bzero(&msg, sizeof(msg));
        memcpy(buf, addrin, sizeof(sockaddr_in));
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        //下面的数据才是需要传递的
        //cmsghdr *cmsg = new cmsghdr;
        //bzero(cmsg, sizeof(cmsghdr));
        cmsghdr* cmsg = (cmsghdr*)calloc(1, CMSG_LEN(sizeof(int)));//calloc分配内存，并初始化为0
        if (cmsg == NULL) return -1;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        *(int*)CMSG_DATA(cmsg) = fd;//将fd写入cmsg
        msg.msg_control = cmsg;
        msg.msg_controllen = cmsg->cmsg_len;
        ssize_t ret = sendmsg(pipes[1], &msg, 0);
        //delete cmsg;
        free(cmsg);
        if (ret == -1) {
            printf("errno %d msg:%s\n", errno, strerror(errno));
            return -2;
        }
        return 0;
    }

    //子进程完成，接收fd
    int RecvFD(int& fd)
    {
        msghdr msg;
        iovec iov[2];
        char buf[][10] = { "","" };
        iov[0].iov_base = buf[0];
        iov[0].iov_len = sizeof(buf[0]);
        iov[1].iov_base = buf[1];
        iov[1].iov_len = sizeof(buf[1]);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        cmsghdr* cmsg = (cmsghdr*)calloc(1, CMSG_LEN(sizeof(int)));
        if (cmsg == NULL) return -1;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        msg.msg_control = cmsg;
        msg.msg_controllen = CMSG_LEN(sizeof(int));
        ssize_t ret = recvmsg(pipes[0], &msg, 0);
        if (ret == -1) {
            free(cmsg);
            return -2;
        }
        fd = *(int*)CMSG_DATA(cmsg);//从cmsg中读取fd
        free(cmsg);
        return 0;
    }

    int RecvSocket(int& fd, sockaddr_in* addrin)
    {
        msghdr msg;
        iovec iov;
        char buf[20] = "";
        bzero(&msg, sizeof(msg));
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        cmsghdr* cmsg = (cmsghdr*)calloc(1, CMSG_LEN(sizeof(int)));
        if (cmsg == NULL) return -1;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        msg.msg_control = cmsg;
        msg.msg_controllen = CMSG_LEN(sizeof(int));
        ssize_t ret = recvmsg(pipes[0], &msg, 0);
        if (ret == -1) {
            free(cmsg);
            return -2;
        }
        memcpy(addrin, buf, sizeof(sockaddr_in));//从cmsg中读取fd
        fd = *(int*)CMSG_DATA(cmsg);//从cmsg中读取fd
        free(cmsg);
        return 0;
    }

    static int SwichDeamon() {
        pid_t ret = fork();
        if (ret == -1)return -1;
        if (ret > 0)exit(0);//主进程到此为止
        //子进程内容如下
        ret = setsid();
        if (ret == -1)return -2;//失败，则返回
        ret = fork();
        if (ret == -1)return -3;
        if (ret > 0)exit(0);//子进程到此为止
        //孙进程内容如下，进入守护状态
        for (int i = 0; i < 3; i++)close(i);
        umask(0);
        signal(SIGCHLD, SIG_IGN);
        return 0;
    }
private:
    CFunctionBase* m_func;
    pid_t m_pid;
    int pipes[2];//用于进程间通信
};