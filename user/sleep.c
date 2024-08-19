#include "kernel/types.h" 
#include "kernel/stat.h" 
#include "user/user.h"   

int main (int argc, char *argv[]) { //argc为参数个数 argv为参数数组
    if (argc <= 1) { //参数个数<=1 没有提供要暂停的秒数
        fprintf(2, "usage: sleep seconds\n"); //输出错误信息
        exit(1); //以错误状态码1退出
    }
    sleep(atoi(argv[1])); //调用sleep函数 使程序暂停指定秒数
    exit(0); //正常退出
}