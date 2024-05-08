//
// Created by zhangshiping on 24-4-23.
//

#ifndef THREADPOOL_THREADPOOL_H
#define THREADPOOL_THREADPOOL_H

#endif //THREADPOOL_THREADPOOL_H
#include "Task.h"

class ThreadPool
{
public:
    ThreadPool(int min, int max);
    ~ThreadPool();

    // 添加任务
    void addTask(Task task);
    // 获取忙线程的个数
    int getBusyNumber();
    // 获取活着的线程个数
    int getAliveNumber();

private:
    // 工作的线程的任务函数
    static void* worker(void* arg);
    // 管理者线程的任务函数
    static void* manager(void* arg);
    void threadExit();

private:
    pthread_mutex_t m_lock;
    pthread_cond_t m_notEmpty;
    pthread_t* m_threadIDs;//工作线程数组
    pthread_t m_managerID;//管理线程
    TaskQueue* m_taskQ;//任务队列
    int m_minNum;
    int m_maxNum;
    int m_busyNum;//正在工作线程数量
    int m_aliveNum;//当前池中线程数量
    int m_exitNum;//需要退出的线程数量
    bool m_shutdown = false;
};


