#include "kernel/types.h" 
#include "kernel/stat.h" 
#include "user/user.h"  

int pipeAry[12][2]; 
//二维数组 存储至多12个管道的文件描述符
//每个管道有两个文件描述符：read write

//prime函数 layer表示递归层数
void prime(int layer) {
    
    if (layer == 11) { //如果层数达到11
        exit(0); //退出程序
    }

    int *EOF = (int*)malloc(sizeof(int)); //分配内存用于存储EOF标记
    *EOF = -1; //初始化EOF为-1
    int *num = (int*)malloc(sizeof(int)); //分配内存用于存储读取的数字
    int min_num = 0; //用于存储当前层的最小质数

    read(pipeAry[layer][0], num, sizeof(int)); //从当前层的管道读取数据

    //读取到EOF标记 输出"over!"并退出程序
    if (*num == *EOF) {
        fprintf(1, "over!\n");
        exit(0);
    } 
    //输出当前层的第一个(最小)质数
    else {
        min_num = *num;
        fprintf(1, "prime %d\n", min_num);

        if (pipe(pipeAry[layer + 1]) == -1) {
            //如果创建下一层的管道失败，则退出程序
            exit(2);
        }
    }

    //创建子进程
    int pid = fork(); 
    if (pid < 0) {
        exit(3); //如果fork失败 则退出程序
    }

    //子进程
    if (pid == 0) {
        close(pipeAry[layer + 1][1]); //关闭子进程的写端管道
        prime(layer + 1); //递归调用prime函数
        return; 
    } 
    //父进程
    else {
        close(pipeAry[layer][1]);     //关闭父进程当前层的写端管道
        close(pipeAry[layer + 1][0]); //关闭父进程下一层的读端管道

        //父进程从当前层管道中读取所有数字 将不是当前质数倍数的数字写入下一层管道
        while (read(pipeAry[layer][0], num, sizeof(int)) && *num != *EOF) {
            if ((*num) % min_num != 0) {
                write(pipeAry[layer + 1][1], num, sizeof(int));
            }
        }

        write(pipeAry[layer + 1][1], EOF, sizeof(int)); //写入EOF标记
        close(pipeAry[layer + 1][1]); //关闭下一层的写端管道
        close(pipeAry[layer][0]); //关闭当前层的读端管道
        wait(0); //等待子进程结束
    }

    free(num); //释放num内存
    free(EOF); //释放EOF内存
}

//main函数
int main(int argc, char *argv[]) {
    
    if (argc != 1) { //检查参数数量是否正确
        exit(1);
    }

    if (pipe(pipeAry[0]) == -1) { //创建第一个管道
        exit(2);
    }

    int pid = fork(); //创建子进程
    if (pid < 0) { //fork失败 则退出程序
        exit(3); 
    }

    int *EOF = (int*)malloc(sizeof(int)); //分配内存用于存储EOF标记
    *EOF = -1; //初始化EOF为-1

    //子进程
    if (pid == 0) {
        close(pipeAry[0][1]); //关闭子进程的写端管道
        prime(0); //递归调用prime函数
        close(pipeAry[1][1]); //关闭下一层的写端管道
    } 
    //父进程
    else {
        close(pipeAry[0][0]); //关闭父进程的读端管道
        int status = 0; //用于存储子进程退出状态

        //向管道写入2-35的数字
        for (int i = 2; i <= 35; i++) {
            write(pipeAry[0][1], &i, sizeof(i));
        }
        write(pipeAry[0][1], EOF, sizeof(int)); //写入EOF标记
        close(pipeAry[0][1]); //关闭父进程的写端管道
        wait(&status); //等待子进程结束
    }

    free(EOF); //释放EOF内存
    exit(0); 
}