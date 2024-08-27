//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

// find_symlink()函数
// 处理递归链接 返回目标文件
static struct inode* 
find_symlink(struct inode* ip) 
{
  int i, j; 
  uint inums[NSYMLINK]; //存储遇到的inode编号
  char target[MAXPATH]; //存储目标路径

  //最多跟随 NSYMLINK(10) 个符号链接
  for(i = 0; i < NSYMLINK; i++) { 
    inums[i] = ip->inum; //将当前inode编号存储在数组中

    //从符号链接文件中读取目标路径
    if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0) { //读取inode中的数据到target
      iunlockput(ip); 
      printf("open_symlink: open symlink failed\n"); 
      return 0; 
    }
    iunlockput(ip); //解锁并释放inode

    //获取目标路径的inode
    if((ip = namei(target)) == 0) { //根据路径获取inode
      printf("open_symlink: path \"%s\" is not exist\n", target); 
      return 0; 
    }

    //检查是否形成了循环链接
    for(j = 0; j <= i; j++) 
      if(ip->inum == inums[j]) { //当前inode编号与之前遇到的编号相同
        printf("open_symlink: links form a cycle\n"); 
        return 0; 
      }
    
    ilock(ip); //加锁新获取的inode
    if(ip->type != T_SYMLINK) //新获取的inode不是符号链接
      return ip; //返回inode
  }

  //超过了跟随的最大深度
  iunlockput(ip); //解锁并释放inode
  printf("open_symlink: the depth of links reaches the limit\n"); 
  return 0; //返回0 表示失败
}

// sys_open()函数
// 用于打开文件
uint64
sys_open(void)
{
  char path[MAXPATH]; //文件路径
  int fd, omode; 
  struct file *f; 
  struct inode *ip; 
  int n; 

  //获取文件路径和打开模式参数
  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0) 
    return -1; 

  begin_op(); //开始文件系统操作

  // O_CREATE 表示需要创建文件
  if(omode & O_CREATE){ 
    ip = create(path, T_FILE, 0, 0); //创建文件inode
    if(ip == 0){ 
      end_op(); 
      return -1; 
    }
  } 
  else { //不需要创建文件
    if((ip = namei(path)) == 0){ //获取文件的inode
      end_op(); 
      return -1; 
    }
    ilock(ip); //加锁inode
    if(ip->type == T_DIR && omode != O_RDONLY){ //inode是目录并且打开模式不是只读
      iunlockput(ip); 
      end_op(); 
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){ //inode是设备 设备号无效
    iunlockput(ip); 
    end_op(); 
    return -1; 
  }

  //inode是符号链接 没有O_NOFOLLOW标志
  if(ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0) 
    //follow_symlink 寻找符号链接的目标文件
    if((ip = find_symlink(ip)) == 0) { 
      // 此处不用调用iunlockput()释放锁
      // follow_symlinktest()返回失败时,锁在函数内已经被释放
      end_op(); //结束操作
      return -1; 
    }

  //尝试分配文件结构体和文件描述符
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f) 
      fileclose(f); 
    iunlockput(ip); 
    end_op(); 
    return -1; 
  }

  if(ip->type == T_DEVICE) { 
    f->type = FD_DEVICE; 
    f->major = ip->major; 
  } 
  else { 
    f->type = FD_INODE; 
    f->off = 0; 
  }
  f->ip = ip; 
  f->readable = !(omode & O_WRONLY); 
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){ 
    itrunc(ip); 
  }

  iunlock(ip); //解锁inode
  end_op();    //结束文件系统操作

  return fd; //返回文件描述符
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// sys_symlink()函数
// 系统调用函数 用于创建符号链接
uint64 
sys_symlink(void) 
{
  char target[MAXPATH], path[MAXPATH];
  //两个字符数组 存储目标路径和符号链接的路径

  struct inode *ip; //inode指针
  int n; //字符串长度

  //调用argstr函数获取命令行参数 存储到target和path数组中
  if ((n = argstr(0, target, MAXPATH)) < 0 || argstr(1, path, MAXPATH) < 0) 
    return -1; //参数获取失败
  
  begin_op(); //开始一个文件系统操作

  //尝试创建符号链接的inode 类型为T_SYMLINK
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0) { //创建失败
    end_op(); //结束操作
    return -1;
  }

  //将目标路径写入到inode中
  if(writei(ip, 0, (uint64)target, 0, n) != n) { //写入失败
    iunlockput(ip); //解锁inode并释放
    end_op(); //结束操作
    return -1;
  }

  iunlockput(ip); //解锁inode 并将其放入空闲队列
  end_op(); //结束文件系统操作

  return 0; //成功
}