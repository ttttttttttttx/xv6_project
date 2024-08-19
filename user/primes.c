#include "kernel/types.h" 
#include "kernel/stat.h" 
#include "user/user.h"  

int pipeAry[12][2]; //二维数组 存储管道描述符

void prime(int layer) {
    // 如果层数达到11，则退出程序
    if (layer == 11) {
        exit(0);
    }

    int *EOF = (int*)malloc(sizeof(int)); // 分配内存用于存储EOF标记
    *EOF = -1; // 初始化EOF标记为-1
    int *num = (int*)malloc(sizeof(int)); // 分配内存用于存储读取的数字
    int min_num = 0; // 用于存储当前层的最小质数

    read(pipeAry[layer][0], num, sizeof(int)); // 从管道读取数据

    if (*num == *EOF) {
        // 如果读取到EOF标记，则输出"over!"并退出程序
        fprintf(1, "over!\n");
        exit(0);
    } else {
        // 否则，输出当前层的第一个质数，并继续递归
        min_num = *num;
        fprintf(1, "prime %d\n", min_num);

        if (pipe(pipeAry[layer + 1]) == -1) {
            // 如果创建下一层的管道失败，则退出程序
            exit(2);
        }
    }

    int pid = fork(); // 创建子进程
    if (pid < 0) {
        exit(3); // 如果fork失败，则退出程序
    }

    if (pid == 0) {
        close(pipeAry[layer + 1][1]); // 关闭子进程的写端管道
        prime(layer + 1); // 递归调用prime函数
        return; // 子进程返回
    } else {
        close(pipeAry[layer][1]); // 关闭父进程的写端管道
        close(pipeAry[layer + 1][0]); // 关闭父进程的读端管道

        while (read(pipeAry[layer][0], num, sizeof(int)) && *num != *EOF) {
            // 如果读取到EOF标记，则停止循环
            if ((*num) % min_num != 0) {
                // 将非最小质数的数字写入下一层的管道
                write(pipeAry[layer + 1][1], num, sizeof(int));
            }
        }
        write(pipeAry[layer + 1][1], EOF, sizeof(int)); // 写入EOF标记
        close(pipeAry[layer + 1][1]); // 关闭下一层的写端管道
        close(pipeAry[layer][0]); // 关闭当前层的读端管道
        wait(0); // 等待子进程结束
    }

    free(num); // 释放num内存
    free(EOF); // 释放EOF内存
}

int main(int argc, char *argv[]) {
    // 检查参数数量是否正确
    if (argc != 1) {
        exit(1);
    }

    if (pipe(pipeAry[0]) == -1) {
        exit(2); // 如果创建管道失败，则退出程序
    }

    int pid = fork(); // 创建子进程
    if (pid < 0) {
        exit(3); // 如果fork失败，则退出程序
    }

    int *EOF = (int*)malloc(sizeof(int)); // 分配内存用于存储EOF标记
    *EOF = -1; // 初始化EOF标记为-1

    if (pid == 0) {
        close(pipeAry[0][1]); // 关闭子进程的写端管道
        prime(0); // 递归调用prime函数
        close(pipeAry[1][1]); // 关闭下一层的写端管道
    } else {
        close(pipeAry[0][0]); // 关闭父进程的读端管道
        int status = 0; // 用于存储子进程退出状态

        // 向管道写入2-35的数字
        for (int i = 2; i <= 35; ++i) {
            write(pipeAry[0][1], &i, sizeof(i));
        }
        write(pipeAry[0][1], EOF, sizeof(int)); // 写入EOF标记
        close(pipeAry[0][1]); // 关闭父进程的写端管道
        wait(&status); // 等待子进程结束
    }

    free(EOF); // 释放EOF内存
    exit(0); // 父进程正常退出
}