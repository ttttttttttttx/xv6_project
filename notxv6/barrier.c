#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;
pthread_mutex_t lock; //锁

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;   // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

// barrier_init()函数
// 初始化线程屏障 barrier
static void barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0); //互斥锁：保护屏障状态 原子性
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);   //条件变量：实现线程间同步
  bstate.nthread = 0; //初始化屏障状态变量 表示没有线程到达屏障点
}

// barrier()函数
// 线程屏障（barrier）的核心实现
static void barrier()
{
  // YOUR CODE HERE
  pthread_mutex_lock(&bstate.barrier_mutex); //获取互斥锁
  
  //不是所有线程都到达屏障点
  if (++bstate.nthread < nthread) {
    //等待条件变量 释放锁
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  //所有线程都到达屏障点
  else {
    bstate.nthread = 0; //重置计数器
    ++bstate.round; //增加轮次
    pthread_cond_broadcast(&bstate.barrier_cond); //唤醒所有等待线程
  }
  
  //释放互斥锁
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void * thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}