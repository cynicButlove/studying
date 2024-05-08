//
// Created by zhangshiping on 24-4-29.
//


#include "pub.h"

const char* get_mime_type( char* name){
    char* dot;
    dot=strrchr(name,'.');  //从后向前找第一个.

    if(dot==(char*)0)
        return "text/plain; charset=utf-8";
    if(strcmp(dot,".html")==0||strcmp(dot,".htm")==0)
        return "text/html;charset=utf-8";
    if(strcmp(dot,".jpg")==0||strcmp(dot,".jpeg")==0)
        return "image/jpeg";
    if(strcmp(dot,".png")==0)
        return "image/png";
    if(strcmp(dot,".txt")==0)
        return  "text/plain";
    if(strcmp(dot,".gif")==0)
        return "image/gif";
    if(strcmp(dot,".mp4")==0)
        return "video/mp4";

}


//%E4%BD%A0%E5%A5%BD---->你好0xE4 ,0xBD ,0xA0
void strTranslate(char* to,char* from)
{
    while(*from!='\0'){
        if(from[0]=='%'&&isxdigit(from[1])&& isxdigit(from[2])){
            *to=aToHex(from[1])*16+ aToHex(from[2]);
            from+=3;
            to++;
        }
        else{
            *to=*from;
            from++;
            to++;
        }
    }
    *to='\0';
}

int aToHex(char c){
    if('0'<=c&&c<='9'){
        return c-'0';
    }
    if('a'<=c&&c<='f'){
        return c-'a'+10;
    }
    if('A'<=c&&c<='F'){
        return c-'A'+10;
    }
}