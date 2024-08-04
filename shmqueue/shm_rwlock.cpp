//
// Created by zhangshiping on 24-5-2.
//
#include <stdexcept>
#include <cstring>
#include "shm_rwlock.h"

shmmqueue::CShmRWlock::CShmRWlock(key_t iKey) {
    init(iKey);
}
/*
struct sembuf 是一个结构体，用于描述一个对信号量的操作。它包含三个字段：
    semnum：信号量集合中的信号量编号，0代表第1个信号量,1代表第二个信号量等等。
    sem_op：信号量操作。
            一个正数（增加信号量值），负数（减少信号量值），或者是 0（等待信号量变为 0）。
    sem_flg：操作标志。可能的值为 SEM_UNDO，IPC_NOWAIT。
            IPC_NOWAIT 设置信号量操作不等待
            SEM_UNDO  选项会让内核记录一个与调用进程相关的UNDO记录，
                        如果该进程崩溃，则根据这个进程的UNDO记录自动恢复相应信号量的计数值
 */
int shmmqueue::CShmRWlock::Rlock() const {
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *获取读锁
     *等待写信号量（第一个）变为0：{0, 0, SEM_UNDO},并且把读信号量（第二个）加一：{1, 1, SEM_UNDO}
     **/
    //    {0, 0, SEM_UNDO}：等待第一个信号量（写信号量）变为0。这意味着在获取读锁之前，需要等待没有其他的写操作。
    //    {1, 1, SEM_UNDO}：把第二个信号量（读信号量）加一。这表示当前有一个读操作正在进行。

//    struct sembuf sops[2]={{0,0,SEM_UNDO},
//                           {1,1,SEM_UNDO}};
//    size_t nsops=2;

/**
 * 上面实现中 读锁的实现是错误的，这里读锁的实现应该不能是多个读锁可以同时持有，
 * 因为读索引需要互斥使用，否则读的过程中读索引可能会被修改，导致读取错误。
 */

    struct sembuf sops[3]={{0,0,SEM_UNDO},
                           {1,0,SEM_UNDO},
                           {1,1,SEM_UNDO}};
    size_t nsops=3;

    int ret;
    //semop会顺序执行sops中定义的操作
    do{
        ret=semop(m_iSemId,&sops[0],nsops);
    } while ((ret==-1)&&(errno==EINTR));

    return ret;
}

int shmmqueue::CShmRWlock::UnRlock() const {
    struct sembuf sops[1]={{1,-1,SEM_UNDO}};
    size_t nsops=1;
    int ret=-1;
    do{
        ret=semop(m_iSemId,&sops[0],nsops);
    }while((ret==-1)&&(errno==EINTR));

    return ret;
}
/// 尝试获得读锁
/// \return fasle--尝试失败， true--成功
bool shmmqueue::CShmRWlock::TryRlock() const {
    struct sembuf sops[3]={{0,0,SEM_UNDO},
                           {1,0,SEM_UNDO},
                           {1,1,SEM_UNDO}};
    size_t nsops=3;
    int iRet= semop(m_iSemId,&sops[0],nsops);
    if(iRet==-1){
        if(errno==EAGAIN){ //资源暂时不可用
            return false;
        }
        else{
            throw std::runtime_error("semop error: "+std::string (strerror(errno)));
        }
    }
    return true;
}

int shmmqueue::CShmRWlock::Wlock() const {
    //    sops 数组定义了三个操作：
    //    {0, 0, SEM_UNDO}：等待第一个信号量（写信号量）变为0。
    //    {1, 0, SEM_UNDO}：等待第二个信号量（读信号量）变为0。
    //    {0, 1, SEM_UNDO}：把第一个信号量（写信号量）加一。
    struct sembuf sops[3]={{0,0,SEM_UNDO},
                           {1,0,SEM_UNDO},
                           {0,1,SEM_UNDO}};
    size_t nsops=3;
    int ret=-1;
    /*
     这三个操作在 semop 调用中会按顺序执行。
     这是在获取写锁时的操作，意味着在获取写锁之前，需要等待没有其他的读操作和写操作，
     然后增加写信号量的值，表示当前有一个写操作正在进行。
     */
    do{
        ret=semop(m_iSemId,&sops[0],nsops);
    }while(ret==-1&&errno==EINTR);

    return 0;
}

int shmmqueue::CShmRWlock::UnWlock() const {
    struct sembuf sops[1]={{0,-1,SEM_UNDO}};
    size_t nsops=1;
    int ret=-1;
    do{
        ret= semop(m_iSemId,&sops[0],nsops);
    }while(ret==-1&&errno==EINTR);
    return ret;
}

bool shmmqueue::CShmRWlock::TryWlock() const {
    struct sembuf sops[3]={{0,0,SEM_UNDO|IPC_NOWAIT},
                           {1,0,SEM_UNDO|IPC_NOWAIT},
                           {0,1,SEM_UNDO|IPC_NOWAIT}};
    int nsops=3;
    int iRet=-1;
    iRet= semop(m_iSemId,&sops[0],nsops);
    if(iRet==-1){
        if(errno==EAGAIN) //资源暂时不可用
            return false;
        else
            throw std::runtime_error("semop error:"+std::string (strerror(errno)));
    }
    return true;
}


/// \brief CShmRWlock::init
/// \param iKey
/// \details 信号量集合包含两个信号量，第一个为写信号量，第二个为读信号量
/// \details 生成信号量集
/// \details 如果失败，判断信号量是否已经存在
/// \details 连接信号量
/// \details 将所有信号量的值设置为0
void shmmqueue::CShmRWlock::init(key_t iKey) {
//#if 和 #else 是预处理器指令，它们在编译时执行，而不是在运行时。
// 这意味着它们可以用于控制编译器如何处理代码
#if defined(__GNU_LIBRARY__)&& !defined(_SEM_SEMUN_UNDEFINED)
    //union semun 在<sys/sem.h> 中被定义了
#else
    /* according to X/OPEN we have to define it ourselves */
    union semun{
        int val;        /* value for SETVAL */
        struct  semid_ds *buf;      /*buffer for IPC_STAT ,IPC_SET */
        unsigned short  *array;     /*array for GETALL, SETALL */
        /*Linux specific part: */
        struct seminfo *__buf;      /*buf for IPC_INFO */
    };
#endif
    int iSemID;
    union semun arg;
    //包括两个信号量，第一个为写信号量，第二个为读信号量
    u_short array[2]={0,0}  ;
    //生成信号量集
    if((iSemID= semget(iKey,2,IPC_CREAT|IPC_EXCL|0666))!=-1){
        arg.array=&array[0];
        //将所有信号量设置为0
        if(semctl(iSemID,0,SETALL,arg)==-1){
            throw std::runtime_error("semctl error: "+std::string(strerror(errno)));
        }
    }
    else{
        //如果生成信号量集失败，判断是否已经存在
        //不存在
        if(errno!=EEXIST){
            throw std::runtime_error("sem has exist error: "+ std::string (strerror(errno)));
        }
        //存在，连接信号量
        if((iSemID=semget(iKey,2,0666))==-1){
            throw std::runtime_error("semget error: "+ std::string (strerror(errno)));
        }
    }
    m_iSemKey=iKey;
    m_iSemId=iSemID;
}

