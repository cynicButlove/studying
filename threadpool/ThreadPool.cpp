//
// Created by zhangshiping on 24-4-23.
//
#include "ThreadPool.h"
#include <cstring>
#include <iostream>
#include <csignal>

ThreadPool::ThreadPool(int min, int max) {
    m_taskQ=new TaskQueue;
    m_minNum=min;
    m_maxNum=max;
    m_busyNum=0;
    m_aliveNum=m_minNum;
    //根据最大线程数分配内存
    m_threadIDs=new pthread_t[m_maxNum];
    memset(m_threadIDs,0,sizeof(pthread_t)*m_maxNum);
    pthread_mutex_init(&m_lock, nullptr);
    pthread_cond_init(&m_notEmpty, nullptr);
    //根据最小线程数创建线程
    for(int i=0;i<m_minNum;i++){
        pthread_create(&m_threadIDs[i], nullptr,worker,this);
        std::cout<<"created thread id: "<<std::to_string(m_threadIDs[i])<<std::endl;
    }
    pthread_create(&m_managerID, nullptr,manager,this);
}
void *ThreadPool::worker(void *arg) {
    ThreadPool* pool=static_cast<ThreadPool*>(arg);
    while(true)
    {
        pthread_mutex_lock(&pool->m_lock);
        //当前没任务且没关闭
        if (pool->m_taskQ->taskNumber()==0&&!pool->m_shutdown)
        {
            std::cout<<"thread "<<std::to_string(pthread_self())<<" waiting"<<std::endl;
            //闲线程wait
            pthread_cond_wait(&pool->m_notEmpty, &pool->m_lock);
            //需要减少线程数量
            if(pool->m_exitNum>0)
            {
                pool->m_exitNum--;
                if(pool->m_aliveNum>pool->m_minNum){
                    pool->m_aliveNum--;
                    pthread_mutex_unlock(&pool->m_lock);
                    pool->threadExit();
                    std::cout<<"exit because of few tasks, cur alive:"<<pool->m_aliveNum<<std::endl;
                }
            }
        }
        //没任务且关闭
        if(pool->m_taskQ->taskNumber()==0&& pool->m_shutdown)
        {
            pthread_mutex_unlock(&pool->m_lock);
            pool->threadExit();
        }

        //处理任务
        Task task=pool->m_taskQ->takeTask();
        pool->m_busyNum++;
        pthread_mutex_unlock(&pool->m_lock);
        std::cout<<"thread "<<std::to_string(pthread_self())<<" start"<<std::endl;
        task.function(task.arg);
        std::cout<<"thread "<<std::to_string(pthread_self())<<" end"<<std::endl;
        pthread_mutex_lock(&pool->m_lock);
        pool->m_busyNum--;
        pthread_mutex_unlock(&pool->m_lock);
    }
}

void *ThreadPool::manager(void *arg) {
    ThreadPool* pool=static_cast<ThreadPool*>(arg);
    while(!pool->m_shutdown)
    {
        sleep(5);
        pthread_mutex_lock(&pool->m_lock);
        int queueSize=pool->m_taskQ->taskNumber();
        int liveNum=pool->m_aliveNum;
        int busyNum=pool->m_busyNum;
        pthread_mutex_unlock(&pool->m_lock);
        //创建线程
        const int NUMBER=2;
        //任务过多
        if(queueSize>liveNum&&liveNum<pool->m_maxNum)
        {
            pthread_mutex_lock(&pool->m_lock);
            int num=0;
            //添加NUMBER个线程
            for(int i=0;i<pool->m_maxNum&&num<NUMBER&&pool->m_aliveNum<pool->m_maxNum;i++)
            {
                if(pool->m_threadIDs[i]==0)
                {
                    pthread_create(&pool->m_threadIDs[i], nullptr,worker,pool);
                    num++;
                    pool->m_aliveNum++;
                    std::cout<<"created ,current aliveNum: "<<pool->m_aliveNum<<std::endl;
                }
            }
            pthread_mutex_unlock(&pool->m_lock);
        }
        //任务太少
        if(busyNum*2<liveNum&&liveNum>pool->m_minNum){
            pthread_mutex_lock(&pool->m_lock);
            pool->m_exitNum=NUMBER;
            pthread_mutex_unlock(&pool->m_lock);
            for(int i=0;i<NUMBER;i++){
                pthread_cond_signal(&pool->m_notEmpty);
            }
        }
    }
}
ThreadPool::~ThreadPool(){
    m_shutdown=true;
    //等待管理者线程
    pthread_join(m_managerID, nullptr);
    //唤醒空闲等待的线程
    for(int i=0;i<m_aliveNum;i++){
        pthread_cond_signal(&m_notEmpty);
    }
    //等待所有忙线程
    for(int i=0;i<m_maxNum;i++){
        if(m_threadIDs[i]!=0)
        {
            pthread_join(m_threadIDs[i], nullptr);
        }
    }
    if(m_taskQ) delete m_taskQ;
    if(m_threadIDs) delete[]m_threadIDs;
    pthread_mutex_destroy(&m_lock);
    pthread_cond_destroy(&m_notEmpty);
}
void ThreadPool::addTask(Task task) {
    if(m_shutdown) return;
    m_taskQ->addTask(task);
    pthread_cond_signal(&m_notEmpty);

}
int ThreadPool::getAliveNumber() {
    int threadNum=0;
    pthread_mutex_lock(&m_lock);
    threadNum=m_aliveNum;
    pthread_mutex_unlock(&m_lock);
    return threadNum;
}
int ThreadPool::getBusyNumber() {
    int busyNum=0;
    pthread_mutex_lock(&m_lock);
    busyNum=m_busyNum;
    pthread_mutex_unlock(&m_lock);
    return busyNum;
}



void ThreadPool::threadExit() {
    pthread_t tid=pthread_self();
    for(int i=0;i<m_maxNum;i++)
    {
        if(m_threadIDs[i]==tid)
        {
            m_threadIDs[i]=0;
            std::cout<<"thread "<<std::to_string(tid)<<" exit"<<std::endl;
            break;
        }
    }
    pthread_exit(nullptr);
}
