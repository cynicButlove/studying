#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

#include "pub.h"
#define PORT 9999

void send_header(int cfd,int code,char *info,const char* filetype,int length)
{
    //发送状态行
    char buf[1024];
    int len=sprintf(buf,"HTTP/1.1 %d %s\r\n",code,info);
    send(cfd,buf,len,0);
    //发送消息头
    len= sprintf(buf,"Content-Type:%s\r\n",filetype);
    send(cfd,buf,len,0);
    //发送长度
    if(length>0){
        len= sprintf(buf,"Contnet-Length:%d\r\n",length);
        send(cfd,buf,len,0);
    }
    //空行
    send(cfd,"\r\n",2,0);
}

void send_file(int cfd, const char* path, int efd, struct epoll_event* event, int close_cfd_flag) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    char buf[1024];
    int bytesRead;
    int totalSent = 0;

    while (true) {
        bytesRead = read(fd, buf, sizeof(buf));
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue; // 考虑信号中断，重新读取数据
            } else {
                perror("read");
                break;
            }
        } else if (bytesRead == 0) {
            break; // 文件读取结束
        } else {
            int offset = 0;
            while (offset < bytesRead) {
                int sent = send(cfd, buf + offset, bytesRead - offset, 0);
//                std::cout<<sent<<std::endl;
                if (sent < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 缓冲区暂满，等待下一次发送机会
                        continue;
                    } else {
                        perror("send");
                        break;
                    }
                } else if (sent == 0) {
                    // 对方关闭连接
                    break;
                }
                offset += sent;
                totalSent += sent;
            }
        }
    }

    close(fd);

    if (close_cfd_flag == 1) {
        // 关闭cfd，下树
        close(cfd);
        epoll_ctl(efd, EPOLL_CTL_DEL, cfd, event);
    }
}

int ReadLine(int fd, char* buf, int len) {
    int bytesRead = 0;
    char ch;
    ssize_t readBytes;

    while (bytesRead < len - 1) { // 避免缓冲区溢出，留一个位置给 '\0'
        readBytes = read(fd, &ch, 1);
        if (readBytes < 0) {
            return -1; // 读取错误
        } else if (readBytes == 0) {
            break; // 文件结束
        } else {
            buf[bytesRead++] = ch;
            if (ch == '\n') {
                // 检查是否是 CRLF 结尾，如果是则结束读取
                if (bytesRead >= 2 && buf[bytesRead - 2] == '\r') { //important
                    break;
                }
            }
            else {
                //打印请求的内容，但是run的时候控制台看不到，debug才能看到
                std::cout<<ch;
            }
        }
    }
    buf[bytesRead] = '\0'; // 确保字符串以 '\0' 结尾
    return bytesRead;
}

