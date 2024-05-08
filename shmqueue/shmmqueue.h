//
// Created by zhangshiping on 24-5-1.
//
#ifndef messagequeue_h
#define messagequeue_h

#include "shm_rwlock.h"

#define EXTRA_BYTE 8
#define MESS_SIZE_TYPE size_t

/*
 * __attribute__((aligned(CACHELINE_SIZE)))是 GCC 的一个特性，
 * 用于指定变量或类型的最小对齐要求。
 * 通过这个宏，我们可以确保某个变量始终位于同一个缓存行中，从而提高程序的性能。
 * 这种技术常用于多线程编程中，以避免 "false sharing" 现象。
 * "False sharing"是多线程编程中的一个问题，它发生在多个线程
 * 并发访问同一缓存行（cache line）中的不同数据时。
 * 虽然这些线程访问的是不同的数据，但由于它们位于同一缓存行中，
 * 所以任何一个线程对其数据的修改都会导致整个缓存行的无效，
 * 从而迫使其他线程重新从主内存中加载这个缓存行。这会导致额外的内存访问，降低程序的性能。
 */
#define CACHELINE_SIZE 64
//修改字对齐规则，避免false sharing
#define CACHELINE_ALIGN  __attribute__((aligned(CACHELINE_SIZE)))

#define SHM_MIN(a,b) a < b ? a : b

#define  CACHE_LINE_SIZE 64 //cache line 大小
//内存屏障
#define __MEM_BARRIER \
    __asm__ __volatile__("mfence":::"memory")
//内存读屏障
#define __READ_BARRIER__ \
    __asm__ __volatile__("lfence":::"memory")
//内存写屏障
#define __WRITE_BARRIER__ \
    __asm__ __volatile__("sfence":::"memory")

namespace shmmqueue
{
typedef unsigned char BYTE;

enum class eQueueModel: unsigned char
{
    ONE_READ_ONE_WRITE,   //一个进程读消息一个进程写消息
    ONE_READ_MUL_WRITE,   //一个进程读消息多个进程写消息
    MUL_READ_ONE_WRITE,   //多个进程读消息一个进程写消息
    MUL_READ_MUL_WRITE,   //多个进程读消息多个进程写消息
};

enum class eQueueErrorCode
{
    QUEUE_OK = 0,     // param error
    QUEUE_PARAM_ERROR = -1,     // param error
    QUEUE_NO_SPACE = -2,        // message queue has no space
    QUEUE_NO_MESSAGE = -3,      // message queue has no message
    QUEUE_DATA_SEQUENCE_ERROR = -4,// message queue the message sequence error
};

enum class enShmModule: unsigned char
{
    SHM_INIT,     //第一次申请共享内存，初始化
    SHM_RESUME,   //共享内存已存在，恢复重新映射共享内存数据
};

class CACHELINE_ALIGN CMessageQueue
{
private:
    ///
    /// \param pCurrAddr
    /// \param module
    /// \param sheKey
    /// \param shmId
    /// \param size 如果传入的size！=2^n,那么size初始化为 大于size的最小2^n;
    CMessageQueue(BYTE *pCurrAddr,eQueueModel module, key_t shmKey, int shmId,size_t size);
public:
    ~CMessageQueue();
    CMessageQueue(const CMessageQueue &) = delete;
    CMessageQueue(CMessageQueue &&) = delete;
    CMessageQueue &operator=(const CMessageQueue &) = delete;
public:

    int SendMessage(BYTE *message, MESS_SIZE_TYPE length);

    int GetMessage(BYTE *outDataAddr);

    /**
     * 打印队列信息
     * 这里没有加锁打印仅供参考，不一定是正确的
     **/
    void PrintTrunk();
private:
    //获取空闲区大小
    unsigned int GetFreeSize();
    //获取数据长度
    unsigned int GetDataSize();
    //初始化lock
    void InitLock();
    //是否要对读端上锁
    bool IsReadLock();
    //是否要对写端上锁
    bool IsWriteLock();
public:
    //创建共享内存
    static BYTE *CreateShareMem(key_t iKey, long vSize, enShmModule &shmModule,int& shmId);
    //销毁共享内存
    static int DestroyShareMem(const void *shmaddr,key_t iKey);
    //是否是2的次方
    static bool IsPowerOfTwo(size_t size);
    //向上取2^n
    static size_t RoundupPowofTwo(size_t size);
    //创建CMssageQueue对象
    static CMessageQueue *CreateInstance(key_t shmkey,size_t queuesize,eQueueModel queueModule );
private:
    //
    struct CACHELINE_ALIGN stMemTrunk
    {
        /**
         * 0) 单线程读单线程写模型　https://blog.csdn.net/D_Guco/article/details/100066985
         * 1) 这里读写索引用int类型,cpu可以保证对float,double和long除外的基本类型的读写是原子的,保证一个线程不会读到另外一个线程写到一半的值
         * 2) 在变量之间插入一个64字节(cache line的长度)的变量(没有实际的计算意义),但是可以保证两个变量不会同时在一个cache line里,防止不同的
         *    进程或者线程同时访问在一个cache line里不同的变量产生false sharing,
         */
        volatile unsigned int m_iRead;
        char __cache_padding1__[CACHE_LINE_SIZE];
        volatile unsigned int m_iWrite;
        char __cache_padding2__[CACHE_LINE_SIZE];
        int m_iShmKey;
        char __cache_padding3__[CACHE_LINE_SIZE];
        unsigned int m_iSize;
        char __cache_padding4__[CACHE_LINE_SIZE];
        int m_iShmId;
        char __cache_padding5__[CACHE_LINE_SIZE];
        eQueueModel m_eQueueModule;
    };
private:
    stMemTrunk *m_stMemTrunk;
    CShmRWlock *m_pReadLock;  //m_iRead 锁
    CShmRWlock *m_pWriteLock; //m_iWrite
    BYTE *m_pQueueAddr;
    void * m_pShm;
};
}


#endif /* messagequeue_h */
