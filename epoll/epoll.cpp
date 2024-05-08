//
// Created by zhangshiping on 24-4-20.
//

#define SERVER_PORT 9999

#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

int main(){
    //server初始化
    struct sockaddr_in server_addr;

    server_addr.sin_family=AF_INET;
    server_addr.sin_port= htons(SERVER_PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    //socket
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){perror("socker erroe");exit(1);}
    //bind
    bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    //listen上限
    listen(lfd,128);
    //创建红黑树
    int efd=epoll_create1(0);
    struct epoll_event tep;
    //红黑树上添加lfd
    tep.events=EPOLLIN;
    tep.data.fd=lfd;
    int ret= epoll_ctl(efd,EPOLL_CTL_ADD,lfd,&tep);
    if(ret<0){
        perror("epoll_ctl error");
        exit(1);
    }
    struct epoll_event ep[128];
    while(1){
        std::cout<<"before"<<std::endl;
        int wait_ret= epoll_wait(efd,ep,128,-1);//events是传出参数
        std::cout<<"after"<<std::endl;
        for(int i=0;i<wait_ret;i++){
            int sockfd=ep[i].data.fd;
            //sockfd=lfd,是客户端连接
            if(sockfd==lfd){
                struct sockaddr_in client_addr;
                socklen_t client_addr_len=sizeof(client_addr);
                int cfd= accept(lfd,(struct sockaddr*)&client_addr,&client_addr_len);
                //cfd设置为非阻塞
                int flag=fcntl(cfd,F_GETFL);
                flag|=O_NONBLOCK;
                fcntl(cfd,F_SETFL,flag);
                /*
                 当一个文件描述符（如cfd）被设置为非阻塞模式后，这个描述符在所有的操作中都将表现为非阻塞的
                 无论是调用 read、write 还是 epoll_wait，都会表现为非阻塞的行为。
                 如果在非阻塞模式下，epoll_wait 没有检测到就绪的事件，它会立即返回0，而不会阻塞等待
                 但是此代码中依然阻塞，只打印before，不知道为什么
                 */
                //新的cfd添加到efd红黑树中
                tep.events=EPOLLIN|EPOLLET;
                tep.data.fd=cfd;
                ret= epoll_ctl(efd,EPOLL_CTL_ADD,cfd,&tep);
                if(ret<0){}
            }
            //读事件发生
            else{
                char buf[1024];
                int len=read(sockfd,buf,sizeof(buf));
                //客户端关闭
                if(len==0){
                    epoll_ctl(efd,EPOLL_CTL_DEL,sockfd, nullptr);
                    close(sockfd);
                }
                //服务端出错
                else if(len==-1){
                    perror("read error");
                    exit(1);
                }
                //读数据
                else{
                    std::cout<<std::endl;
                    std::cout << "Received message from client: " << std::string(buf, len) << std::endl;
                }

                std::cout<<"read end"<<std::endl;

            }

        }
    }
}