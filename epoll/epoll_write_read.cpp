//
// Created by zhangshiping on 24-4-21.
//
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <sys/epoll.h>
#include <time.h>
#include <iostream>
#include <csignal>

#define SERVER_PORT 9999
#define BufSize 1024
#define MaxEvents 1024
struct myevent_s{
    int fd;
    int events;
    void *arg;
    void (*call_back)(int fd,int events,void *arg);
    int status;                         //1--在监听，0--不在监听
    char buf[BufSize];
    int len;
    long last_active;
};

struct myevent_s g_events[MaxEvents+1];
int efd;

/// 初始化一个myevent_s结构体变量
/// \param ev
/// \param fd
/// \param call_back
/// \param arg
void eventset(struct myevent_s *ev,int fd,void(*call_back)(int,int,void*),void* arg){
    ev->fd=fd;
    ev->events=0;
    ev->arg=arg;
    ev->call_back=call_back;
    ev->status=0;
    memset(ev->buf,0,sizeof (ev->buf));
    ev->len=0;
    ev->last_active=time(NULL); // 获取当前系统时间的时间戳
}
//typedef union epoll_data
//{
//  void *ptr;
//  int fd;
//  uint32_t u32;
//  uint64_t u64;
//} epoll_data_t;
//struct epoll_event
// {
//  uint32_t events;	/* Epoll events */
//  epoll_data_t data;	/* User data variable */
//} __EPOLL_PACKED;
void eventDel(int efd,struct myevent_s *ev){
    struct epoll_event epv={0,{0}} ;
    if(ev->status!=1) return;
    epv.data.ptr=NULL;
    ev->status=0;
    epoll_ctl(efd,EPOLL_CTL_DEL,ev->fd,&epv);
}
void eventAdd(int efd,int events,struct myevent_s *ev){
    struct epoll_event epv={0,{0}};
    epv.data.ptr=ev;
    epv.events=events;
    ev->events=events;
    ev->status=1;
    epoll_ctl(efd,EPOLL_CTL_ADD,ev->fd,&epv);
}
void receiveData(int fd,int events,void* arg);

void sendData(int fd,int events,void* arg){
    struct myevent_s* ev=(struct myevent_s*)arg;
    strcpy(ev->buf,"got!");
    int len=send(fd,ev->buf,BufSize,0);

    if(len>0){
        std::cout<<"send: "<<ev->buf<<std::endl;
        eventDel(efd,ev);
        eventset(ev,fd,receiveData,ev);
        eventAdd(efd,EPOLLIN,ev);
    }else{
        close(ev->fd);
        std::cout<<"send error"<<std::endl;
    }
}
void receiveData(int fd,int events,void *arg){
    struct myevent_s *ev=(struct myevent_s*)arg;
    int len= recv(fd,ev->buf,BUFSIZ,0); //flags=0,就是read
    if(len>0){
        ev->len=len;
        ev->buf[len]='\0';
        std::cout<<"receive: "<<ev->buf<<std::endl;
        eventDel(efd,ev);
        eventset(ev,fd,sendData,ev);
        eventAdd(efd,EPOLLOUT,ev);
    }else if(len==0){
        close(ev->fd);
        std::cout<<"receive close"<<std::endl;
    }else{
        close(ev->fd);
        std::cout<<"receive error"<<std::endl;
    }
}
void acceptConn(int lfd,int events,void* arg){
    struct sockaddr_in client_addr;
    socklen_t client_addr_len=sizeof(client_addr);
    int cfd= accept(lfd,(struct sockaddr*)&client_addr,&client_addr_len);
    std::cout<<"accept cfd: "<<cfd<<std::endl;

    int i;
    for(i=0;i<MaxEvents;i++){  //数组中找个空闲
        if(g_events[i].status==0){
            g_events[i].status=1;
            break;
        }
    }
    if(i==MaxEvents) {
        std::cout<<"max connect limit"<<std::endl;
        return;
    }
    int flag=fcntl(cfd,F_SETFL,O_NONBLOCK);
    if(flag<0){
        std::cout<<"nonblock error"<<std::endl;
        return;
    }
    eventset(&g_events[i], cfd, receiveData, &g_events[i]);
    eventAdd(efd,EPOLLIN,&g_events[i]);

    std::cout<<"connect:"<<inet_ntoa(client_addr.sin_addr)<<" "
        <<ntohs(client_addr.sin_port)<<std::endl;
}


int main(int argc,char* argv[]){
    //初始化server
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_addr.s_addr=INADDR_ANY;
    server_addr.sin_port=htons(SERVER_PORT);
    server_addr.sin_family=AF_INET;
    //socket
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    std::cout<<"lfd："<<lfd<<std::endl;
    //socket设置为非阻塞
    fcntl(lfd,F_SETFL,O_NONBLOCK);
    //bind
    bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    //listen
    listen(lfd,10);
    //epoll_creat1(0)
    efd= epoll_create1(0);
    std::cout<<"efd: "<<efd<<std::endl;
    //lfd加入efd中
    eventset(&g_events[MaxEvents],lfd,acceptConn,&g_events[MaxEvents]);
    eventAdd(efd,EPOLLIN,&g_events[MaxEvents]);

    struct epoll_event events[MaxEvents+1];
    int pos=0;
    while(true){
        //超时检测
        long now=time(NULL);
        for(int i=0;i<100;i++){
            if(pos==MaxEvents){
                pos=0;
            }
            if(g_events[pos].status==0){
                continue;
            }
            long duration=now-g_events[pos].last_active;
            if(duration>10){
                close(g_events[pos].fd);
                std::cout<<"time out fd: "<<g_events[pos].fd<<std::endl;
                eventDel(efd,&g_events[pos]);
            }
            pos++;
        }
        /*监听红黑树g_efd, 将满足的事件的文件描述符加至events数组中, 5秒没有事件满足, 返回 0,如果有立即返回*/
        int nfd= epoll_wait(efd,events,MaxEvents+1,5000);
        std::cout<<"nfd :"<<nfd<<std::endl;
        for(int i=0;i<nfd;i++){
            struct myevent_s* ev=(struct myevent_s*)events[i].data.ptr;

            //读和连接都是EPOLLIN
            if((events[i].events&EPOLLIN)&& (ev->events & EPOLLIN)){
                std::cout<<"in fd :"<<ev->fd<<std::endl;
                ev->call_back(ev->fd,events[i].events,ev->arg);
            }
            //写是EPOLLOUT
            if((events[i].events&EPOLLOUT)&& (ev->events & EPOLLOUT)){
                ev->call_back(ev->fd,events[i].events,ev->arg);
            }
        }

    }

}

