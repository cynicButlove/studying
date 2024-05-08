#include <iostream>
#include <csignal>
#include "ThreadPool.h"
void func(void* arg){
    int* i=(int*)arg;
    std::cout<<"i= "<<*i<<std::endl;
    (*i)++;
    sleep(3);
}
int main() {
    int count=0;
    ThreadPool pool(3,20);
    for(int i=0;i<20;i++){
        Task task(func,&count);
        pool.addTask(task);
    }
    sleep(5);
    return 0;
}
