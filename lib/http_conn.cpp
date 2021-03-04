#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/home/zpeng/www";

//设置非阻塞
int set_nonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_option | O_NONBLOCK);
    return old_option;
}
//向内核epoll注册fd，同时设置fd非阻塞
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}
//从内核epoll移除fd,同时关闭fd
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
//更改已注册fd的事件，可重置oneshot使之再次可触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
void closefd(int fd)
{
    if(fd>=0)
    {
        close(fd);
    }
}
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_addr = addr;
    ++m_user_count;
    addfd(m_epollfd, sockfd, true);
    init();
}
void http_conn::init()
{
    modfd(m_epollfd,m_sockfd,EPOLLIN);
    m_read_idx = 0;
    m_checked_idx = 0;
    m_line_start = 0;

    m_check_state = CHECK_REQUESTLINE;

    m_method = UNKOWN;
    m_url = NULL;
    m_version = NULL;
    m_host = NULL;
    m_content_length = 0;
    m_linger = false;

    m_write_idx=0;
    m_sent_idx=0;
    m_file_fd=-1;
    m_file_sent_sz=0;
}

/* 
    读取非阻塞套接字，读完缓冲区时结束。
    返回true说明读取数据成功
    返回false说明读取出错或者对方关闭套接字
*/
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

#ifdef DEBUG
            throw std::runtime_error("http_conn::read() error: bytes_read=recv(m_sockfd,m_read_buf,READ_BUFFER_SIZE-m_read_idx,0)");
#endif

            return false;
        }
        else if (bytes_read == 0)
        {
            return false; //对方关闭连接
        }

        m_read_idx += bytes_read;
    }
#ifdef DEBUG
    printf("read successful\n");
#endif
    return true;
}

http_conn::LINE_STATE http_conn::parse_line()
{
    for (m_checked_idx; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        char tmp = m_read_buf[m_checked_idx];
        if (tmp == '\r')
        {
            if (m_checked_idx + 1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (m_checked_idx - 1 >= 0 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    if (m_url == NULL)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    if (strcasecmp(text, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");

    m_version = strpbrk(m_url, " \t");
    if (m_version == NULL)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';

    if (m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_header(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }

    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request()
{
    char file_path[FILENAME_LEN];
    strcpy(file_path,doc_root);
    int len=strlen(file_path);

    if(strcmp(m_url,"/")==0)
    {
        strncpy( file_path +len, "/index.html", FILENAME_LEN - len - 1 );
    }
    else
    {
        strncpy( file_path +len, m_url, FILENAME_LEN - len - 1 );
    }
    
    #ifdef DEBUG
        printf("url_path:%s\n",file_path);
    #endif
    if( stat( file_path, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDDEN_REQUEST;
    }
    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;
    }
    int fd = open( file_path, O_RDONLY );
    if(fd<0)
    {
        return INTERNAL_ERROR;
    }
    m_file_fd=fd;
    return FILE_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret;

    while (((m_check_state == CHECK_CONTENT) && (line_state == LINE_OK)) ||
           ((line_state = parse_line()) == LINE_OK))
    {
        char *text = m_read_buf + m_line_start;
        m_line_start = m_checked_idx;

#ifdef DEBUG
        printf("got 1 http line: %s\n", text);
#endif

        switch (m_check_state)
        {
        case CHECK_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        case CHECK_HEADER:
            ret = parse_header(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;

        case CHECK_CONTENT:
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_state = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }

    if (line_state == LINE_BAD)
    {
        return BAD_REQUEST;
    }
    return NO_REQUEST;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("HTTP/1.1 %d %s\r\n", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) &&
           add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int length)
{
    return add_response("Content-Length: %d\r\n", length);
}
bool http_conn::add_content(const char *content)
{
    return add_response(content);
}
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("\r\n");
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx > WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - m_write_idx, format, arg_list);
    va_end(arg_list);
    if (len < WRITE_BUFFER_SIZE - m_write_idx)
    {
        m_write_idx += len;
        return true;
    }
    else
    {
        return false;
    }
}

bool http_conn::process_write(HTTP_CODE code)
{
    bool ret;
    switch (code)
    {
    case INTERNAL_ERROR:
        ret=add_status_line(500,error_500_title)&&
            add_headers(strlen(error_500_form))&&
            add_content(error_500_form);
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        break;

    case BAD_REQUEST:
        ret=add_status_line(400,error_400_title)&&
            add_headers(strlen(error_400_form))&&
            add_content(error_400_form);
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        break;
    case NO_RESOURCE:
        ret=add_status_line(404,error_404_title)&&
            add_headers(strlen(error_404_form))&&
            add_content(error_404_form);
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        ret=add_status_line(403,error_404_title)&&
            add_headers(strlen(error_403_form))&&
            add_content(error_403_form);
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        break;
    case FILE_REQUEST:
        ret=add_status_line( 200, ok_200_title );
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        if(m_file_stat.st_size!=0)
        {
            ret=add_headers( m_file_stat.st_size );
        }
        else
        {
            const char* ok_string = "<html><body></body></html>";
            ret=add_headers( strlen( ok_string ) )&&add_content(ok_string);
        }
        if(!ret)
        {
            m_write_idx=0;
            return false;
        }
        break;
    default:
        m_write_idx=0;
        return false;
    }
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    //如果还没有解析出request继续读取完整请求
    if (read_ret == NO_REQUEST)
    {
        //重置使得oneshot可重新触发
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);

    /*触发写事件*/
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
//返回是否保持连接
bool http_conn::write()
{
    while(m_sent_idx<m_write_idx)
    {
        int ret=send(m_sockfd,m_write_buf+m_sent_idx,m_write_idx-m_sent_idx,0);
        if(ret==-1)
        {
            if(errno==EAGAIN||errno==EWOULDBLOCK)
            {
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            else
            {
                closefd(m_file_fd);
                return false;
            }
        }
        else
        {
            m_sent_idx+=ret;
        }
    }
    #ifdef DEBUG
    printf("write header successful\n");
    #endif
    if(m_file_fd>=0)
    {
        while(m_file_sent_sz<m_file_stat.st_size)
        {
            int ret=sendfile(m_sockfd,m_file_fd,NULL,
                    m_file_stat.st_size-m_file_sent_sz);
            if(ret==-1)
            {
                if(errno==EAGAIN||errno==EWOULDBLOCK)
                {
                    modfd(m_epollfd,m_sockfd,EPOLLOUT);
                    return true;
                }
                else
                {
                    closefd(m_file_fd);
                    return false;
                }
            }
            else
            {
                m_file_sent_sz+=ret;
            }
        }
        closefd(m_file_fd);
    }
    #ifdef DEBUG
    printf("write file successful\n");
    #endif
    if(m_linger)
    {
        init();
        return true;
    }
    else
    {
        return false;
    }
}

void http_conn::close_conn()
{
    if (m_sockfd >= 0)
    {
        removefd(m_epollfd, m_sockfd);
        --m_user_count;
        m_sockfd = -1;
    }
    closefd(m_file_fd);
}
