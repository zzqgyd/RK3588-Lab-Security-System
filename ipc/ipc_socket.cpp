#include "ipc/ipc_socket.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/**
 * 普通数据通过 msg_iov 传，fd 通过 msg_control 传。
 * 两者必须在同一个 sendmsg 中发送，接收方在同一个 recvmsg 中同时收到数据+fd。
 */

// 服务器端：创建监听套接字
// backlog = 等待 accept 的客户端队列的最大长度。
int ipc_sock_server_create(const char* path, int backlog)
{
    unlink(path);//   上次异常退出可能没清理，不删会导致bind失败
    // AF_UNIX=Unix域走本地文件系统, SOCK_STREAM=TCP式可靠传输
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("[IPC] socket"); return -1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    // 把fd和文件路径绑定，内核创建这个socket文件
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(fd, backlog) < 0) {
        // 此时 fd 处于监听状态，只能用来 accept，不能收发数据。
        perror("[IPC] bind/listen");
        close(fd);
        return -1;
    }
    printf("[IPC] 监听: %s\n", path);
    return fd;// 返回监听fd，还不能通信，只能accept
}

// 服务器端：接受客户端连接（阻塞）
int ipc_sock_accept(int listen_fd)
{   // 阻塞等待客户端connect NULL=不关心客户端地址
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) perror("[IPC] accept");
    else        printf("[IPC] 客户端已连接 fd=%d\n", fd);
    // 返回新的通信fd，和客户端一对一通信
    // 注意：listen_fd继续监听，fd用于和这个客户端收发数据
    return fd;
}

// 客户端：连接服务器
int ipc_sock_client_connect(const char* path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);// 和服务器一样创建Unix域流式套接字
    if (fd < 0) { perror("[IPC] socket"); return -1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    //     path必须和服务器bind的路径完全一致
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0'; 

    //       向服务器发起连接，阻塞直到服务器accept
    //       成功返回后，fd可以直接用来收发数据
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[IPC] connect");
        close(fd);
        return -1;
    }
    printf("[IPC] 已连接: %s fd=%d\n", path, fd);
    return fd;
}

int ipc_sock_send_frame(int sock_fd, const FrameMeta* meta, int dma_fd)
{
    // ---------- 1. 准备普通数据：你的结构体 ----------
    struct iovec iov;                        // iovec：描述一块内存的基址和长度
    iov.iov_base = (void*)meta;              //   指向你的 FrameMeta 结构体
    iov.iov_len  = sizeof(FrameMeta);        //   结构体的大小

    // ---------- 2. 准备 msghdr：消息的"总控制块" ----------
    struct msghdr msg = {0};
    msg.msg_iov    = &iov;                   //   普通数据：指向上面的 iovec
    msg.msg_iovlen = 1;                      //   iovec 数组只有1个元素

    // ---------- 3. 准备辅助数据：传递 fd ----------
    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};  // 分配足够大的对齐缓冲区
    //   CMSG_SPACE = 头部12字节 + fd数据4字节 + 对齐填充 = 16字节

    if (dma_fd >= 0) {                       // 有有效的 fd 才传
        msg.msg_control    = cmsg_buf;        //   辅助数据缓冲区指针
        msg.msg_controllen = sizeof(cmsg_buf);//   辅助数据总大小（16字节）

        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);  // 定位到第一个控制消息头
        cmsg->cmsg_level = SOL_SOCKET;        //   协议层：套接字层（传fd必须用这个）
        cmsg->cmsg_type  = SCM_RIGHTS;         //   消息类型：传递文件描述符权限
        cmsg->cmsg_len   = CMSG_LEN(sizeof(int));    //   有效长度 = 头部(12) + 数据(4) = 16

        memcpy(CMSG_DATA(cmsg), &dma_fd, sizeof(int)); // 把 fd 值拷贝到控制消息数据区
        //     CMSG_DATA(cmsg)：跳过头部，指向后面4字节的数据区位置
    }

    // 4. 发送
    if (sendmsg(sock_fd, &msg, 0) < 0) {
        perror("[IPC] sendmsg");
        return -1;
    }
    return 0;
}

int ipc_sock_recv_frame(int sock_fd, FrameMeta* meta, int* dma_fd)
{
    // 1. 准备接收普通数据
    struct iovec iov;
    iov.iov_base = meta;
    iov.iov_len  = sizeof(FrameMeta);

    // 2. 准备接收辅助数据
    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};

    struct msghdr msg = {0};
    msg.msg_iov         = &iov;
    msg.msg_iovlen      = 1;
    msg.msg_control     = cmsg_buf;
    msg.msg_controllen  = sizeof(cmsg_buf);

    //阻塞接收
    ssize_t n = recvmsg(sock_fd, &msg, 0);// 一次接收：结构体 + fd
    if (n < 0) { perror("[IPC] recvmsg"); return -1; }
    if (n == 0) return 0;       // 对端关闭连接

    *dma_fd = -1;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);  // 提取控制消息
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
        memcpy(dma_fd, CMSG_DATA(cmsg), sizeof(int));
    return (int)n;
}

void ipc_sock_close(int fd, const char* path)
{
    if (fd >= 0) close(fd);
    //只有服务器端负责删除socket文件。
    //谁bind谁unlink，客户端只管关自己的fd。
    /**
     * ipc_sock_close(client_fd, NULL);     // 关通信fd，不删文件
     * ipc_sock_close(listen_fd, "/tmp/my.sock");  // 关监听fd，删文件
     */
    //客户端不创建文件，只是连接，没资格删。ipc_sock_close(sock_fd, NULL); 
    if (path)    unlink(path);
    printf("[IPC] Socket 已关闭: %s\n", path ? path : "(client)");
}