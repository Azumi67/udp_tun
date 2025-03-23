#!/bin/bash
apt update -y
apt install wget -y
wget -O /etc/logo2.sh https://github.com/Azumi67/UDP2RAW_FEC/raw/main/logo2.sh
chmod +x /etc/logo2.sh
if [ -f "udp_tun.py" ]; then
    rm udp_tun.py
fi
wget https://github.com/Azumi67/udp_tun/releases/download/v1.0/udp_tun.py
python3 udp_tun.py
