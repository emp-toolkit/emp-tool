#ifndef NETWORK_IO_CHANNEL
#define NETWORK_IO_CHANNEL

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "emp-tool/io/io_channel.h"
using std::string;

#ifdef UNIX_PLATFORM

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace emp {
/** @addtogroup IO
  @{
 */

class NetIO: public IOChannel<NetIO> { public:
	bool is_server;
	int mysocket = -1;
	int consocket = -1;
	FILE * stream = nullptr;
	char * buffer = nullptr;
	bool has_sent = false;
	string addr;
	int port;
	uint64_t counter = 0;
	NetIO(const char * address, int port, bool quiet = false) {
		this->port = port;
		is_server = (address == nullptr);
		if (address == nullptr) {
			struct sockaddr_in dest;
			struct sockaddr_in serv;
			socklen_t socksize = sizeof(struct sockaddr_in);
			memset(&serv, 0, sizeof(serv));
			serv.sin_family = AF_INET;
			serv.sin_addr.s_addr = htonl(INADDR_ANY); /* set our address to any interface */
			serv.sin_port = htons(port);           /* set the server port number */    
			mysocket = socket(AF_INET, SOCK_STREAM, 0);
			int reuse = 1;
			setsockopt(mysocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
			if(bind(mysocket, (struct sockaddr *)&serv, sizeof(struct sockaddr)) < 0) {
				perror("error: bind");
				exit(1);
			}
			if(listen(mysocket, 1) < 0) {
				perror("error: listen");
				exit(1);
			}
			consocket = accept(mysocket, (struct sockaddr *)&dest, &socksize);
			close(mysocket);
		}
		else {
			addr = string(address);
			
			struct sockaddr_in dest;
			memset(&dest, 0, sizeof(dest));
			dest.sin_family = AF_INET;
			dest.sin_addr.s_addr = inet_addr(address);
			dest.sin_port = htons(port);

			while(1) {
				consocket = socket(AF_INET, SOCK_STREAM, 0);

				if (connect(consocket, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == 0) {
					break;
				}
				
				close(consocket);
				usleep(1000);
			}
		}
		set_nodelay();
		stream = fdopen(consocket, "wb+");
		buffer = new char[NETWORK_BUFFER_SIZE];
		memset(buffer, 0, NETWORK_BUFFER_SIZE);
		setvbuf(stream, buffer, _IOFBF, NETWORK_BUFFER_SIZE);
		if(!quiet)
			std::cout << "connected\n";
	}

	void sync() {
		int tmp = 0;
		if(is_server) {
			send_data(&tmp, 1);
			recv_data(&tmp, 1);
		} else {
			recv_data(&tmp, 1);
			send_data(&tmp, 1);
			flush();
		}
	}

	~NetIO(){
		flush();
		fclose(stream);
		delete[] buffer;
	}

	void set_nodelay() {
		const int one=1;
		setsockopt(consocket,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
	}

	void set_delay() {
		const int zero = 0;
		setsockopt(consocket,IPPROTO_TCP,TCP_NODELAY,&zero,sizeof(zero));
	}

	void flush() {
		fflush(stream);
	}

	void send_data(const void * data, int len) {
		counter += len;
		int sent = 0;
		while(sent < len) {
			int res = fwrite(sent + (char*)data, 1, len - sent, stream);
			if (res >= 0)
				sent+=res;
			else
				fprintf(stderr,"error: net_send_data %d\n", res);
		}
		has_sent = true;
	}

	void recv_data(void  * data, int len) {
		if(has_sent)
			fflush(stream);
		has_sent = false;
		int sent = 0;
		while(sent < len) {
			int res = fread(sent + (char*)data, 1, len - sent, stream);
			if (res >= 0)
				sent += res;
			else 
				fprintf(stderr,"error: net_send_data %d\n", res);
		}
	}
};
/**@}*/

}

#else  // not UNIX_PLATFORM

#include <boost/asio.hpp>
using boost::asio::ip::tcp;

namespace emp {

/** @addtogroup IO
  @{
 */
class NetIO: public IOChannel<NetIO> { 
public:
	bool is_server;
	string addr;
	int port;
	uint64_t counter = 0;
	char * buffer = nullptr;
	int buffer_ptr = 0;
	int buffer_cap = NETWORK_BUFFER_SIZE;
	bool has_send = false;
	boost::asio::io_service io_service;
	tcp::socket s = tcp::socket(io_service);
	NetIO(const char * address, int port, bool quiet = false) {
		this->port = port;
		is_server = (address == nullptr);
		if (address == nullptr) {
			tcp::acceptor a(io_service, tcp::endpoint(tcp::v4(), port));
			s = tcp::socket(io_service);
			a.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
			a.accept(s);
		} else {
			tcp::resolver resolver(io_service);
			tcp::resolver::query query(tcp::v4(), address, std::to_string(port).c_str());
			tcp::resolver::iterator iterator = resolver.resolve(query);

			s = tcp::socket(io_service);
			s.connect(*iterator);
		}
		s.set_option( boost::asio::socket_base::send_buffer_size( 65536 ) );
		buffer = new char[buffer_cap];
		set_nodelay();
		if(!quiet)
			std::cout << "connected\n";
	}
	void sync() {
		int tmp = 0;
		if(is_server) {
			send_data(&tmp, 1);
			recv_data(&tmp, 1);
		} else {
			recv_data(&tmp, 1);
			send_data(&tmp, 1);
			flush();
		}
	}

	~NetIO() {
		flush();
		delete[] buffer;
	}

	void set_nodelay() {
		s.set_option(boost::asio::ip::tcp::no_delay(true));
	}

	void set_delay() {
		s.set_option(boost::asio::ip::tcp::no_delay(false));
	}

	void flush() {
		boost::asio::write(s, boost::asio::buffer(buffer, buffer_ptr));
		buffer_ptr = 0;
	}

	void send_data(const void * data, int len) {
		counter += len;
		if (len >= buffer_cap) {
			if(has_send) {
				flush();
			}
			has_send = false;
			boost::asio::write(s, boost::asio::buffer(data, len));
			return;
		}
		if (buffer_ptr + len > buffer_cap)
			flush();
		memcpy(buffer + buffer_ptr, data, len);
		buffer_ptr += len;
		has_send = true;
	}

	void recv_data(void  * data, int len) {
		int sent = 0;
		if(has_send) {
			flush();
		}
		has_send = false;
		while(sent < len) {
			int res = s.read_some(boost::asio::buffer(sent + (char *)data, len - sent));
			if (res >= 0)
				sent += res;
			else 
				fprintf(stderr,"error: net_send_data %d\n", res);
		}
	}
};

}

#endif  //UNIX_PLATFORM
#endif  //NETWORK_IO_CHANNEL
