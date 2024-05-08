//
// Created by zhangshiping on 24-4-23.
//

#ifndef THREADPOOL_TASK_H
#define THREADPOOL_TASK_H

#endif //THREADPOOL_TASK_H

#include <mutex>
#include <queue>

using callback=void(*)(void*);
struct Task{
    callback function;
    void* arg;
    Task(){
        function= nullptr;
        arg= nullptr;
    }
    Task(callback f,void* arg){
        function=f;
        this->arg=arg;
    }
};
class TaskQueue{
private:
    pthread_mutex_t m_mutex;
    std::queue<Task> m_queue;
public:
    TaskQueue();
    ~TaskQueue();
    //添加任务
    void addTask(Task& task);
    void addTask(callback func,void* arg);
    //取出一个任务
    Task takeTask();
    //获取当前队列任务个数
    inline int taskNumber(){
        return m_queue.size();
    }
};
