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
#include <deque>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

#define BUFSIZE 2000

int select_timeout_ms = 0;              // 0 = blocking; nonzero = low-latency timeout
int mode = 0;                           // 0 = cost less bandwidth (default), 1 = low latency
int sock_buf = 1024;                    // UDP socket buffer in kB (default 1024)
int log_level = 0;                      // Logging disabled; valid levels: never, fatal, error, warn, info, debug, trace
int keep_alive_interval = 0;            // seconds; 0 = disabled
int dynamic_pacing = 0;                 // 0 disabled, 1 enabled
int jitter_buffer_ms = 0;               // jitter buffer duration in ms; 0 = disabled
int multithread = 0;                    // 0 = single-threaded (default), 1 = use dedicated threads
int use_epoll = 0;                      // 0 = use select() in UDP thread; 1 = use epoll (if multithread==1)
int multiplex = 0;                      // 0 = single client (default), 1 = multiplex mode enabled
int additional_obfuscation_enabled = 0; // 0 = disabled, 1 = enabled
const int rotate_offset = 3;            // Fixed offset for additional obfuscation

std::vector<struct sockaddr_in> g_client_addrs;
std::mutex g_client_addrs_mutex;

struct sockaddr_in g_client_addr;
std::mutex g_client_addr_mutex;

std::atomic<bool> running(true);

struct JitterPacket
{
    std::vector<char> data;
    std::chrono::steady_clock::time_point arrival;
};

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

void xor_cipher(char *data, int len, const std::string &key)
{
    if (key.empty())
        return;
    size_t key_len = key.size();
    for (int i = 0; i < len; i++)
        data[i] ^= key[i % key_len];
}

void rotate_cipher(char *data, int len, int offset)
{
    for (int i = 0; i < len; i++)
    {
        data[i] = static_cast<unsigned char>(data[i] + offset);
    }
}

void rotate_cipher_reverse(char *data, int len, int offset)
{
    for (int i = 0; i < len; i++)
    {
        data[i] = static_cast<unsigned char>(data[i] - offset);
    }
}

void apply_all_obfuscation(char *data, int len, const std::string &password)
{
    xor_cipher(data, len, password);
    if (additional_obfuscation_enabled)
        rotate_cipher(data, len, rotate_offset);
}

void remove_all_obfuscation(char *data, int len, const std::string &password)
{
    if (additional_obfuscation_enabled)
        rotate_cipher_reverse(data, len, rotate_offset);
    xor_cipher(data, len, password);
}

