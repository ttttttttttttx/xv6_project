#include "kernel/types.h" 
#include "kernel/stat.h" 
#include "user/user.h" 
#include "kernel/fs.h" 

//match函数 
//用于检查路径path是否包含文件或目录name
int match(char *path, char *name)
{
    char *p;

    //查找路径path中最后一个'/'后的第一个字符
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++; //将p指向最后一个'/'后面的第一个字符

    //如果路径中包含name，则返回1，否则返回0
    if (strcmp(p, name) == 0) 
        return 1;
    else
        return 0;
}

//find函数 
//递归地在目录path中查找与name匹配的文件或目录
void find(char *path, char *name)
{
    char buf[512], *p; //存储路径和目录项
    int fd;            //文件描述符fd用于打开目录
    struct dirent de;  //目录项结构体
    struct stat st;    //文件状态结构体

    if ((fd = open(path, 0)) < 0) { //尝试打开目录path
        fprintf(2, "ls: cannot open %s\n", path); //打开失败 打印错误信息
        return; 
    }

    if (fstat(fd, &st) < 0) { //获取目录path的文件状态
        fprintf(2, "ls: cannot stat %s\n", path); //获取状态失败 打印错误信息
        close(fd); //关闭文件描述符
        return; 
    }

    //根据文件类型进行不同的操作
    switch (st.type) { 
        //文件
        case T_FILE: 
            if (match(path, name))  { //检查路径是否包含name
                printf("%s\n", path); //如果包含，打印路径
            }
            break;  
        
        //目录
        case T_DIR: 
            //检查路径长度加上一个'/'和目录项大小是否超过buf大小
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n"); 
                break; 
            }

            strcpy(buf, path);     //复制路径到buf
            p = buf + strlen(buf); //将p指向buf的末尾
            *p++ = '/';            //在buf末尾添加一个'/'

            while (read(fd, &de, sizeof(de)) == sizeof(de)) { //读取目录项
                //跳过特殊目录项
                if (de.inum == 0) 
                    continue;
                //如果目录项是.或..，则跳过
                if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                    continue;

                memmove(p, de.name, DIRSIZ); //将目录项的名称拷贝到p
                p[DIRSIZ] = 0; //设置p指向的字符串结束
                find(buf, name); //递归调用find函数查找匹配的文件或目录
            }
            break; 
    }

    close(fd); //关闭文件描述符
}

//main函数
int main(int argc, char *argv[]) {

    //检查命令行参数数量
    if (argc < 3) { 
        printf("argc is less then 3\n"); 
        exit(1); //退出程序
    }

    find(argv[1], argv[2]); //调用find函数进行查找
    exit(0); //正常退出
}