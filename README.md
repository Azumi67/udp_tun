![R (2)](https://github.com/Azumi67/PrivateIP-Tunnel/assets/119934376/a064577c-9302-4f43-b3bf-3d4f84245a6f)
نام پروژه :  تانل udp (پروژه آموزشی)
---------------------------------------------------------------
**توضیحات**
- من از این پروژه برای مدتی داخل گیم هایم استفاده کرده ام و راضی بوده ام. اگر در سرور شما کار نکرد، سرور خارج را عوض کنید
- برای surf و browing هم مشکلی ندارد
- در اموزش مقادیری را قرار دادم که برای خودم خوب کار میکند. ممکن است در سرور شما با مقادیر متفاوتی نتیجه شبیه من را بدهد.
- مانند tiny vpn از xor encryption استفاده شده است که چیز خاصی به حساب نمی اید
- سرور میتواند ایران یا خارج باشد
- گزینه multiplex و Obfuscation اضافه شد

--------
![6348248](https://github.com/Azumi67/PrivateIP-Tunnel/assets/119934376/398f8b07-65be-472e-9821-631f7b70f783)
**آموزش نصب با اسکریپت**
 <div align="right">
  <details>
    <summary><strong><img src="https://github.com/Azumi67/Rathole_reverseTunnel/assets/119934376/fcbbdc62-2de5-48aa-bbdd-e323e96a62b5" alt="Image"> </strong>نصب udp_tun</summary>

------------------------------------ 

<p align="right">
  <img src="https://github.com/user-attachments/assets/23e8c2ca-2c9a-44b2-9030-9a0c906b08a8" alt="Image" />
  </p>

- بین سرور و کلاینت یک پرایوت ایپی ایجاد میکنیم و از این پرایوت ایپی در تانل ها و پورت فوروارد ها استفاده مینماییم
- نخست سرور را کانفیگ میکنم.نام دیوایس را ازومی قرار میدهم و پرایوت ایچی را به صورت 60.22.22.1/24 وارد میکنم
- مقدار mtu را 1250 یا هر مقداری که دوست دارید قرار میدهم. xor را قعال میکنم و پسورد ازومی را وارد میکنم
-  پورت تانل را 8005 میذارم. mode را فعال میکنم و بافر را در سرور 1024 قرار میدهم. شما میتوانید بیشتر کنید و تست نمایید
-  لاگ را info قرار میدهم و مقدار keep alive را بر روی 10 ثانیه قرار میدهم
-  در سرور dynamic pacing را غیر فعال میکنم و Jitter buffer ]م بر روی صفر قرار میدهم. بسته به سرور شما میتواند متفاوت باشد
-  مولتی thread و epoll را فعال میکنم. شما میتوانید بیسته به نیاز خود ان را غیر فعال کنید
**کلاینت**
<p align="right">
  <img src="https://github.com/user-attachments/assets/ab310888-0047-4c7a-ba0e-935a342b1c0f" alt="Image" />
  </p>

- حالا در کلاینت ایپی پابلیک ورژن 4 سرور را وارد میکنم و سپس نام دیوایس و پرایوت ایپی را وارد میکنم . 60.22.22.2/24
- مقدار mtu را 1250 و پورت تانل هم 8005 وارد میکنم
- مقدار retry را 5 یا هر عددی قرار میدهم که در صورت قطع شدن اتصال، دوباره وصل شود
- در کلاینت هم mode را فعال میکنم و مقدار 1 را وارد میکنم
- بافر را 2048 در کلاینت قرار میدهم. میتوانید عدد های متفاوتی بسته به نیاز خود قرار دهید
- لاگ را بر روی Info و مقدار Keep alive هم بر روی 10 قرار میدهم
- در کلاینت dynamic pacing را فعال میکنم. این میتواند در سرور شما متفاوت باشد
- بقیه موارد مانند سرور وارد میکنم

------------------

  </details>
</div>  

--------------
![R (a2)](https://github.com/Azumi67/PrivateIP-Tunnel/assets/119934376/716fd45e-635c-4796-b8cf-856024e5b2b2)
**اسکریپت من**
----------------

- نصب پیش نیاز ها
```
apt install python3 -y && sudo apt install python3-pip &&  pip install colorama && pip install netifaces && apt install curl -y
pip3 install colorama
sudo apt-get install python-pip -y  &&  apt-get install python3 -y && alias python=python3 && python -m pip install colorama && python -m pip install netifaces
sudo apt update -y && sudo apt install -y python3 python3-pip curl && pip3 install --upgrade pip && pip3 install netifaces colorama requests

```
- اجرای اسکریپت
```
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Azumi67/udp_tun/refs/heads/main/udp_tun.sh)"
```

--------
**Description**

- This tunnel application creates a virtual TUN interface on both the client and the server and tunnels IP packets over a UDP connection. It supports optional encryption via a simple XOR cipher, dynamic pacing for adaptive latency control, jitter buffering to smooth out packet delivery, and a keep‑alive mechanism to maintain NAT mappings and detect connection problems. The tunnel can be run in either a multithreaded mode—with separate threads handling TUN-to-UDP and UDP-to-TUN transfers (optionally using epoll for scalable event handling)—or in a single‑threaded fallback mode. On the client side, a reconnect mechanism is provided so that if the UDP connection fails (for example, if a keep‑alive packet is met with an “ECONNREFUSED” error), the client will close the socket, wait for a configurable retry interval, and then reconnect automatically.
It also supports multiplex to send traffic on one udp socket on server to different clients when enabled.It also adds another obf to xor which migh help to camouflage traffic.

------------------

**CLI Samples**(V1.2)
- Server
```
./server --ifname tun0 --ip 50.22.22.1/24 --mtu 1250 --pwd mypassword --port 8004 --mode 1 --sock-buf 1024 --log-lvl info --keep-alive 10 --dynamic-pacing 0 --jitter-buffer 0 --multithread 1 --use-epoll 1 --multiplex 1 --obf 1
```
- Client
```
./client --server ip Server --ifname tun0 --ip 50.22.22.2/24 --mtu 1250 --port 8004 --retry 5 --mode 1 --sock-buf 2048 --log-lvl info --keep-alive 10 --dynamic-pacing 1 --jitter-buffer 0 --multithread 1 --use-epoll 1 --multiplex 1 --obf 1
```

**Advanced Usage**(V1.2)

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

  --multiplex 0|1
        The client always uses a single UDP connection, while the server (if multiplex enabled) can forward packets to multiple clients via one socket.

  --obf 0|1
         The additional obfuscation is optional via the “--obf” flag (set to 1 to enable).
         All outgoing data (including keep-alive messages) is processed with XOR and then a simple rotate (by 3 bytes) if obfuscation is enabled.

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

  --multiplex 0|1
        The client always uses a single UDP connection, while the server (if multiplex enabled) can forward packets to multiple clients via one socket.

  --obf 0|1
         The additional obfuscation is optional via the “--obf” flag (set to 1 to enable).
         All outgoing data (including keep-alive messages) is processed with XOR and then a simple rotate (by 3 bytes) if obfuscation is enabled.

  -h, --help               
        Display this advanced help message and exit.
```
**Further description:**

- **TUN Interface Creation and Configuration**:
Both client and server allocate a TUN interface that acts as a virtual network adapter. This interface is then configured using standard Linux ip commands to assign an IP address and bring the interface up. The MTU can be tuned to match the network path characteristics.

- **UDP Tunnel**:
Packets read from the TUN interface are encapsulated in UDP datagrams and sent to the remote endpoint. The client initiates the connection (using connect() for simplicity), while the server binds its UDP socket to listen for incoming packets from any client.

- **Optional XOR Encryption**:
If a password is provided using the --pwd option, the tunnel applies a simple XOR cipher to every packet. This lightweight encryption is meant for basic obfuscation rather than high-security applications.

- **Keep-Alive Mechanism**:
To maintain NAT mappings and detect dropped connections, both the client and server send periodic keep-alive ("KA") packets. The client monitors these for errors (such as “Connection refused”) and triggers a reconnect if necessary. The server only sends keep-alive packets when it has recorded a valid client address.

- **Dynamic Pacing and Jitter Buffering**
Dynamic pacing adjusts the select timeout values to lower latency in low‑latency mode (when --mode 1 is selected or dynamic pacing is enabled). Jitter buffering (when enabled via --jitter-buffer) temporarily holds incoming packets for a defined time window (in milliseconds) to smooth out network jitter before writing them to the TUN interface.

- **Multithreading and Epoll**:
In multithreaded mode (--multithread 1), the tunnel uses dedicated threads for reading from and writing to the TUN interface, as well as for sending keep-alive packets. Additionally, using epoll (--use-epoll 1) in place of select() can provide more efficient event handling when there are many file descriptors or under heavy load.

- **Reconnect Logic (Client Side)**:
The client continuously monitors for connection errors during data transmission (especially in the keep‑alive and TUN→UDP threads). If an error (e.g., ECONNREFUSED) is detected, a reconnect flag is set, the threads exit, and the main loop closes the socket. After waiting for the interval specified by --retry, the client attempts to reconnect to the server.

- **Obfuscation vs. Encryption:
The additional obfuscation (XOR plus a fixed byte rotation) is a very lightweight transformation meant to obscure the traffic. It might help hide the fact that the traffic is VPN data from casual inspection, but it is not cryptographically secure.
The provided mechanism is primarily meant for basic obfuscation to make the VPN traffic look more like normal UDP traffic. it might suffice for avoiding superficial DPI, But for any serious security requirements, this is not a substitute for proper encryption.
