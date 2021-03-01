#define DEBUG

#include<unistd.h>
#include<iostream>
#include<cstdio>
#include"threadpool.h"

struct Task
{
    Task(int n=rand()):n(n)
    {

    }
    void process()
    {
        printf("process %u %d\n",(unsigned)pthread_self(),n);
        sleep(1);
    }
    int n;
};
int main(){

    threadpool<Task> pool;
    int n=100;
    Task* t=new Task[n];

    for(int i=0;i<n;++i)t[i].n=i,pool.push(t+i);
    sleep(100);
    return 0;
}
