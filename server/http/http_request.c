/*
 * Copyright (C) Reage
 * blog:http://www.ireage.com
 */
#include "http_request.h"



static int find_header_end(buffer *b);


response * response_init(pool_t *p)
{
	response *out;
    
 	out = palloc(p, sizeof(response));
 	out->status_code = HTTP_UNDEFINED;
	out->content_length = 0;
	out->server  = NULL;
	out->date = NULL;
	out->www_authenticate = NULL;
	out->content_type = NULL;
	out->physical_path = NULL;
	out->content_encoding = _NOCOMPRESS;

	return out;
}

request * request_init(pool_t *p)
{
	request *in;

	in = pcalloc(p, sizeof(request));


	return in;
}
 


static int start_web_server(http_conf *g)
{
	int count ;
	int epfd;
	int fd = 0;
	epoll_extra_data_t *data;

	data = (epoll_extra_data_t *) malloc(sizeof(epoll_extra_data_t));

	data->type = SERVERFD;
	if(g->port <=0) g->port = 80;
	epfd = epoll_init(MAX_CONNECT);
	//while(count--){
	fd = socket_listen("127.0.0.1", g->port);
	//	web->fd = fd;
	epoll_add_fd(epfd, fd,EPOLL_R, data);
	//	web = web->next;
	//}
	
	g->fd = fd;
	g->epfd = epfd;

	return epfd;
}



static int read_http_header(buffer *header, pool_t *p, int fd)
{
	buffer *b;
	int palloc_size = 1024;
	char *c ;
	char *ptr;

	c = palloc(p,sizeof(char));
	header->ptr = palloc(p, palloc_size);
	header->size = palloc_size;


	while(read(fd, c, 1)) {
		buffer_append_char(header,*c,p);
		if(*c == '\n' && header->used >= 2) {
			ptr =  header->ptr + header->used - 2;
			if(strncasecmp(ptr, "\n\n", 2) == 0) {
				return;
			}
		}
	}

	return 0;
}

/**
 *0 读取结束，其他没有结束
 *
 */
static int read_header(http_connect_t *con)
{
	pool_t *p;
	buffer *header;


	p =(pool_t *) con->p;

	if(con->in->header == NULL)con->in->header = buffer_init(p);
	header = con->in->header;
	
	return read_http_header(header, p, con->fd);
}


static char * skip_space(char *start, char *end){
	if(start == NULL) return NULL;
	while( *start == ' ' ) {
		if(start >= end) return NULL;
		start ++;
	}

	return start;
}

static char * find_line(char *start, char *end) {
	while(*start != '\n') {
		if(start >= end) return end;
		start ++;
	}

	return start;
}


static void parse_header(http_connect_t * con)
{
 	request *in;
	buffer *header;
	buffer *b;
	read_buffer *dst;
	pool_t *p;
	char * start, *end;


	p = (pool_t *)con->p;
	dst = (read_buffer *)palloc(p, sizeof(read_buffer));
	in = con->in;
	header = in->header;
	start = (char *)(header->ptr);
	end = (char *)header->ptr + (header->used - 1);
	if(strncasecmp(start,"put", 3) == 0) {
		in->http_method = _PUT;
		start += 3;
	}if(strncasecmp(start,"get", 3) == 0) {
		in->http_method = _GET;
		start += 3;
	}
	else if(strncasecmp(start, "post", 4) == 0) {
		in->http_method = _POST;
		start += 4;
	}

	start = skip_space(start, end);
	in->uri = (read_buffer *)palloc(p, sizeof(read_buffer));
	in->uri->ptr = start;
	start = find_line(start, end);
	in->uri->size = start - in->uri->ptr;
	start++;


	in->content_length = atoi(start);
	//test_print_header(in);
	return ;
}


int accept_handler(http_connect_t *con)
{

 	//con->in = (request *)request_init(con->p);
 	//con->out = (response *)response_init(con->p);


	read_header(con);
	parse_header(con);
	ds_log(con, "  [ACCEPT] ", LOG_LEVEL_DEFAULT);
	if(con->in->http_method == _PUT) {
		//open_write_file(con);
		con->next_handle = open_write_file;
	} else {
		con->next_handle = NULL;
	}

	return 0;
}


int start_accept(http_conf *g)
{
	int count;
	int confd  ;
	struct epoll_event ev[MAX_EVENT];
	struct epoll_event *evfd ;
	epoll_extra_data_t *epoll_data;
	struct sockaddr addr;
	struct sockaddr_in addrIn;
	socklen_t addLen = sizeof(struct sockaddr );


	start_web_server(g);

	printf("--------------- start server\n--------------");
	while(1){
		count = epoll_wait(g->epfd, ev, MAX_EVENT, -1);
		evfd = ev;
		while( (confd =  accept(g->fd, &addr, &addLen)) && confd > 0) {
			pool_t *p = (pool_t *)pool_create();
			http_connect_t * con;
			epoll_extra_data_t *data_ptr;

			addrIn =  *((struct sockaddr_in *) &addr);
			data_ptr = (epoll_extra_data_t *)palloc(p, sizeof(epoll_extra_data_t));
			con = (http_connect_t *) palloc(p, sizeof(http_connect_t));//换成初始化函数，
			con->p= p;
			con->fd = confd;
			con->in = (request *)request_init(p);
			con->out = (response *)response_init(p);
			char *ip  = NULL;
			if(addrIn.sin_addr.s_addr) {
				ip = inet_ntoa(addrIn.sin_addr);
			}

			if(ip) {
				con->in->clientIp = (read_buffer *) read_buffer_init_str(p, ip, strlen(ip));
			}

			con->next_handle = accept_handler;
			data_ptr->type = SOCKFD;
			data_ptr->ptr = (void *) con;
			epoll_add_fd(g->epfd, confd, EPOLL_R, (void *)data_ptr);//对epoll data结构指向的结构体重新封装，分websit
			//data struct ,  connect  data struct , file data struct ,
		}
		while((evfd->events & EPOLLIN)){
			if((evfd->events & EPOLLIN)) {
				http_connect_t * con;
				epoll_data = (epoll_data_t *)evfd->data.ptr;

				con = (http_connect_t *) epoll_data->ptr;
				switch(epoll_data->type) {
					case SOCKFD:
						if(con->in == NULL) {
							//accept_handler(g, con, evfd);
							epoll_edit_fd(g->epfd, evfd, EPOLL_W);
                            //epoll_del_fd(g->epfd, evfd);
						}
						while(con->next_handle != NULL) {
							int ret = con->next_handle(con);
							if(ret == -1) {
								con->next_handle = NULL;
								epoll_del_fd(g->epfd, evfd);
								close(con->fd);
								ds_log(con, "  [END] ", LOG_LEVEL_DEFAULT);
								pool_destroy(con->p);
							}else if(ret == 1) {
								break;
							}
                            /*if(con->next_handle(con) == -1) {
                            	epoll_del_fd(g->epfd, evfd);
                                close(con->fd);
                                pool_destroy(con->p);
                            }*/
						}
	 					break;
					case CGIFD: {
	 					epoll_cgi_t *cgi = (epoll_cgi_t *)epoll_data->ptr;
		 				break;
	 	 			}
	 	 		}


			}
			else if(evfd->events & EPOLLOUT) {

	 	 	}

	 	 	evfd++;
	 	}
	}
}
