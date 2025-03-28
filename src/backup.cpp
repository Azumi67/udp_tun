#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <chrono>
#include <deque>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#define BUFSIZE 2000

int select_timeout_ms = 0;   // 0 = blocking; nonzero = low-latency mode.
int mode = 0;                // 0 = cost less bandwidth (default), 1 = low latency.
int sock_buf = 1024;         // UDP socket buffer in kB.
int log_level = 0;           // Logging disabled by default.
int keep_alive_interval = 0; // in seconds; 0 = disabled.
int dynamic_pacing = 0;      // 0 disabled, 1 enabled.
int jitter_buffer_ms = 0;    // jitter buffer duration in ms; 0 = disabled.
int multithread = 0;         // 0 = single-threaded (default), 1 = use dedicated threads.
int use_epoll = 0;           // 0 = use select() in UDP thread; 1 = use epoll (if multithread==1).
int retry_interval = 0;      // seconds to wait before reconnecting

std::atomic<bool> running(true);
std::atomic<bool> reconnect_required(false);

int parse_log_lvl(const std::string &lvl)
{
    if (lvl == "never")
        return 0;
    if (lvl == "fatal")
        return 1;
    if (lvl == "error")
        return 2;
    if (lvl == "warn")
        return 3;
    if (lvl == "info")
        return 4;
    if (lvl == "debug")
        return 5;
    if (lvl == "trace")
        return 6;
    return 0;
}

void log_msg(int level, const std::string &msg, const char *file = "", int line = 0)
{
    if (log_level == 0 || level > log_level)
        return;
    std::cout << "[" << file << ":" << line << "] " << msg << std::endl;
}

int tun_alloc(std::string &tunName, int flags)
{
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0)
    {
        perror("Opening /dev/net/tun");
        return fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (!tunName.empty())
        strncpy(ifr.ifr_name, tunName.c_str(), IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0)
    {
        perror("ioctl(TUNSETIFF)");
        close(fd);
        return -1;
    }
    tunName = ifr.ifr_name;
    return fd;
}

void config_interface(const std::string &ifname, const std::string &ip)
{
    std::ostringstream cmd;
    cmd << "ip addr add " << ip << " dev " << ifname;
    std::cout << "Configuring interface: " << cmd.str() << std::endl;
    if (system(cmd.str().c_str()) != 0)
    {
        std::cerr << "assign IP address failed.\n";
        exit(1);
    }
    cmd.str("");
    cmd << "ip link set dev " << ifname << " up";
    std::cout << "Bringing up interface: " << cmd.str() << std::endl;
    if (system(cmd.str().c_str()) != 0)
    {
        std::cerr << "failed to bring interface up.\n";
        exit(1);
    }
}

void set_mtu(const std::string &ifname, int mtu)
{
    std::ostringstream cmd;
    cmd << "ip link set dev " << ifname << " mtu " << mtu;
    std::cout << "Setting MTU: " << cmd.str() << std::endl;
    if (system(cmd.str().c_str()) != 0)
    {
        std::cerr << "failed to set MTU.\n";
        exit(1);
    }
}

// Simple XOR cipher
void xor_cipher(char *data, int len, const std::string &key)
{
    if (key.empty())
        return;
    size_t key_len = key.size();
    for (int i = 0; i < len; i++)
        data[i] ^= key[i % key_len];
}
// jitter , still testing
struct JitterPacket
{
    std::vector<char> data;
    std::chrono::steady_clock::time_point arrival;
};

void print_usage(const char *progname)
{
    std::cerr << "Usage Help Minus []: " << progname
              << " --server SERVER_IP [--ifname tunName] [--port portNumber] [--ip IP/mask] "
              << "[--mtu value] [--pwd password] [--retry seconds] [--mode 0|1] [--sock-buf number] "
              << "[--log-lvl level] [--keep-alive seconds] [--dynamic-pacing 0|1] [--jitter-buffer ms] "
              << "[--multithread 0|1] [--use-epoll 0|1] [-h]\n";
    exit(1);
}