void read_client_request(int efd, struct epoll_event *event){
     char buf[1024];
     char temp[1024];
     //只读第一行
     int  n=ReadLine(event->data.fd,buf,sizeof(buf));
     if(n==0)
     {
         std::cout<<"close "<<std::endl;
         epoll_ctl(efd,EPOLL_CTL_DEL,event->data.fd,event);
         close(event->data.fd);
         return ;
     }
     else if(n<0){
         perror("read error");
         return;
     }
     else{
         //std::cout<<buf<<std::endl;  //buf有数据但是输出看不到
         //读完剩下的行,因为是水平触发
         int t=0;
         while((t= ReadLine(event->data.fd,temp,sizeof(temp)))>0){}
         //解析请求 GET /a.txt HTTP/1.1\R\N
         char method[256];
         char content[256];
         char protocol[256];
         sscanf(buf, "%s %s %s", method, content, protocol);
         std::cout<<"method: "<<method<<" content: "<<content<<" protocol: "<<protocol<<std::endl;
         //判断是否是get请求，get才处理 get GET
         if(strcasecmp(method,"get")==0)
         {
            char* strfile=content+1;
            //%E4%BD%A0%E5%A5%BD---->你好0xE4 ,0xBD ,0xA0
             strTranslate(strfile,strfile);
            //GET / HTTP/1.1\R\N
            // 得到请求的文件，如果没有请求文件，默认./index.html
            if(*strfile=='\0') strfile="./index.html";
            //判断文件是否存在，如果存在（普通文件or目录）
            struct stat s;
            //文件不存在
            if(stat(strfile,&s)<0){  //stat()将file的信息写到buf中
                std::cout<<"file not found"<<std::endl;
                //发送报头
                send_header(event->data.fd,404,"Not Found", get_mime_type("*.html"),0);
                //发送error.html
                send_file(event->data.fd,"error.html",efd,event,1);
            }
            else{
                //普通文件
                if(S_ISREG(s.st_mode))
                {
                    std::cout<<"file"<<std::endl;
                    //发送报头
                    send_header(event->data.fd,200,"OK", get_mime_type(strfile),s.st_size);
                    //发送文件
                    send_file(event->data.fd,strfile,efd,event,1);
                }

                //目录
                else if(S_ISDIR(s.st_mode))
                {
                    std::cout<<"dir"<<std::endl;
                    //发送报头
                    send_header(event->data.fd,200,"OK", get_mime_type("*.html"),0);
                    //发送header.html
                    send_file(event->data.fd,"dir_header.html",efd,event,0);

                    struct dirent **mylist=nullptr;
                    char bufDir[1024];
                    int n=scandir(strfile,&mylist,nullptr,alphasort); //读目录写strfile到mylist中，selector是过滤器，cmp是排序
                    /*
                     * int scandir (const char *__restrict __dir,
                                struct dirent ***__restrict __namelist,
                                int (*__selector) (const struct dirent *),
                                int (*__cmp) (const struct dirent **,
                                      const struct dirent **))
                     */
                    int len;
                    for(int i=0;i<n;i++){
                        //还是目录,在超链接路径上加/
                        if(mylist[i]->d_type==DT_DIR){
                            len= sprintf(bufDir, "<li><a href=%s/ >%s</a></li>", mylist[i]->d_name, mylist[i]->d_name);
                        }
                        else{
                            len= sprintf(bufDir, "<li><a href=%s >%s</a></li>", mylist[i]->d_name, mylist[i]->d_name);
                        }
                        send(event->data.fd, bufDir, len, 0);

                    }
                    //发送tail.html
                    send_file(event->data.fd,"dir_tail.html",efd,event,1);
                }
            }

         }

     }

}

int main() {
    //
    signal(SIGPIPE,SIG_IGN);
    //当前路径
    char pwd_path[256];
    char* path=getenv("PWD");
    strcpy(pwd_path,path);
    strcat(pwd_path,"/web-http");
    chdir(pwd_path);
    //server初始化
    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_port= htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    //server_addr.sin_addr.s_addr= inet_addr("192.168.163.128");
    //socket
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){perror("socker erroe");exit(1);}
    fcntl(lfd,F_SETFL,O_NONBLOCK);
    //bind
    bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    //listen上限
    listen(lfd,128);
    // 设置地址重用选项，允许地址快速重用
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
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
    struct epoll_event ev, evs[128];
    while(true){
        int nready= epoll_wait(efd,evs,128,-1);
        if(nready<0)
        {
            perror("epoll_wait");
            break;
        }
        else{
            for(int i=0;i<nready;i++){
                //判断是不是lfd
                if(evs[i].data.fd==lfd&&evs[i].events&EPOLLIN){
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len=sizeof(client_addr);
                    int cfd= accept(lfd,(struct sockaddr*)&client_addr,&client_addr_len);
                    std::cout<<"new client : "<<inet_ntoa(client_addr.sin_addr)<<
                        " "<<ntohs(client_addr.sin_port)<<std::endl;
                    //非阻塞
                    fcntl(cfd,F_SETFL,O_NONBLOCK);
                    //上树
                    ev.data.fd =cfd;
                    ev.events=EPOLLIN;
                    epoll_ctl(efd,EPOLL_CTL_ADD,cfd,&ev);
                }
                else{
                  read_client_request(efd,&evs[i]);

                }
            }
        }
    }
}


