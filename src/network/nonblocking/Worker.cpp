#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>

#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <vector>
#include <map>
#include "Utils.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

const ssize_t BUF_SIZE = 512;

class Connection_info {
public:
    Connection_info() : is_parsed(false), is_building(false), readed("") {
        //		parser = Afina::Protocol::Parser();
    }
    std::string readed;
    Afina::Protocol::Parser parser;
    bool is_parsed, is_building;
    std::vector<std::string> results;
    uint32_t body_size;
    std::unique_ptr<Execute::Command> command;
};

// See Worker.h
Worker::~Worker() {
    // TODO: implementation here
}

Worker::Worker(const Worker &other)
    : _running(other._running.load()), _thread(other._thread), Storage(other.Storage), server_sock(other.server_sock) {}

// See Worker.h
void Worker::Start(int server_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    server_sock = server_socket;
    _running.store(true);
    if (pthread_create(&_thread, NULL, Worker::OnRun, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    _running.store(false);
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(_thread, 0);
}

void* Worker::OnRun(void *arg) {
    Worker *parent = reinterpret_cast<Worker *>(arg);
    parent->Run();
}

// See Worker.h
void Worker::Run() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // TODO: implementation here
    // 1. Create epoll_context here
    // 2. Add server_socket to context
    // 3. Accept new connections, don't forget to call make_socket_nonblocking on
    //    the client socket descriptor
    // 4. Add connections to the local context
    // 5. Process connection events
    //
    // Do not forget to use EPOLLEXCLUSIVE flag when register socket
    // for events to avoid thundering herd type behavior.
    std::map<int, Connection_info> connections;
    std::string result;
    char buf[BUF_SIZE];

    const int events_cnt = 10;
    std::vector<struct epoll_event> events(events_cnt);
    int epoll_fd = epoll_create(events_cnt);

    struct epoll_event event;
    event.data.fd = server_sock;
    event.events = EPOLLIN;
    int s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sock, &event);
    if (s == -1) {
        perror("epoll_ctl");
        abort();
    }

    while (_running.load()){
        size_t n = epoll_wait(epoll_fd, events.data(), events_cnt, 100);

        for (size_t i = 0; i < n; i++){
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || !((events[i].events & EPOLLIN) || (events[i].events & EPOLLOUT))) {
                /* Error occured */
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                s = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                continue;
            } else if (server_sock == events[i].data.fd){
                /* Need to accept new connection */
                struct sockaddr in_addr;
                socklen_t in_len = sizeof(in_addr);
                int infd = accept(server_sock, &in_addr, &in_len);
                if (infd == -1)
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        break;
                    else {
                        perror("accept");
                        break;
                    }

                struct epoll_event event;
                event.data.fd = infd;
                event.events = EPOLLIN | EPOLLOUT | EPOLLET;
                s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event);
                if (s == -1) {
                    perror("epoll_ctl");
                    abort();
                }
                connections.emplace(infd, Connection_info());
            } else {
                int fd = events[i].data.fd;
                Connection_info *conn = &connections[fd];

                if (events[i].events & (EPOLLIN | EPOLLOUT)){
                    /* Read query */
                    /* Sometimes get EPOLLOUT without getting EPOLLIN */
                    do {
                        size_t size_buf = conn->readed.size();
                        memcpy(buf, conn->readed.data(), size_buf);
                        size_t parsed = 0;
                        ssize_t count = 0;

                        if (!conn->is_parsed){
                            try {
                                while (((count = read(fd, buf + size_buf, BUF_SIZE - size_buf)) > 0) && !conn->is_parsed) {
                                    conn->is_parsed = conn->parser.Parse(buf, size_buf + count, parsed);
                                    size_buf += count - parsed;
                                    memcpy(buf, buf + parsed, size_buf);
                                }

                                if (!conn->is_parsed){
                                    parsed = 1;
                                    while (!conn->is_parsed && size_buf > 0 && parsed > 0) {
                                        conn->is_parsed = conn->parser.Parse(buf, size_buf, parsed);
                                        size_buf -= parsed;
                                        memcpy(buf, buf + parsed, size_buf);
                                    }
                                }
                                conn->readed = std::string(buf, size_buf);
                            } catch (const std::runtime_error &error) {
                                result = "ERROR\r\n";
                                conn->results.push_back(result);
                                result.clear();
                                conn->parser.Reset();
                                break;
                            }
                        }

                        if (conn->is_parsed && !conn->is_building) {
                            conn->command = conn->parser.Build(conn->body_size);
                            conn->is_building = true;
                        }

                        if (conn->is_building){
                            ssize_t count_to_read = conn->body_size + 2 - conn->readed.size();
                            if ((conn->body_size != 0) && (count_to_read > 0)) {
                                while ((count_to_read > 0) && ((count = read(fd, buf, std::min(count_to_read, BUF_SIZE))) > 0)) {
                                    conn->readed.append(buf, count);
                                    count_to_read -= count;
                                }
                            }
                            if ((count_to_read <= 0) || (conn->body_size == 0)) {
                                std::string result;
                                (*conn->command).Execute(*(Storage), conn->readed.substr(0, conn->body_size), result);
                                if (conn->body_size != 0)
                                    conn->readed.erase(0, conn->body_size + 2);
                                result += "\r\n";
                                conn->results.push_back(result);
                                conn->parser.Reset();
                                conn->is_building = false;
                                conn->is_parsed = false;
                            }
                        }
                    } while (conn->readed.size() > 0);
                }

                if (events[i].events & (EPOLLOUT)) {
                    /* Answer to query */
                    for (std::string result: conn->results) {
                        size_t pos = 0, count_write;
                        while (pos < result.size()){
                            if ((count_write = write(events[i].data.fd, result.substr(pos).data(), result.size())) < 0) {
                                if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                    throw std::runtime_error("Socket send() failed");
                                }
                            }
                            pos += count_write;
                        }
                    }
                    conn->results.clear();
                }

                if ((conn->readed.size() == 0) && conn->results.empty()){
                    close(fd);
                    connections.erase(fd);
                }
            }
        }
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
