

#include "threadpool.h"
#include "http_conn.h"
#include <assert.h>
#include <arpa/inet.h>

#define MAX_FD 1000
#define MAX_EVENT_NUMBER 10000
extern void addfd(int epollfd, int fd, bool one_shot);

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}

void run_http_server(int port)
{
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>();
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(1);
    }
    http_conn *users = new http_conn[MAX_FD];
    assert(users);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    //inet_pton(AF_INET, INADDR_ANY, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event *events = new epoll_event[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true)
    {
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((num<0)&&(errno!=EINTR))
        {
            fprintf(stderr,"epoll failure\n");
            break;
        }

        for(int i=0;i<num;++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                while(true)
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if(connfd<0)
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK)
                        {
                            break;
                        }
                        else
                        {
                            fprintf(stderr,"accept err:%s\n",strerror(errno));
                            continue;
                        }
                    }
                    if(http_conn::m_user_count>=MAX_FD-3)
                    {
                        show_error( connfd, "Internal server busy" );
                        continue;
                    }
                    users[connfd].init( connfd, client_address );
                }
                
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )
            {
                if( users[sockfd].read() )
                {
                    pool->push( users + sockfd );
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )
            {
                if( !users[sockfd].write() )
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close( epollfd );
    close( listenfd );
    delete[] users;
    delete pool;
    delete[] events;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);
    run_http_server(port);
    return 0;
}