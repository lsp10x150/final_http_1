#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <csignal>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int daemoize(const char* pname, int facility) {
    pid_t pid;
    if ( (pid = fork()) < 0 ) return -1;
    else if (pid) _exit(0);
    if (setsid() < -1) return -1;
    signal(SIGHUP, SIG_IGN);
    if ( (pid = fork()) < 0 ) return -1;
    else if (pid) _exit(1);
//    chdir("/");
    for (int i = 0; i < 64; i++) close(i);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
    int fd = open("myPID", O_CREAT | O_EXCL, 0644);
    if (fd == -1) {
        fd = open("myPID", O_RDWR | O_TRUNC);
        if (fd == -1) {
            perror("open");
            return 1;
        }
    }
    std::string pids = std::to_string(getpid());
    int res = write(fd, pids.c_str(), pids.size());
    if (res == -1) {
        perror("write");
        return 1;
    }
    res = close(fd);
    if (res == -1) {
        perror("close");
        return 1;
    }
    openlog(pname, LOG_PID, facility);
    return 0;
}

std::string ip, port, dir;
bool run = true;

void catch_sigint(int signo) {
    printf("sigint: end the program\n");
    run = false;
}

std::vector<std::string> tokenize(const char *str) {
    std::vector<std::string> result;
    do
    {
        const char *begin = str;
        while(*str != ' ' && *str)
            str++;
        result.push_back(std::string(begin, str));
    } while (0 != *str++);
    return result;
}

void* serve(void* arg) {
    std::string OK_200 = "HTTP/1.0 200 OK\r\n";
    char times[256] = {0};
    time_t t = time(0);
    OK_200 += "Date: ";
    std::string date = ctime_r(&t, times);
    if (*date.rbegin() == '\n')
        date = date.substr(0, date.size() - 1);
    OK_200 += date; OK_200 += "\r\n";
    OK_200 += "server: my_serv\r\n";
    OK_200 += "MIME-version: 1.0\r\n";
    OK_200 += "Content-Type: text/html;\r\n";


    int fd = *(int*)arg;
    printf("serve fd: %d\n", fd);
    char buf[1024] = {0};
    int res = read(fd, buf, 1024);
    if (res == -1) {
        perror("serve::read");
        exit(1);
    }
    std::string st = buf;
    auto v = tokenize(st.c_str());
    if (v.size() >= 2 && v.at(0) == "GET") {
        std::string file_requested = v.at(1);
        struct stat s;
        if (*file_requested.rbegin() == '\n')
            file_requested = file_requested.substr(0, file_requested.size() - 1);
        if (*file_requested.rbegin() == '\r')
            file_requested = file_requested.substr(0, file_requested.size() - 1);
        if (file_requested == "/") {
            file_requested = "/index.html";
        }
        std::string file_to_send = dir + file_requested;
        if ( stat(file_to_send.c_str(), &s) == 0 ) {
            OK_200 += "Conent-Length: ";
            OK_200 += std::to_string(s.st_size)  + "\r\n\r\n";
            res = write(fd, OK_200.c_str(), OK_200.size());
            if (res != -1) {
                int rf = open(file_to_send.c_str(), O_RDONLY);
                auto tot_size = s.st_size;
                while (tot_size) {
                    auto how_much = std::min(1024, int(tot_size));
                    read(rf, buf, how_much);
                    tot_size -= how_much;
                    write(fd, buf, how_much);
                }
//                write(fd, "\r\n", 2);
            }
        } else {
            perror("stat");
            std::string buf = "HTTP/1.0 404\r\n";
            res = write(fd, buf.c_str(), buf.size());
            if (res == -1) {
                perror("serve::write");
                exit(1);
            }
        }
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc != 7) {
        fprintf(stderr, "Usage: %s -h ip -p port -d dir\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    daemoize(argv[0], 0);
    syslog(0, "Hello!");
    signal(SIGINT, catch_sigint);
    int opt = 0;
    while ( (opt = getopt(argc, argv, "h:p:d:")) != -1 ) {
        switch (opt) {
            case 'h':
                ip = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'd':
                dir = optarg;
                if (dir == ".") {
                    char buf[1024] = {0};
                    dir = getcwd(buf, 1024);
                }
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s -h ip -p port -d dir\n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    struct sockaddr_in server_addr {};
    int res = inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    if (res == -1) {
        perror("inet_pton");
        return 1;
    }
    server_addr.sin_port = htons(std::stoi(port));
    server_addr.sin_family = AF_INET;
    int sock_fd = socket(AF_INET, SOCK_STREAM, NULL);
    int i = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    res = bind(sock_fd, (const sockaddr*) &server_addr, sizeof server_addr);
    if (res == -1) {
        perror("bind");
        return 1;
    }
    res = listen(sock_fd, 100);
    if (res == -1) {
        perror("listen");
        return 1;
    }
    while (run) {
        struct sockaddr_in accepted_addr;
        socklen_t accepted_size = sizeof accepted_addr;
        int accepted_fd = accept(sock_fd, (sockaddr*) &accepted_addr, &accepted_size);
        if (accepted_fd == -1) {
            perror("accept");
            return 1;
        }
        pthread_t thread;
        if (pthread_create(&thread, NULL, serve, (void*) &accepted_fd) != 0) {
            perror("pthread_create");
            return 1;
        }
        if ( pthread_detach(thread) != 0 ) {
            perror("pthread_detach");
            return 1;
        }
    }
}
