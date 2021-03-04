#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#ifdef DEBUG
#include <stdexcept>
#include <cstdio>
#endif

#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>

class http_conn
{
public:
    static constexpr int FILENAME_LEN = 200;
    static constexpr int READ_BUFFER_SIZE = 2048;
    static constexpr int WRITE_BUFFER_SIZE = 1024;
    //解析http请求，主状态机状态
    enum CHECK_STATE
    {
        CHECK_REQUESTLINE,
        CHECK_HEADER,
        CHECK_CONTENT
    };
    //处理http请求的结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,//
        NO_RESOURCE,//
        FORBIDDEN_REQUEST,//
        FILE_REQUEST,//
        INTERNAL_ERROR,//
        CLOSED_CONNECTION
    };
    //解析http请求时行的状态，从状态机状态
    enum LINE_STATE
    {
        LINE_OK,
        LINE_BAD,
        LINE_OPEN
    };

    enum METHOD
    {
        GET,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH,
        UNKOWN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr);
    bool read();    // 对外接口，读http请求
    void process(); // 对外接口，读完http请求之后由线程池调用处理http请求，构造http回答
    bool write();   // 对外接口，写http回答
    void close_conn();

private:
    void init();

    HTTP_CODE process_read();      //解析请求
    bool process_write(HTTP_CODE); //构造应答

    LINE_STATE parse_line();
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_header(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();

    bool add_status_line(int status,const char*title);
    bool add_headers(int content_len);
    bool add_content_length(int length);
    bool add_content(const char* content);
    bool add_linger();
    bool add_blank_line();
    bool add_response(const char* format,...);
    
public:
    static int m_epollfd;
    static int m_user_count;

private:
    int m_sockfd;
    sockaddr_in m_addr;

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_line_start;
    CHECK_STATE m_check_state;

    METHOD m_method;
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    int m_sent_idx;
    struct stat m_file_stat;
    int m_file_fd;
    int m_file_sent_sz;
};

#endif