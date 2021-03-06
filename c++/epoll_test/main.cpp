/* wfh
 * 2018-06-20
 * epoll demo
 * reference: https://www.cnblogs.com/carekee/articles/2760693.html
 */
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 8333
#define IP   "127.0.0.1"
#define MAX_EVENT 20

int InitListenSocket();
int SetSocketNoBlocking(int iFd);
int EpollRun(int iListenFd);
int EpollAddEvent(int iEpollFd, int iFd);
int EpollModEvent(int iEpollFd, int iFd, int iEvent);
int EpollDelEvent(int iEpollFd, int iFd);

int main() {
    int iFd = InitListenSocket();
    if (iFd < 0) {
        printf("init listen socket failed!\n");
        return -1;
    }

    EpollRun(iFd);
    close(iFd);
    return 0;
}

int SetSocketNoBlocking(int iFd) {
    return fcntl(iFd, F_SETFL, fcntl(iFd, F_GETFL) | O_NONBLOCK);
}

int EpollAddEvent(int iEpollFd, int iFd) {
    printf("epoll add event, epoll fd: %d, socket fd: %d\n", iEpollFd, iFd);

    epoll_event tEvent;
    tEvent.data.fd = iFd;
    tEvent.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, iFd, &tEvent) < 0) {
        printf("ctl add epfd failed! error: %d\n", errno);
        return -1;
    }

    SetSocketNoBlocking(iFd);
    return 0;
}

int EpollModEvent(int iEpollFd, int iFd, int iEvent) {
    printf("epoll mod event, epoll fd: %d, socket fd: %d, event: %d\n", iEpollFd, iFd, iEvent);

    epoll_event tEvent;
    tEvent.data.fd = iFd;
    tEvent.events = EPOLLET | iEvent;
    if (epoll_ctl(iEpollFd, EPOLL_CTL_MOD, iFd, &tEvent) < 0) {
        printf("ctl mod epfd failed! error: %d\n", errno);
        return -1;

    }

    SetSocketNoBlocking(iFd);
    return 0;
}

int EpollDelEvent(int iEpollFd, int iFd) {
    printf("epoll delete event! epoll fd = %d, socket fd = %d\n", iEpollFd, iFd);
    return epoll_ctl(iEpollFd, EPOLL_CTL_DEL, iFd, 0);
}

void CloseConn(int iEpollFd, int iFd) {
    printf("close socket, fd = %d\n", iFd);
    EpollDelEvent(iEpollFd, iFd);
    close(iFd);
}

int InitListenSocket() {
    printf("init listen sockt, port: %d\n", PORT);

    struct sockaddr_in tAddr;
    bzero(&tAddr, sizeof(tAddr));
    tAddr.sin_family = AF_INET;
    tAddr.sin_port = htons(PORT);
    tAddr.sin_addr.s_addr = inet_addr(IP);

    int iFd = socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0) {
        printf("socket error: %d\n", errno);
        return -1;
    }

    int iReuse = 1;
    ::setsockopt(iFd, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(iReuse));
    if (bind(iFd, (struct sockaddr*)&tAddr, sizeof(tAddr))) {
        close(iFd);
        printf("bind error. errno: %d\n", errno);
        return -1;
    }

    if (listen(iFd, 64) < 0) {
        close(iFd);
        printf("listen error. err: %d\n", errno);
        return -1;
    }

    return iFd;
}

int EpollRun(int iListenFd) {
    printf("epoll run...\n");

    int iEpollFd = epoll_create(64);
    if (iEpollFd < 0) {
        printf("create epoll fd failed! error: %d\n", errno);
        return -1;
    }

    if (EpollAddEvent(iEpollFd, iListenFd) < 0) {
        printf("add epfd failed! error: %d\n", errno);
        close(iEpollFd);
        return -1;
    }

    epoll_event arrEvents[MAX_EVENT];

    for (;;) {
        int iEvents = epoll_wait(iEpollFd, arrEvents, MAX_EVENT, 500);
        for (int i = 0; i < iEvents; i++) {
            int iEvents = arrEvents[i].events;
            int iFd = arrEvents[i].data.fd;
            if (iFd < 0) {
                continue;
            }

            //accept new connect.
            if (iFd == iListenFd) {
                socklen_t uiSocktLen = 0;
                struct sockaddr_in tClientAddr;
                int iConnFd = accept(iListenFd, (struct sockaddr*)&tClientAddr, &uiSocktLen);
                if (iConnFd < 0) {
                    printf("accept failed! error: %d\n", errno);
                    continue;
                }

                printf("client conn ip: %s\n", inet_ntoa(tClientAddr.sin_addr));

                if (EpollAddEvent(iEpollFd, iConnFd) < 0) {
                    printf("add event failed! client fd: %d, error:%d\n", iConnFd, errno);
                    continue;
                }
            } else if (iEvents & EPOLLIN) {
                char szBuffer[1024] = {0};
                int iRecvLen = recv(iFd, szBuffer, sizeof(szBuffer), 0);
                if (iRecvLen < 0) {
                    printf("recv data len < 0! error: %d, fd: %d\n", errno, iFd);
                    if (errno == EAGAIN || errno == EINTR) {
                        printf("read later, fd: %d!\n", iFd);
                        continue;
                    } else {
                        CloseConn(iEpollFd, iFd);
                    }
                } else if (0 == iRecvLen) {
                    printf("client close connect. fd: %d\n", iFd);
                    CloseConn(iEpollFd, iFd);
                } else {
                    printf("recv data: %s\n", szBuffer);
                    EpollModEvent(iEpollFd, iFd, EPOLLOUT);
                }
            } else if (iEvents & EPOLLOUT) {
                int iFd = arrEvents[i].data.fd;
                char szBuffer[256] = {"123test\n"};
                printf("send client data: %s\n", szBuffer);
                send(iFd, szBuffer, strlen(szBuffer), 0);

                EpollModEvent(iEpollFd, iFd, EPOLLIN);
            }
        }
    }

    close(iEpollFd);
    return 0;
}
