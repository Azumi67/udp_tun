**این پروژه اموزشی که برای گیم استفاده میشود پس از تست کامل و ساخت اسکریپت قرار داده خواهد شد**

#Description

- This tunnel application creates a virtual TUN interface on both the client and the server and tunnels IP packets over a UDP connection. It supports optional encryption via a simple XOR cipher, dynamic pacing for adaptive latency control, jitter buffering to smooth out packet delivery, and a keep‑alive mechanism to maintain NAT mappings and detect connection problems. The tunnel can be run in either a multithreaded mode—with separate threads handling TUN-to-UDP and UDP-to-TUN transfers (optionally using epoll for scalable event handling)—or in a single‑threaded fallback mode. On the client side, a reconnect mechanism is provided so that if the UDP connection fails (for example, if a keep‑alive packet is met with an “ECONNREFUSED” error), the client will close the socket, wait for a configurable retry interval, and then reconnect automatically.

#Advanced Usage

- Server :
```
Usage: server [options]

The server listens on a specified UDP port and bridges packets between its TUN interface and remote clients.

Options:
  --ifname tunName         
        Set the name for the TUN interface to be allocated (default: "tun0").

  --port portNumber        
        UDP port number on which the server listens (default: 8000).

  --ip IP/mask             
        Local IP address and subnet (in CIDR notation) to assign to the TUN interface.
        Example: "50.22.22.1/24".

  --mtu value              
        Set the Maximum Transmission Unit (MTU) for the TUN interface (default: 1500).
        Adjust based on your network’s requirements.

  --pwd password           
        Password for the XOR encryption/decryption. If omitted, no encryption is applied.

  --mode 0|1               
        Operating mode:
          0 = bandwidth-efficient mode (default)
          1 = low-latency mode (sets a shorter select timeout).

  --sock-buf number        
        UDP socket buffer size in kilobytes (range: 10 to 10240; default: 1024).

  --log-lvl level          
        Set the logging level. Acceptable values:
          never, fatal, error, warn, info, debug, trace.
        (Default: logging disabled).

  --keep-alive seconds      
        Interval in seconds for sending keep-alive packets to connected clients.
        Note: The server sends keep-alives only when a valid client address is recorded.

  --dynamic-pacing 0|1      
        Enable (1) or disable (0) dynamic pacing, which adjusts the UDP read timeout
        for lower latency (default is disabled).

  --jitter-buffer ms       
        Duration (in milliseconds) for buffering incoming packets to smooth out jitter.
        (Set to 0 to disable jitter buffering.)

  --multithread 0|1        
        Run in multithreaded mode (1) or single-threaded mode (0).
        In multithreaded mode, separate threads handle packet forwarding.

  --use-epoll 0|1          
        In multithreaded mode, use epoll (1) for efficient UDP event handling
        instead of select (0).

  -h, --help               
        Display this advanced help message and exit.
```
- Client :
```

Required:
  --server SERVER_IP       
        Specifies the remote server’s IP address to connect to.

Options:
  --ifname tunName         
        Set the name for the TUN interface to be allocated (default: "tun0").
        
  --port portNumber        
        UDP port number on the server to connect to (default: 8000).

  --ip IP/mask             
        Local IP address and subnet (in CIDR notation) to assign to the TUN interface.
        Example: "50.22.22.2/24".

  --mtu value              
        Set the Maximum Transmission Unit (MTU) for the TUN interface (default: 1500).
        Adjust this value if you need to optimize for specific network conditions.

  --pwd password           
        Password for the simple XOR encryption/decryption. If omitted, no encryption is applied.
        
  --retry seconds          
        Retry interval (in seconds) for reconnecting if the connection is lost.
        This activates reconnect logic in multithreaded mode.

  --mode 0|1               
        Operating mode: 
          0 = bandwidth-efficient mode (default)
          1 = low-latency mode (sets a shorter select timeout).

  --sock-buf number        
        UDP socket buffer size in kilobytes (acceptable range: 10 to 10240; default: 1024).

  --log-lvl level          
        Set the logging level. Acceptable values:
          never, fatal, error, warn, info, debug, trace.
        (Default: logging disabled).

  --keep-alive seconds      
        Interval in seconds to send keep-alive packets. Helps maintain NAT bindings and
        detect connection failures.

  --dynamic-pacing 0|1      
        Enable (1) or disable (0) dynamic pacing. When enabled, the UDP socket timeout
        adapts to reduce latency (default is disabled).

  --jitter-buffer ms       
        Duration (in milliseconds) for buffering incoming packets to smooth jitter.
        (Set to 0 to disable jitter buffering.)

  --multithread 0|1        
        Run in multithreaded mode (1) or single-threaded mode (0). 
        Multithreaded mode uses separate threads for packet handling.

  --use-epoll 0|1          
        In multithreaded mode, use epoll (1) instead of select (0) for efficient UDP event handling.
        (Effective only when --multithread is set to 1.)

  -h, --help               
        Display this advanced help message and exit.
```
#Further description:

- **TUN Interface Creation and Configuration**
Both client and server allocate a TUN interface that acts as a virtual network adapter. This interface is then configured using standard Linux ip commands to assign an IP address and bring the interface up. The MTU can be tuned to match the network path characteristics.

- **UDP Tunnel**
Packets read from the TUN interface are encapsulated in UDP datagrams and sent to the remote endpoint. The client initiates the connection (using connect() for simplicity), while the server binds its UDP socket to listen for incoming packets from any client.

- **Optional XOR Encryption**
If a password is provided using the --pwd option, the tunnel applies a simple XOR cipher to every packet. This lightweight encryption is meant for basic obfuscation rather than high-security applications.

- **Keep-Alive Mechanism**
To maintain NAT mappings and detect dropped connections, both the client and server send periodic keep-alive ("KA") packets. The client monitors these for errors (such as “Connection refused”) and triggers a reconnect if necessary. The server only sends keep-alive packets when it has recorded a valid client address.

- **Dynamic Pacing and Jitter Buffering**
Dynamic pacing adjusts the select timeout values to lower latency in low‑latency mode (when --mode 1 is selected or dynamic pacing is enabled). Jitter buffering (when enabled via --jitter-buffer) temporarily holds incoming packets for a defined time window (in milliseconds) to smooth out network jitter before writing them to the TUN interface.

- **Multithreading and Epoll**
In multithreaded mode (--multithread 1), the tunnel uses dedicated threads for reading from and writing to the TUN interface, as well as for sending keep-alive packets. Additionally, using epoll (--use-epoll 1) in place of select() can provide more efficient event handling when there are many file descriptors or under heavy load.

- **Reconnect Logic (Client Side)**
The client continuously monitors for connection errors during data transmission (especially in the keep‑alive and TUN→UDP threads). If an error (e.g., ECONNREFUSED) is detected, a reconnect flag is set, the threads exit, and the main loop closes the socket. After waiting for the interval specified by --retry, the client attempts to reconnect to the server.
