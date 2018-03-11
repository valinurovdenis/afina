#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <functional>

#include <afina/Storage.h>

#include "../../protocol/Parser.h"
#include "../../executor/Executor.cpp"
#include <afina/execute/Command.h>

namespace Afina {
namespace Network {
namespace Blocking {

void *ServerImpl::RunAcceptorProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        srv->RunAcceptor();
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

void *ServerImpl::RunConnectionProxy(void *p) {
    std::pair<ServerImpl*, int> *srv = reinterpret_cast<std::pair<ServerImpl*, int> *>(p);
    try {
        srv->first->RunConnection(srv->second);
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunAcceptorProxy, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
    shutdown(server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);

    std::unique_lock<std::mutex> lock(connections_mutex);
    while (connections.size() != 0)
        connections_cv.wait(lock);
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makesthe OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }
    Afina::Executor ex("number 1", 10, 20, 60, 1000);
    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);
    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
        }

        std::lock_guard<std::mutex> lock(connections_mutex);
        if (connections.size() == max_workers)
            close(client_socket);
        else {
            std::pair<ServerImpl*, int> temp = std::make_pair(this, client_socket);
            ex.Execute(&Afina::Network::Blocking::ServerImpl::RunConnection, this, client_socket);
            /*
            pthread_t client_pthread;
            if (pthread_create(&client_pthread, NULL, ServerImpl::RunConnectionProxy, &temp) < 0) {
                throw std::runtime_error("Could not create server thread");
            }
            connections.insert(client_pthread);
            pthread_detach(client_pthread);
            */
        }
    }

    // Cleanup on exit...
    close(server_socket);
}

// See Server.h
void ServerImpl::RunConnection(int socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    Afina::Protocol::Parser parser;
    
    std::string in, out;
    const int buf_size = 4096;
    in.resize(buf_size);
    char buf[buf_size];
    ssize_t count;
    size_t parsed, len = 0;
    bool error_catched = false;

    while (running.load()){
        try {
            parsed = 0;

            do {
                if (parsed != 0){
                    len -= parsed;
                    std::memmove(&in[0], &in[0] + parsed, len);
                }

                if ((count = read(socket, buf, buf_size)) >= 0){
                    if (len + count > in.capacity())
                        in.resize(len + count);

                    std::memmove(&in[0] + len, buf, count);
                    len += count;

                    if (len == 0)
                        EndConnection(socket);
                } else
                    throw std::runtime_error("Socket command read() failed");
            } while (!parser.Parse(&in[0], len, parsed));

            if (parsed != 0){
                len -= parsed;
                std::memmove(&in[0], &in[0] + parsed, len);
            }

            uint32_t body_size=0;
            std::unique_ptr<Execute::Command> command = parser.Build(body_size);
            parser.Reset();

            if (body_size != 0){
                body_size += 2;

                if (in.capacity() < body_size)
                    in.resize(body_size);

                while (len < body_size){
                    if ((count = read(socket, buf, std::min((size_t)buf_size, body_size - len))) >= 0){
                        std::memmove(&in[0] + len, buf, count);
                        len += count;
                    } else
                        throw std::runtime_error("Socket arguments read() failed");
                }
            }

            command->Execute(*pStorage, std::string(in.data(), (body_size > 2) ? body_size - 2 : 0), out);

            if (body_size != 0){
                len -= body_size;
                std::memmove(&in[0], &in[0] + body_size, len);
            }
        } catch (std::exception& e){
            out = std::string("SERVER_ERROR ") + e.what();
            error_catched = true;
        }
        
        if (out.size() > 0){
            out += "\r\n";
            if (send(socket, &out[0], out.size(), 0) < 0)
                error_catched = true;
        }
        if (error_catched)
            break;
    }

    EndConnection(socket);
}

void ServerImpl::EndConnection(int socket) {
    close(socket);
    std::unique_lock<std::mutex> lock(connections_mutex);
    connections.erase(pthread_self());
    connections_cv.notify_one();
}
} // namespace Blocking
} // namespace Network
} // namespace Afina
