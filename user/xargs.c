#include "kernel/types.h" 
#include "kernel/stat.h" 
#include "kernel/param.h" 
#include "user/user.h" 

//main函数
int main(int argc, char *argv[]) {

    int argv_len = 0;       //参数个数
    char buf[128] = {'\0'}; //存储用户输入字符串
    char *new_argv[MAXARG]; //存储拆分后的单词

    //检查命令行参数的数量是否超过限制
    if (argc > MAXARG) {
        printf("Too many arguments\n"); //打印错误信息
        exit(1); //退出程序
    }

    //把argv的内容复制到new_argv中
    for (int i = 1; i < argc; i++) {
        new_argv[i - 1] = argv[i]; 
    }

    //循环读取用户的输入
    while (gets(buf, sizeof(buf))) {

        int buf_len = strlen(buf); //获取读取到的字符串的长度
        //如果字符串长度小于1 跳出循环
        if (buf_len < 1)
            break; 

        argv_len = argc - 1; //参数个数
        buf[buf_len - 1] = '\0'; //将读取到的字符串中的换行符替换为字符串结束符

        //把buf中读取到用户输入的内容按照word拆分到每个new_argv中
        for (char *p = buf; *p; p++) {
            //跳过连续的空格字符
            while (*p && (*p == ' ')) 
                *p++ = '\0'; 
            
            //第一个非空字符
            if (*p) {
                //检查参数个数是否超过了限制
                if (argv_len >= MAXARG - 1) {
                    printf("Too many arguments\n"); //打印错误信息
                    exit(1); 
                }
                new_argv[argv_len++] = p; //将新参数添加到new_argv
            }

            //跳过当前参数剩余字符，p指向下一个空格字符或字符串结束符
            while (*p && (*p != ' ')) 
                p++;
        }

        //终止string
        new_argv[argv_len] = "\0"; //添加结束标记

        //父进程
        if (fork() > 0) {
            int status;
            wait(&status); //等待子进程结束
        }
        //子进程
        else {
            //修改exec函数的调用方式，传递可执行文件路径作为第一个参数
            exec(new_argv[0], new_argv); //执行新参数
        }
    }

    exit(0); 
}