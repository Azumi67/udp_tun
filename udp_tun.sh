#!/bin/bash

RED="\033[91m"
GREEN="\033[92m"
YELLOW="\033[93m"
BLUE="\033[94m"
CYAN="\033[96m"
RESET="\033[0m"

clear
echo -e "${CYAN}============================================${RESET}"
echo -e "${BLUE}         UDP_TUN Installation Menu          ${RESET}"
echo -e "${CYAN}============================================${RESET}"
echo -e "${YELLOW}Select the version to install:${RESET}"
echo -e "${GREEN}1) Version 1.1${RESET}"
echo -e "${YELLOW}2) Version 1.2${RESET}"
echo -e "${CYAN}--------------------------------------------${RESET}"
read -p "Enter your choice (1 or 2): " choice

if [ "$choice" == "1" ]; then
    echo -e "\n${BLUE}Installing Version 1.1...${RESET}"
    apt update -y
    apt install wget -y
    echo -e "${GREEN}Downloading logo ...${RESET}"
    wget -O /etc/logo2.sh https://github.com/Azumi67/UDP2RAW_FEC/raw/main/logo2.sh
    chmod +x /etc/logo2.sh
    if [ -f "udp_tun.py" ]; then
        echo -e "${YELLOW}Removing existing udp_tun ...${RESET}"
        rm udp_tun.py
    fi
    echo -e "${YELLOW}Downloading udp_tun.py for Version 1.1...${RESET}"
    wget https://github.com/Azumi67/udp_tun/releases/download/v1.1/udp_tun.py
    echo -e "${GREEN}Launching udp_tun.py...${RESET}"
    python3 udp_tun.py
elif [ "$choice" == "2" ]; then
    echo -e "\n${BLUE}Installing Version 1.2...${RESET}"
    apt update -y
    apt install wget -y
    echo -e "${GREEN}Downloading logo ...${RESET}"
    wget -O /etc/logo2.sh https://github.com/Azumi67/UDP2RAW_FEC/raw/main/logo2.sh
    chmod +x /etc/logo2.sh
    if [ -f "udp_tun_1.2.py" ]; then
        echo -e "${YELLOW}Removing existing udp_tun_1.2...${RESET}"
        rm udp_tun_1.2.py
    fi
    echo -e "${YELLOW}Downloading udp_tun_1.2.py for Version 1.2...${RESET}"
    wget https://github.com/Azumi67/udp_tun/releases/download/v1.2/udp_tun_1.2.py
    echo -e "${GREEN}Launching udp_tun_1.2.py...${RESET}"
    python3 udp_tun_1.2.py
else
    echo -e "\n${RED}Invalid choice. Exiting.${RESET}"
    exit 1
fi