void thread_tun_to_udp(int tun_fd, int sockfd, const std::string &password)
{
    char buffer[BUFSIZE];
    while (running.load())
    {
        int n = read(tun_fd, buffer, sizeof(buffer));
        if (n < 0)
        {
            perror("read from TUN");
            continue;
        }
        xor_cipher(buffer, n, password);
        int sent = send(sockfd, buffer, n, 0);
        if (sent < 0)
        {
            perror("send (TUN->UDP)");
            if (errno == ECONNREFUSED)
            {
                std::cerr << "send (TUN->UDP) failed: Connection refused. Reconnecting...\n";
                reconnect_required = true;
                running = false;
                break;
            }
        }
    }
}

void thread_udp_to_tun(int tun_fd, int sockfd, const std::string &password)
{
    char buffer[BUFSIZE];
    std::deque<JitterPacket> jitterQueue;
    if (use_epoll)
    {
        int epfd = epoll_create1(0);
        if (epfd < 0)
        {
            perror("epoll_create1");
            return;
        }
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = sockfd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
        {
            perror("epoll_ctl");
            close(epfd);
            return;
        }
        while (running.load())
        {
            int timeout = (select_timeout_ms > 0 ? select_timeout_ms : -1);
            int nfds = epoll_wait(epfd, &ev, 1, timeout);
            if (nfds < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("epoll_wait");
                break;
            }
            if (nfds > 0 && (ev.events & EPOLLIN))
            {
                int n = recv(sockfd, buffer, sizeof(buffer), 0);
                if (n < 0)
                {
                    perror("recv (UDP->TUN)");
                    continue;
                }
                if (n == 2 && std::string(buffer, n) == "KA")
                    continue;
                xor_cipher(buffer, n, password);
                if (jitter_buffer_ms > 0)
                {
                    JitterPacket jp;
                    jp.data.assign(buffer, buffer + n);
                    jp.arrival = std::chrono::steady_clock::now();
                    jitterQueue.push_back(jp);
                }
                else
                {
                    int written = write(tun_fd, buffer, n);
                    if (written < 0)
                        perror("write to TUN (UDP->TUN)");
                }
            }
            if (jitter_buffer_ms > 0)
            {
                auto now = std::chrono::steady_clock::now();
                while (!jitterQueue.empty())
                {
                    auto &jp = jitterQueue.front();
                    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - jp.arrival).count();
                    if (jitterQueue.size() == 1 || wait_ms >= jitter_buffer_ms)
                    {
                        int written = write(tun_fd, jp.data.data(), jp.data.size());
                        if (written < 0)
                            perror("write jittered packet to TUN");
                        jitterQueue.pop_front();
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
        close(epfd);
    }
    else
    {
        while (running.load())
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);
            timeval timeout;
            timeval *timeout_ptr = nullptr;
            if (select_timeout_ms > 0)
            {
                timeout.tv_sec = select_timeout_ms / 1000;
                timeout.tv_usec = (select_timeout_ms % 1000) * 1000;
                timeout_ptr = &timeout;
            }
            int ret = select(sockfd + 1, &readfds, nullptr, nullptr, timeout_ptr);
            if (ret < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("select");
                break;
            }
            if (FD_ISSET(sockfd, &readfds))
            {
                int n = recv(sockfd, buffer, sizeof(buffer), 0);
                if (n < 0)
                {
                    perror("recv (UDP->TUN)");
                    continue;
                }
                if (n == 2 && std::string(buffer, n) == "KA")
                    continue;
                xor_cipher(buffer, n, password);
                if (jitter_buffer_ms > 0)
                {
                    JitterPacket jp;
                    jp.data.assign(buffer, buffer + n);
                    jp.arrival = std::chrono::steady_clock::now();
                    jitterQueue.push_back(jp);
                }
                else
                {
                    int written = write(tun_fd, buffer, n);
                    if (written < 0)
                        perror("write to TUN (UDP->TUN)");
                }
            }
            if (jitter_buffer_ms > 0)
            {
                auto now = std::chrono::steady_clock::now();
                while (!jitterQueue.empty())
                {
                    auto &jp = jitterQueue.front();
                    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - jp.arrival).count();
                    if (jitterQueue.size() == 1 || wait_ms >= jitter_buffer_ms)
                    {
                        int written = write(tun_fd, jp.data.data(), jp.data.size());
                        if (written < 0)
                            perror("write jittered packet to TUN");
                        jitterQueue.pop_front();
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
    }
}

void thread_keep_alive(int sockfd)
{
    auto last = std::chrono::steady_clock::now();
    while (running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (keep_alive_interval > 0)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last).count() >= keep_alive_interval)
            {
                const char *ka = "KA";
                int sent = send(sockfd, ka, 2, 0);
                if (sent < 0)
                {
                    if (errno == ECONNREFUSED)
                    {
                        std::cerr << "Keep-alive failed: Connection refused. Reconnecting...\n";
                        reconnect_required = true;
                        running = false;
                        break;
                    }
                    else
                    {
                        perror("send keep-alive");
                    }
                }
                last = now;
            }
        }
    }
}

int run_client_single_thread(int tun_fd, int sockfd, const std::string &password)
{
    char buffer[BUFSIZE];
    std::deque<JitterPacket> jitterQueue;
    auto last_keep_alive = std::chrono::steady_clock::now();
    while (running.load())
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(tun_fd, &readfds);
        FD_SET(sockfd, &readfds);
        int maxfd = (tun_fd > sockfd ? tun_fd : sockfd) + 1;

        timeval timeout;
        timeval *timeout_ptr = nullptr;
        if (select_timeout_ms > 0)
        {
            timeout.tv_sec = select_timeout_ms / 1000;
            timeout.tv_usec = (select_timeout_ms % 1000) * 1000;
            timeout_ptr = &timeout;
        }
        int ret = select(maxfd, &readfds, nullptr, nullptr, timeout_ptr);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        if (keep_alive_interval > 0)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_keep_alive).count() >= keep_alive_interval)
            {
                const char *ka = "KA";
                int sent = send(sockfd, ka, 2, 0);
                if (sent < 0)
                {
                    if (errno == ECONNREFUSED)
                    {
                        std::cerr << "Keep-alive failed: Connection refused. Reconnecting...\n";
                        reconnect_required = true;
                        running = false;
                        break;
                    }
                    else
                    {
                        perror("send keep-alive");
                    }
                }
                last_keep_alive = now;
            }
        }
        if (FD_ISSET(tun_fd, &readfds))
        {
            int n = read(tun_fd, buffer, sizeof(buffer));
            if (n < 0)
            {
                perror("TUN read");
                continue;
            }
            xor_cipher(buffer, n, password);
            int sent = send(sockfd, buffer, n, 0);
            if (sent < 0)
            {
                perror("send (TUN->UDP)");
                if (errno == ECONNREFUSED)
                {
                    std::cerr << "send (TUN->UDP) failed: Connection refused. Reconnecting...\n";
                    reconnect_required = true;
                    running = false;
                    break;
                }
            }
        }
        if (FD_ISSET(sockfd, &readfds))
        {
            int n = recv(sockfd, buffer, sizeof(buffer), 0);
            if (n < 0)
            {
                perror("recv");
                continue;
            }
            if (n == 2 && std::string(buffer, n) == "KA")
                continue;
            xor_cipher(buffer, n, password);
            if (jitter_buffer_ms > 0)
            {
                JitterPacket jp;
                jp.data.assign(buffer, buffer + n);
                jp.arrival = std::chrono::steady_clock::now();
                jitterQueue.push_back(jp);
            }
            else
            {
                int written = write(tun_fd, buffer, n);
                if (written < 0)
                    perror("write to TUN (UDP->TUN)");
            }
        }
        if (jitter_buffer_ms > 0)
        {
            auto now = std::chrono::steady_clock::now();
            while (!jitterQueue.empty())
            {
                auto &jp = jitterQueue.front();
                auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - jp.arrival).count();
                if (jitterQueue.size() == 1 || wait_ms >= jitter_buffer_ms)
                {
                    int written = write(tun_fd, jp.data.data(), jp.data.size());
                    if (written < 0)
                        perror("write jittered packet to TUN");
                    jitterQueue.pop_front();
                }
                else
                {
                    break;
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    std::string tunName = "azumitan";
    std::string server_ip = "";
    int port = 8000;
    std::string ip_address = "";
    int mtu = 1500;
    std::string password = "";

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--server" && i + 1 < argc)
        {
            server_ip = argv[++i];
        }
        else if (arg == "--ifname" && i + 1 < argc)
        {
            tunName = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            port = std::stoi(argv[++i]);
        }
        else if (arg == "--ip" && i + 1 < argc)
        {
            ip_address = argv[++i];
        }
        else if (arg == "--mtu" && i + 1 < argc)
        {
            mtu = std::stoi(argv[++i]);
        }
        else if (arg == "--pwd" && i + 1 < argc)
        {
            password = argv[++i];
        }
        else if (arg == "--retry" && i + 1 < argc)
        {
            retry_interval = std::stoi(argv[++i]);
        }
        else if (arg == "--mode" && i + 1 < argc)
        {
            mode = std::stoi(argv[++i]);
            select_timeout_ms = (mode == 1 ? 5 : 0);
        }
        else if (arg == "--sock-buf" && i + 1 < argc)
        {
            sock_buf = std::stoi(argv[++i]);
            if (sock_buf < 10 || sock_buf > 10240)
            {
                std::cerr << "sock-buf must be between 10 and 10240\n";
                exit(1);
            }
        }
        else if (arg == "--log-lvl" && i + 1 < argc)
        {
            log_level = parse_log_lvl(argv[++i]);
        }
        else if (arg == "--keep-alive" && i + 1 < argc)
        {
            keep_alive_interval = std::stoi(argv[++i]);
        }
        else if ((arg == "--dynamic-pacing" || arg == "--adaptive-latency") && i + 1 < argc)
        {
            dynamic_pacing = std::stoi(argv[++i]);
        }
        else if (arg == "--jitter-buffer" && i + 1 < argc)
        {
            jitter_buffer_ms = std::stoi(argv[++i]);
        }
        else if (arg == "--multithread" && i + 1 < argc)
        {
            multithread = std::stoi(argv[++i]);
        }
        else if (arg == "--use-epoll" && i + 1 < argc)
        {
            use_epoll = std::stoi(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help")
        {
            print_usage(argv[0]);
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
        }
    }
    if (server_ip.empty())
    {
        std::cerr << "Server IP is required.\n";
        print_usage(argv[0]);
    }
    if (dynamic_pacing == 1)
        select_timeout_ms = 2;

    int tun_fd = tun_alloc(tunName, IFF_TUN | IFF_NO_PI);
    if (tun_fd < 0)
    {
        std::cerr << "error in connecting to TUN interface " << tunName << "\n";
        exit(1);
    }
    std::cout << "TUN interface " << tunName << " allocated\n";

    if (!ip_address.empty())
        config_interface(tunName, ip_address);
    if (mtu > 0)
        set_mtu(tunName, mtu);

    while (true)
    {
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0)
        {
            perror("socket");
            exit(1);
        }
        int buf_val = sock_buf * 1024;
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_val, sizeof(buf_val)) < 0)
            perror("setsockopt SO_SNDBUF");
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buf_val, sizeof(buf_val)) < 0)
            perror("setsockopt SO_RCVBUF");

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_aton(server_ip.c_str(), &server_addr.sin_addr) == 0)
        {
            std::cerr << "Invalid server IP.\n";
            exit(1);
        }
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("connect");
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::seconds(retry_interval));
            continue;
        }
        std::cout << "UDP socket connected to " << server_ip << ":" << port << "\n";

        reconnect_required = false;
        running = true;

        if (multithread == 1)
        {
            std::thread t1(thread_tun_to_udp, tun_fd, sockfd, password);
            std::thread t2(thread_udp_to_tun, tun_fd, sockfd, password);
            std::thread t3;
            if (keep_alive_interval > 0)
                t3 = std::thread(thread_keep_alive, sockfd);
            t1.join();
            t2.join();
            if (t3.joinable())
                t3.join();
        }
        else
        {
            run_client_single_thread(tun_fd, sockfd, password);
        }

        if (reconnect_required.load())
        {
            std::cout << "Reconnecting in " << retry_interval << " seconds...\n";
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::seconds(retry_interval));
            continue;
        }
        else
        {
            close(sockfd);
            break;
        }
    }

    return 0;
}
