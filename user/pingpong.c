#include "kernel/types.h"
#include "kernel/stat.h" 
#include "user/user.h"   

#define BUF 16  //缓冲区大小

int main (int argc, char *argv[]) { 

    int p[2];      //存储管道的文件描述符
    char buf[BUF]; //缓冲区

    pipe(p);          //创建管道，p[0]是读端，p[1]是写端
    int pid = fork(); //创建一个子进程，并返回子进程的进程ID给父进程，0给子进程

    //父进程
    if (pid > 0) { 
        int curPid = getpid();  //获取父进程ID
        write(p[1], "ping", 4); //向管道写端写入字符串"ping"
        wait((int*) 0);       
        read(p[0], buf, BUF);   //从管道读端读取数据到缓冲区
        printf("%d: received %s\n", curPid, buf); //打印父进程的ID和接收到的消息
    }
    //子进程
    else {          
        int curPid = getpid();  //获取子进程ID
        read(p[0], buf, BUF);   //从管道读端读取数据到缓冲区
        printf("%d: received %s\n", curPid, buf); //打印子进程的ID和接收到的消息
        write(p[1], "pong", 4); //向管道写端写入字符串"pong"
    }

    exit(0); //正常退出
}