void print_usage(const char *progname)
{
    std::cerr << "Usage: " << progname
              << " [--ifname tunName] [--port portNumber] [--ip IP/mask] [--mtu value] [--pwd password] "
              << "[--mode 0|1] [--sock-buf number] [--log-lvl level] [--keep-alive seconds] "
              << "[--dynamic-pacing 0|1] [--jitter-buffer ms] [--multithread 0|1] [--use-epoll 0|1] "
              << "[--multiplex 0|1] [--obf 0|1] [-h]\n";
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
            perror("TUN read");
            continue;
        }

        apply_all_obfuscation(buffer, n, password);

        if (multiplex)
        {
            std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
            for (auto &client : g_client_addrs)
            {
                int sent = sendto(sockfd, buffer, n, 0, (struct sockaddr *)&client, sizeof(client));
                if (sent < 0)
                    perror("sendto (TUN->UDP) multiplex");
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(g_client_addr_mutex);
            if (g_client_addr.sin_port != 0)
            {
                int sent = sendto(sockfd, buffer, n, 0, (struct sockaddr *)&g_client_addr, sizeof(g_client_addr));
                if (sent < 0)
                    perror("sendto (TUN->UDP)");
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
                struct sockaddr_in addr;
                socklen_t addrlen = sizeof(addr);
                int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
                if (n < 0)
                {
                    perror("recvfrom (UDP->TUN)");
                    continue;
                }

                if (multiplex)
                {
                    std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                    bool found = false;
                    for (auto &client : g_client_addrs)
                    {
                        if (client.sin_addr.s_addr == addr.sin_addr.s_addr &&
                            client.sin_port == addr.sin_port)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        g_client_addrs.push_back(addr);
                        std::cout << "New client added: " << inet_ntoa(addr.sin_addr)
                                  << ":" << ntohs(addr.sin_port) << "\n";
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                    g_client_addr = addr;
                }

                char tmp[BUFSIZE];
                memcpy(tmp, buffer, n);
                remove_all_obfuscation(tmp, n, password);
                if (n == 2 && std::string(tmp, n) == "KA")
                    continue;

                remove_all_obfuscation(buffer, n, password);

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
                struct sockaddr_in addr;
                socklen_t addrlen = sizeof(addr);
                int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
                if (n < 0)
                {
                    perror("recvfrom (UDP->TUN)");
                    continue;
                }

                if (multiplex)
                {
                    std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                    bool found = false;
                    for (auto &client : g_client_addrs)
                    {
                        if (client.sin_addr.s_addr == addr.sin_addr.s_addr &&
                            client.sin_port == addr.sin_port)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        g_client_addrs.push_back(addr);
                        std::cout << "New client added: " << inet_ntoa(addr.sin_addr)
                                  << ":" << ntohs(addr.sin_port) << "\n";
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                    g_client_addr = addr;
                }
                char tmp[BUFSIZE];
                memcpy(tmp, buffer, n);
                remove_all_obfuscation(tmp, n, password);
                if (n == 2 && std::string(tmp, n) == "KA")
                    continue;
                remove_all_obfuscation(buffer, n, password);
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

void thread_keep_alive(int sockfd, const std::string &password)
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
                char ka[3] = "KA";
                apply_all_obfuscation(ka, 2, password);
                if (multiplex)
                {
                    std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                    for (auto &client : g_client_addrs)
                    {
                        int sent = sendto(sockfd, ka, 2, 0, (struct sockaddr *)&client, sizeof(client));
                        if (sent < 0)
                            perror("sendto keep-alive multiplex");
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                    if (g_client_addr.sin_port == 0 || g_client_addr.sin_addr.s_addr == 0)
                    {
                        last = now;
                        continue;
                    }
                    int sent = sendto(sockfd, ka, 2, 0, (struct sockaddr *)&g_client_addr, sizeof(g_client_addr));
                    if (sent < 0)
                        perror("sendto keep-alive");
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
    socklen_t addrlen = sizeof(struct sockaddr_in);
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
                char ka[3] = "KA";
                apply_all_obfuscation(ka, 2, password);
                if (multiplex)
                {
                    std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                    for (auto &client : g_client_addrs)
                    {
                        if (sendto(sockfd, ka, 2, 0, (struct sockaddr *)&client, sizeof(client)) < 0)
                            perror("sendto keep-alive multiplex");
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                    if (g_client_addr.sin_port != 0 && g_client_addr.sin_addr.s_addr != 0)
                    {
                        if (sendto(sockfd, ka, 2, 0, (struct sockaddr *)&g_client_addr, sizeof(g_client_addr)) < 0)
                            perror("sendto keep-alive");
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
            apply_all_obfuscation(buffer, n, password);
            if (multiplex)
            {
                std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                for (auto &client : g_client_addrs)
                {
                    if (sendto(sockfd, buffer, n, 0, (struct sockaddr *)&client, sizeof(client)) < 0)
                        perror("sendto (TUN->UDP) multiplex");
                }
            }
            else
            {
                std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                if (g_client_addr.sin_port != 0)
                {
                    if (sendto(sockfd, buffer, n, 0, (struct sockaddr *)&g_client_addr, sizeof(g_client_addr)) < 0)
                        perror("sendto (TUN->UDP)");
                }
            }
        }

        if (FD_ISSET(sockfd, &readfds))
        {
            struct sockaddr_in client_addr;
            memset(&client_addr, 0, sizeof(client_addr));
            int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addrlen);
            if (n < 0)
            {
                perror("recvfrom");
                continue;
            }
            if (multiplex)
            {
                std::lock_guard<std::mutex> lock(g_client_addrs_mutex);
                bool found = false;
                for (auto &client : g_client_addrs)
                {
                    if (client.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                        client.sin_port == client_addr.sin_port)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    g_client_addrs.push_back(client_addr);
                    std::cout << "New client added: " << inet_ntoa(client_addr.sin_addr)
                              << ":" << ntohs(client_addr.sin_port) << "\n";
                }
            }
            else
            {
                std::lock_guard<std::mutex> lock(g_client_addr_mutex);
                g_client_addr = client_addr;
            }
            char tmp[BUFSIZE];
            memcpy(tmp, buffer, n);
            remove_all_obfuscation(tmp, n, password);
            if (n == 2 && std::string(tmp, n) == "KA")
                continue;
            remove_all_obfuscation(buffer, n, password);
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
        if (arg == "--ifname" && i + 1 < argc)
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
        else if (arg == "--multiplex" && i + 1 < argc)
        {
            multiplex = std::stoi(argv[++i]);
        }
        else if (arg == "--obf" && i + 1 < argc)
        {
            additional_obfuscation_enabled = std::stoi(argv[++i]);
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

    if (dynamic_pacing == 1)
        select_timeout_ms = 2;

    int tun_fd = tun_alloc(tunName, IFF_TUN | IFF_NO_PI);
    if (tun_fd < 0)
    {
        std::cerr << "error connecting to TUN interface " << tunName << "\n";
        exit(1);
    }
    std::cout << "TUN interface " << tunName << " allocated\n";

    if (!ip_address.empty())
        config_interface(tunName, ip_address);
    if (mtu > 0)
        set_mtu(tunName, mtu);

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
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(1);
    }
    std::cout << "UDP socket bound to port " << port << "\n";

    if (multithread == 1)
    {
        std::thread t1(thread_tun_to_udp, tun_fd, sockfd, password);
        std::thread t2(thread_udp_to_tun, tun_fd, sockfd, password);
        std::thread t3;
        if (keep_alive_interval > 0)
            t3 = std::thread(thread_keep_alive, sockfd, password);
        t1.join();
        t2.join();
        if (t3.joinable())
            t3.join();
    }
    else
    {
        run_client_single_thread(tun_fd, sockfd, password);
    }

    return 0;
}
