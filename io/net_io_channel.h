#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include <cryptoTools/Network/Channel.h>
//#include <cryptoTools/Network/Endpoint.h>
//#include <cryptoTools/Network/IOService.h>

#ifdef _MSC_VER 
#define USE_ASIO
#endif

#ifdef USE_ASIO 
#include <boost/asio.hpp>
extern  boost::asio::io_service emp_io_service;
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <sys/types.h>

#include <string>
#include "io_channel.h"
using namespace std;

#ifndef NETWORK_IO_CHANNEL
#define NETWORK_IO_CHANNEL
/** @addtogroup IO
    @{
  */




class NetIO : public IOChannel<NetIO> {
public:
    bool is_server;

#ifdef USE_ASIO 
    boost::asio::ip::tcp::socket mSock;
#else
    int mysocket = -1;
    int consocket = -1;
    FILE * stream = nullptr;
    char * buffer = nullptr;
#endif 

    bool has_sent = false;
    string addr;
    int port;
#ifdef COUNT_IO
    uint64_t counter = 0;
#endif

    NetIO(const char * address, int port, bool quiet = false)
#ifdef USE_ASIO 
        :mSock(emp_io_service)
#endif
    {

#ifdef USE_ASIO
        using namespace boost::asio::ip;
        this->port = port;
        is_server = (address == nullptr);
        if (is_server) {
            // wildcard address
            //addr = "0.0.0.0";

            tcp::acceptor acceptor(emp_io_service, tcp::endpoint(tcp::v4(), port));
            acceptor.accept(mSock);

        }
        else
        {
            addr = address;
            tcp::resolver resolver(emp_io_service);
            tcp::resolver::query query(addr, std::to_string(port));
            tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

            boost::system::error_code ec;
            boost::asio::connect(mSock, endpoint_iterator, ec);
            while (ec) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                boost::asio::connect(mSock, endpoint_iterator, ec);
            }
        }


#else
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
            bind(mysocket, (struct sockaddr *)&serv, sizeof(struct sockaddr));
            listen(mysocket, 1);
            consocket = accept(mysocket, (struct sockaddr *)&dest, &socksize);
        }
        else {
            addr = string(address);
            struct sockaddr_in dest;
            consocket = socket(AF_INET, SOCK_STREAM, 0);
            memset(&dest, 0, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_addr.s_addr = inet_addr(address);
            dest.sin_port = htons(port);
            while (connect(consocket, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == -1) {
                usleep(1000);
            }
        }

        stream = fdopen(consocket, "wb+");
        buffer = new char[NETWORK_BUFFER_SIZE];
        memset(buffer, 0, NETWORK_BUFFER_SIZE);
        setvbuf(stream, buffer, _IOFBF, NETWORK_BUFFER_SIZE);

#endif

        if (!quiet)
            cout << "connected" << endl;
    }

    ~NetIO() {
#ifndef USE_ASIO
        fflush(stream);
        close(consocket);
        delete[] buffer;
#endif
    }
    void sync() {
        int tmp = 0;
        if (is_server) {
            send_data(&tmp, 1);
            recv_data(&tmp, 1);
        }
        else {
            recv_data(&tmp, 1);
            send_data(&tmp, 1);
            flush();
        }
    }
    void set_nodelay() {
#ifdef USE_ASIO
        boost::asio::ip::tcp::no_delay option(true);
        mSock.set_option(option);
#else
        const int one = 1;
        setsockopt(consocket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    }

    void set_delay() {
#ifdef USE_ASIO
        boost::asio::ip::tcp::no_delay option(false);
        mSock.set_option(option);
#else
        const int zero = 0;
        setsockopt(consocket, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
#endif
    }

    void flush() {
#ifndef USE_ASIO
        fflush(stream);
#endif
    }

    void send_data_impl(const void * data, int len) {
#ifdef COUNT_IO
        counter += len;
#endif

#ifdef USE_ASIO
        boost::system::error_code ec;
        boost::asio::write(mSock, boost::asio::buffer(data, len), ec);

        if (ec)
        {
            std::cout << "network error" << std::endl << ec.message() << std::endl;
            std::terminate();
        }
#else
        int sent = 0;
        while (sent < len) {
            int res = fwrite(sent + (char*)data, 1, len - sent, stream);
            if (res >= 0)
                sent += res;
            else
                fprintf(stderr, "error: net_send_data %d\n", res);
        }
#endif
        has_sent = true;
    }

    void recv_data_impl(void  * data, int len) {
#ifdef USE_ASIO
        boost::system::error_code ec;
        boost::asio::read(mSock, boost::asio::buffer(data, len), ec);

        if (ec)
        {
            std::cout << "network error" << std::endl << ec.message() << std::endl;
            std::terminate();
        }
#else
        if (has_sent)
            fflush(stream);
        has_sent = false;
        int sent = 0;
        while (sent < len) {
            int res = fread(sent + (char*)data, 1, len - sent, stream);
            if (res >= 0)
                sent += res;
            else
                fprintf(stderr, "error: net_send_data %d\n", res);
        }
#endif
    }
};





/**@}*/
#endif//NETWORK_IO_CHANNEL
