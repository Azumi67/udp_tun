#!/usr/bin/env python3
"""
====================================================================
 UDP_TUN Deployment Script
====================================================================
This script will:
  - Clone the udp_tun repository from GitHub into /usr/local/bin
  - Compile the C++ source (client or server) based on user selection
  - Prompt for configuration parameters interactively (with no pre-defined defaults)
  - Write the complete command into a systemd service file
  - Reload systemd and enable/start the service
  - Display the TUN private IP in a nicely formatted ASCII box
  - Show service status and details (e.g. dynamic pacing, XOR encryption, etc.)
  - Provide an Uninstall button (stub) and an Edit option (stub)
====================================================================
"""

import os
import subprocess
import sys
import io
import re
import readline

sys.stdout = io.TextIOWrapper(sys.stdout.detach(), encoding="utf-8", errors="replace")

if os.geteuid() != 0:
    print("\033[91mThis script must be run as root. Please use sudo -i.\033[0m")
    sys.exit(1)

def logo():
    logo_path = "/etc/logo2.sh"
    try:
        subprocess.run(["bash", "-c", logo_path], check=True)
    except subprocess.CalledProcessError as e:
        return e
    return None

def display_checkmark(message):
    print("\u2714 " + message)

def display_error(message):
    print("\u2718 Error: " + message)

def display_notification(message):
    print("\u2728 " + message)

def clear():
    os.system("clear")


RED     = "\033[91m"
GREEN   = "\033[92m"
YELLOW  = "\033[93m"
BLUE    = "\033[94m"
MAGENTA = "\033[95m"
CYAN    = "\033[96m"
RESET   = "\033[0m"


def run_cmd(cmd, cwd=None):
    try:
        print(f"{CYAN}Running: {cmd}{RESET}")
        subprocess.run(cmd, shell=True, check=True, cwd=cwd)
    except subprocess.CalledProcessError:
        print(f"{RED}Error: Command failed: {cmd}{RESET}")
        sys.exit(1)

def display_box(message):
    lines = message.splitlines()
    width = max(len(line) for line in lines)
    print(f"{GREEN}+" + "-"*(width+2) + "+")
    for line in lines:
        print("| " + line.ljust(width) + " |")
    print("+" + "-"*(width+2) + f"+{RESET}")


def install_prestuff():
    print(f"{CYAN}{'-'*40}{RESET}")
    print(f"{BLUE}Installing required stuff for Debian/Ubuntu...{RESET}")
    run_cmd("apt-get update -y")
    run_cmd("apt-get install -y git g++")
    print(f"{GREEN}Required packages installed successfully.{RESET}")
    print(f"{CYAN}{'-'*40}{RESET}")

def clone_repository(repo_url, dest_dir):
    print(f"{CYAN}{'-'*40}{RESET}")
    if os.path.exists(dest_dir):
        print(f"{YELLOW}Directory '{dest_dir}' already exists. Skipping clone.{RESET}")
    else:
        print(f"{GREEN}Cloning repository from '{repo_url}' into '{dest_dir}'...{RESET}")
        run_cmd(f"git clone {repo_url} {dest_dir}")
    print(f"{CYAN}{'-'*40}{RESET}")

def compile_source(source_file, output_file, work_dir):
    print(f"{CYAN}{'-'*40}{RESET}")
    source_path = os.path.join(work_dir, "src", source_file)
    if not os.path.exists(source_path):
        print(f"{RED}Source file '{source_path}' not found.{RESET}")
        sys.exit(1)
    if source_file == "client.cpp":
        compile_cmd = f"g++ -std=c++11 -O2 {source_path} -o client -lpthread"
    elif source_file == "server.cpp":
        compile_cmd = f"g++ -std=c++11 -O2 {source_path} -o server -lpthread"
    else:
        print(f"{RED}Unknown source file: {source_file}{RESET}")
        sys.exit(1)
    print(f"{GREEN}Compiling '{source_file}' from '{source_path}' ...{RESET}")
    run_cmd(compile_cmd, cwd=work_dir)
    print(f"{CYAN}{'-'*40}{RESET}")



def create_service_file(service_name, exec_command, description, work_dir):
    service_content = f"""[Unit]
Description={description}
After=network.target

[Service]
ExecStart={exec_command}
Restart=always
User=root
WorkingDirectory={work_dir}

[Install]
WantedBy=multi-user.target
"""
    service_file_path = f"/etc/systemd/system/{service_name}.service"
    print(f"{GREEN}Creating service file {service_file_path} ...{RESET}")
    try:
        with open(service_file_path, "w") as f:
            f.write(service_content)
    except Exception as e:
        print(f"{RED}failed to write service file: {e}{RESET}")
        sys.exit(1)
    run_cmd("systemctl daemon-reload")
    run_cmd(f"systemctl enable {service_name}")
    run_cmd(f"systemctl start {service_name}")
    print(f"{GREEN}Service {service_name} is now running.{RESET}")


def ask_server_config():
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{BLUE}=== Server Configuration ==={RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{YELLOW}Tip: Configure the server settings. For example:\n"
          f"  - ifname: The network interface for the TUN device (e.g, azumi).\n"
          f"  - ip: The TUN IP address (e.g, 50.22.22.1/24).\n"
          f"  - mtu: The maximum transmission unit size.\n"
          f"  - mode: 1 to enable, 0 to disable it.\n"
          f"  - Other options adjust socket buffer size, logging, keep-alive, pacing, etc.{RESET}\n")
    print("\033[93m───────────────────────────────────────\033[0m")
    
    server_ifname = input(f"{YELLOW}Enter {GREEN}Server network interface{RESET} (e.g, azumi){YELLOW}: {RESET}").strip()
    server_ip     = input(f"{YELLOW}Enter {GREEN}Server TUN IP {RESET}(e.g, 50.22.22.1/24){YELLOW}: {RESET}").strip()
    server_mtu    = input(f"{YELLOW}Enter {GREEN}Server MTU {RESET}(e.g, 1250){YELLOW}: {RESET}").strip()
    
    enable_xor = input(f"{YELLOW}Do you want to {GREEN}Enable XOR encryption{YELLOW} for server? ({GREEN}y{YELLOW}/{RED}n{YELLOW}): {RESET}").strip().lower()
    if enable_xor in ['y', 'yes']:
        server_pwd = input(f"{YELLOW}Enter{GREEN} XOR password{YELLOW}: {RESET}").strip()
        pwd_flag = f"--pwd {server_pwd}"
    else:
        pwd_flag = ""
    
    server_port          = input(f"{YELLOW}Enter Server {GREEN}port {RESET}(e.g, 8004){YELLOW}: {RESET}").strip()
    server_mode          = input(f"{YELLOW}Enter Server {GREEN}mode {RESET}(0 for disable, 1 for enable){YELLOW}: {RESET}").strip()
    server_sock_buf      = input(f"{YELLOW}Enter Server {GREEN}socket buffer {RESET}(e.g, 1024){YELLOW}: {RESET}").strip()
    server_log_lvl       = input(f"{YELLOW}Enter Server {GREEN}log level {RESET}(e.g, info){YELLOW}: {RESET}").strip()
    server_keep_alive    = input(f"{YELLOW}Enter Server {GREEN}keep-alive {RESET}(e.g, 10){YELLOW}: {RESET}").strip()
    server_dynamic_pacing= input(f"{YELLOW}Do you want to {GREEN}Enable dynamic pacing{YELLOW}? {RESET}(1 to enable, 0 to disable){YELLOW}: {RESET}").strip()
    server_jitter_buffer = input(f"{YELLOW}Enter Server {GREEN}jitter buffer {RESET}(e.g, in seconds 0){YELLOW}: {RESET}").strip()
    server_multithread   = input(f"{YELLOW}Do you want to {GREEN}Enable multithread{YELLOW}? {RESET}(1 to enable, 0 to disable){YELLOW}: {RESET}").strip()
    server_use_epoll     = input(f"{YELLOW}Do you want to {GREEN}Use epoll{YELLOW}? {RESET}(1 to enable, 0 to disable){YELLOW}: {RESET}").strip()
    
    server_cmd = (
        f"./server --ifname {server_ifname} --ip {server_ip} --mtu {server_mtu} "
        f"{pwd_flag} --port {server_port} --mode {server_mode} --sock-buf {server_sock_buf} "
        f"--log-lvl {server_log_lvl} --keep-alive {server_keep_alive} "
        f"--dynamic-pacing {server_dynamic_pacing} --jitter-buffer {server_jitter_buffer} "
        f"--multithread {server_multithread} --use-epoll {server_use_epoll}"
    )
    input(f"\n{RESET}Press Enter to return to the main menu...{RESET}")
    main_menu()
    return server_cmd, server_ip

def ask_client_config():
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{BLUE}=== Client Configuration ==={RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{YELLOW}Tip: Configure the client settings. For example:\n"
          f"  - server: The IP address of the remote server (e.g, 206.100.101.102).\n"
          f"  - ifname: The network interface for the TUN device (e.g, azumi).\n"
          f"  - ip: The TUN IP address (e.g, 50.22.22.2/24).\n"
          f"  - retry: The number of connection retries.\n"
          f"  - mode: 1 to enable, 0 to disable it.\n"
          f"  - Other options adjust socket buffer size, logging, keep-alive, pacing, etc.{RESET}\n")
    print("\033[93m───────────────────────────────────────\033[0m")
    
    client_server_ip = input(f"{YELLOW}Enter {GREEN}Server IP {RESET}(e.g, 206.100.101.102){YELLOW}: {RESET}").strip()
    client_ifname    = input(f"{YELLOW}Enter Client {GREEN}network interface {RESET}(e.g, azumi){YELLOW}: {RESET}").strip()
    client_ip        = input(f"{YELLOW}Enter Client {GREEN}TUN IP {RESET}(e.g, 50.22.22.2/24){YELLOW}: {RESET}").strip()
    client_mtu       = input(f"{YELLOW}Enter Client {GREEN}MTU {RESET}(e.g, 1250){YELLOW}: {RESET}").strip()
    
    enable_xor = input(f"{YELLOW}Do you want to {GREEN}enable XOR encryption{YELLOW}? ({GREEN}y{YELLOW}/{RED}n{YELLOW}): {RESET}").strip().lower()
    if enable_xor in ['y', 'yes']:
        client_pwd = input(f"{YELLOW}Enter {GREEN}XOR password{YELLOW}: {RESET}").strip()
        xor_flag = f"--pwd {client_pwd}"
    else:
        xor_flag = ""
    
    client_port      = input(f"{YELLOW}Enter Client {GREEN}port {RESET}(e.g, 8004){YELLOW}: {RESET}").strip()
    client_retry     = input(f"{YELLOW}Enter Client {GREEN}retry count {RESET}(e.g, 5){YELLOW}: {RESET}").strip()
    client_mode      = input(f"{YELLOW}Enter Client {GREEN}mode {RESET}(0 for disable, 1 for enable){YELLOW}: {RESET}").strip()
    client_sock_buf  = input(f"{YELLOW}Enter Client {GREEN}socket buffer {RESET}(e.g, 2048){YELLOW}: {RESET}").strip()
    client_log_lvl   = input(f"{YELLOW}Enter Client {GREEN}log level {RESET}(e.g, info){YELLOW}: {RESET}").strip()
    client_keep_alive= input(f"{YELLOW}Enter Client {GREEN}keep-alive {RESET}(e.g, 10){YELLOW}: {RESET}").strip()
    client_dynamic_pacing = input(f"{YELLOW}Do you want to {GREEN}enable dynamic pacing{YELLOW}? ({RESET}1 to enable, 0 to disable{YELLOW}): {RESET}").strip()
    client_jitter_buffer  = input(f"{YELLOW}Enter Client {GREEN}jitter buffer {YELLOW}(e.g, in seconds 0): {RESET}").strip()
    client_multithread    = input(f"{YELLOW}Do you want to {GREEN}enable multithread{YELLOW}? ({RESET}1 to enable, 0 to disable{YELLOW}): {RESET}").strip()
    client_use_epoll      = input(f"{YELLOW}Do you want to {GREEN}use epoll{YELLOW}? ({RESET}1 to enable, 0 to disable{YELLOW}): {RESET}").strip()
    
    client_cmd = (
        f"./client --server {client_server_ip} --ifname {client_ifname} --ip {client_ip} "
        f"--mtu {client_mtu} {xor_flag} --port {client_port} --retry {client_retry} --mode {client_mode} "
        f"--sock-buf {client_sock_buf} --log-lvl {client_log_lvl} --keep-alive {client_keep_alive} "
        f"--dynamic-pacing {client_dynamic_pacing} --jitter-buffer {client_jitter_buffer} "
        f"--multithread {client_multithread} --use-epoll {client_use_epoll}"
    )
    input(f"\n{RESET}Press Enter to return to the main menu...{RESET}")
    main_menu()
    return client_cmd, client_ip
    


def udp_status():
    os.system("clear")
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[93mUDP Tun \033[92mStatusMenu\033[0m")
    print('\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    
    services = ["udp_tun_server", "udp_tun_client"]
    available_services = []
    
    for service in services:
        service_file = f"/etc/systemd/system/{service}.service"
        if os.path.exists(service_file):
            available_services.append(service)
    
    if not available_services:
        print(f"\033[91mNo UDP_TUN services found.\033[0m")
        input(f"\n\033[93mPress Enter to return to the main menu...\033[0m")
        main_menu()
        return

    for service in available_services:
        print("\033[93m───────────────────────────────────────\033[0m")
        print(f"\n\033[93m--- Checking status for {service} ---\033[0m")
        print("\033[93m───────────────────────────────────────\033[0m")
        
        try:
            status_output = subprocess.check_output(
                f"systemctl is-active {service}", shell=True
            ).decode().strip()
        except subprocess.CalledProcessError:
            status_output = "inactive or not found"
        print(f"\033[96mStatus: {status_output}\033[0m")
        
        service_file = f"/etc/systemd/system/{service}.service"
        print(f"\033[95mService File: {service_file}\033[0m")
        with open(service_file, "r") as f:
            content = f.read()
        exec_line = ""
        for line in content.splitlines():
            if line.startswith("ExecStart="):
                exec_line = line[len("ExecStart="):].strip()
                break
        print(f"\033[95mService Command: {exec_line}\033[0m")
        
        details = {}
        if "--dynamic-pacing 1" in exec_line:
            details["Dynamic Pacing"] = "Enabled"
        elif "--dynamic-pacing 0" in exec_line:
            details["Dynamic Pacing"] = "Disabled"
        if "--jitter-buffer 1" in exec_line:
            details["Jitter Buffer"] = "Enabled"
        elif "--jitter-buffer 0" in exec_line:
            details["Jitter Buffer"] = "Disabled"
        if "--multithread 1" in exec_line:
            details["Multithread"] = "Enabled"
        elif "--multithread 0" in exec_line:
            details["Multithread"] = "Disabled"
        if "--use-epoll 1" in exec_line:
            details["Use Epoll"] = "Enabled"
        elif "--use-epoll 0" in exec_line:
            details["Use Epoll"] = "Disabled"
        if "--pwd " in exec_line:
            details["XOR Encryption"] = "Enabled"
        else:
            details["XOR Encryption"] = "Disabled"
        mode_match = re.search(r"--mode\s+(\d+)", exec_line)
        if mode_match:
            details["Mode"] = mode_match.group(1)
        port_match = re.search(r"--port\s+(\d+)", exec_line)
        if port_match:
            details["Port"] = port_match.group(1)
        
        print(f"\033[92mDetailed Options:{RESET}")
        for key, val in details.items():
            print(f"    • {key}: {val}")
        
        print(f"\033[94m\nLast 10 log entries for {service}:{RESET}")
        try:
            logs = subprocess.check_output(
                f"journalctl -u {service} --no-pager | tail -n 10", shell=True
            ).decode()
            print(f"\033[96m{logs}{RESET}")
        except subprocess.CalledProcessError:
            print(f"\033[91mUnable to retrieve logs for {service}.{RESET}")
    
    input(f"\n\033[97mPress Enter to return to the main menu...\033[0m")
    main_menu()



def edit_server_menu():
    service_file = "/etc/systemd/system/udp_tun_server.service"
    if not os.path.exists(service_file):
        print(f"{RED}Server service file not found. Plz deploy the server first.{RESET}")
        input(f"{YELLOW}Press Enter to return to the edit menu...{RESET}")
        return
    with open(service_file, "r") as f:
        lines = f.readlines()
    exec_index = None
    exec_line = ""
    for i, line in enumerate(lines):
        if line.startswith("ExecStart="):
            exec_index = i
            exec_line = line[len("ExecStart="):].strip()
            break
    if not exec_line:
        print(f"{RED}Unable to parse the ExecStart command.{RESET}")
        return

    def get_param_input(flag):
        pattern = re.compile(re.escape(flag) + r"\s+(\S+)")
        match = pattern.search(exec_line)
        return match.group(1) if match else ""
    
    server_params = {
        "1": {"name": "TUN Name", "flag": "--ifname", "value": get_param_input("--ifname"),
              "desc": "Network interface for TUN (e.g, azumi)"},
        "2": {"name": "TUN IP", "flag": "--ip", "value": get_param_input("--ip"),
              "desc": "TUN IP address (e.g, 50.22.22.1/24)"},
        "3": {"name": "MTU", "flag": "--mtu", "value": get_param_input("--mtu"),
              "desc": "Maximum Transmission Unit (e.g, 1250)"},
        "4": {"name": "XOR Password", "flag": "--pwd", "value": get_param_input("--pwd"),
              "desc": "XOR encryption password (leave empty to disable)"},
        "5": {"name": "Port", "flag": "--port", "value": get_param_input("--port"),
              "desc": "Port number (e.g, 8004)"},
        "6": {"name": "Mode", "flag": "--mode", "value": get_param_input("--mode"),
              "desc": "Mode: 1 to enable, 0 to disable"},
        "7": {"name": "Socket Buffer", "flag": "--sock-buf", "value": get_param_input("--sock-buf"),
              "desc": "Socket buffer size (e.g, 1024)"},
        "8": {"name": "Log Level", "flag": "--log-lvl", "value": get_param_input("--log-lvl"),
              "desc": "Log level (e.g, info)"},
        "9": {"name": "Keep Alive", "flag": "--keep-alive", "value": get_param_input("--keep-alive"),
              "desc": "Keep-alive interval (e.g, 10)"},
        "10": {"name": "Dynamic Pacing", "flag": "--dynamic-pacing", "value": get_param_input("--dynamic-pacing"),
              "desc": "1 to enable, 0 to disable"},
        "11": {"name": "Jitter Buffer", "flag": "--jitter-buffer", "value": get_param_input("--jitter-buffer"),
              "desc": "Jitter buffer value (e.g, 0)"},
        "12": {"name": "Multithread", "flag": "--multithread", "value": get_param_input("--multithread"),
              "desc": "1 to enable, 0 to disable"},
        "13": {"name": "Use Epoll", "flag": "--use-epoll", "value": get_param_input("--use-epoll"),
              "desc": "1 to enable, 0 to disable"}
    }
    binary = exec_line.split()[0]

    while True:
        clear()
        clear()
        os.system("clear")
        print("\033[92m ^ ^\033[0m")
        print("\033[92m(\033[91mO,O\033[92m)\033[0m")
        print("\033[92m(   ) \033[93mEdit UDP_TUN Server \033[92mConfiguration\033[0m")
        print(
        '\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m'
    )
        for key, param in sorted(server_params.items(), key=lambda x: int(x[0])):
            print(f"{YELLOW}{key}) {param['name']}: {GREEN}{param['value']}{RESET} [{param['desc']}]")
        print(f"{GREEN}s) Save changes")
        print(f"{RED}b) Back to previous menu{RESET}")
        choice = input(f"{CYAN}Enter option number to edit, \033[92m's' to save,\033[97m or 'b' to go back\033[93m: {RESET}").strip()
        if choice in server_params:
            current_val = server_params[choice]["value"]
            prompt = f"Enter new value for {server_params[choice]['name']} (current: {GREEN}{current_val}{RESET})"
            new_val = input(f"{YELLOW}{prompt}: {RESET}").strip()
            if new_val != "":
                server_params[choice]["value"] = new_val
        elif choice.lower() == "s":
            new_cmd = binary
            for key in sorted(server_params, key=lambda x: int(x)):
                param = server_params[key]
                if param["flag"] == "--pwd" and param["value"] == "":
                    continue
                new_cmd += f" {param['flag']} {param['value']}"
            new_exec_line = "ExecStart=" + new_cmd + "\n"
            lines[exec_index] = new_exec_line
            with open(service_file, "w") as f:
                f.writelines(lines)
            run_cmd("systemctl daemon-reload")
            run_cmd("systemctl restart udp_tun_server")
            print(f"{GREEN}Server service updated and restarted successfully.{RESET}")
            input(f"{YELLOW}Press Enter to return to the main menu...{RESET}")
            break
        elif choice.lower() == "b":
            break
        else:
            print(f"{RED}Invalid option. Please try again.{RESET}")
            input("Press Enter to continue...")

def edit_client_menu():
    service_file = "/etc/systemd/system/udp_tun_client.service"
    if not os.path.exists(service_file):
        print(f"{RED}Client service file not found. Please deploy the client first.{RESET}")
        input(f"{YELLOW}Press Enter to return to the edit menu...{RESET}")
        return
    with open(service_file, "r") as f:
        lines = f.readlines()
    exec_index = None
    exec_line = ""
    for i, line in enumerate(lines):
        if line.startswith("ExecStart="):
            exec_index = i
            exec_line = line[len("ExecStart="):].strip()
            break
    if not exec_line:
        print(f"{RED}Unable to parse the ExecStart command.{RESET}")
        return

    def obt_param_value(flag):
        pattern = re.compile(re.escape(flag) + r"\s+(\S+)")
        match = pattern.search(exec_line)
        return match.group(1) if match else ""
    
    client_params = {
        "1": {"name": "Server IP", "flag": "--server", "value": obt_param_value("--server"),
              "desc": "IP address of remote server (e.g, 206.100.101.102)"},
        "2": {"name": "TUN Name", "flag": "--ifname", "value": obt_param_value("--ifname"),
              "desc": "Network interface for TUN (e.g, azumi)"},
        "3": {"name": "TUN IP", "flag": "--ip", "value": obt_param_value("--ip"),
              "desc": "TUN IP address (e.g, 50.22.22.2/24)"},
        "4": {"name": "MTU", "flag": "--mtu", "value": obt_param_value("--mtu"),
              "desc": "Maximum Transmission Unit (e.g, 1250)"},
        "5": {"name": "Port", "flag": "--port", "value": obt_param_value("--port"),
              "desc": "Port number (e.g, 8004)"},
        "6": {"name": "Retry", "flag": "--retry", "value": obt_param_value("--retry"),
              "desc": "Number of connection retries"},
        "7": {"name": "Mode", "flag": "--mode", "value": obt_param_value("--mode"),
              "desc": "Mode: 1 to enable, 0 to disable"},
        "8": {"name": "Socket Buffer", "flag": "--sock-buf", "value": obt_param_value("--sock-buf"),
              "desc": "Socket buffer size (e.g, 2048)"},
        "9": {"name": "Log Level", "flag": "--log-lvl", "value": obt_param_value("--log-lvl"),
              "desc": "Log level (e.g, info)"},
        "10": {"name": "Keep Alive", "flag": "--keep-alive", "value": obt_param_value("--keep-alive"),
               "desc": "Keep-alive interval (e.g, 10)"},
        "11": {"name": "Dynamic Pacing", "flag": "--dynamic-pacing", "value": obt_param_value("--dynamic-pacing"),
               "desc": "1 to enable, 0 to disable"},
        "12": {"name": "Jitter Buffer", "flag": "--jitter-buffer", "value": obt_param_value("--jitter-buffer"),
               "desc": "Jitter buffer value (e.g, 0)"},
        "13": {"name": "Multithread", "flag": "--multithread", "value": obt_param_value("--multithread"),
               "desc": "1 to enable, 0 to disable"},
        "14": {"name": "Use Epoll", "flag": "--use-epoll", "value": obt_param_value("--use-epoll"),
               "desc": "1 to enable, 0 to disable"}
    }
    binary = exec_line.split()[0]

    while True:
        clear()
        os.system("clear")
        print("\033[92m ^ ^\033[0m")
        print("\033[92m(\033[91mO,O\033[92m)\033[0m")
        print("\033[92m(   ) \033[93mEdit UDP_TUN Client \033[92mConfiguration\033[0m")
        print(
        '\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m'
    )
        print("\033[93m╭───────────────────────────────────────╮\033[0m")
        for key, param in sorted(client_params.items(), key=lambda x: int(x[0])):
            print(f"{YELLOW}{key}) {param['name']}: {GREEN}{param['value']}{RESET} [{param['desc']}]")
        print(f"{GREEN}s) Save changes")
        print(f"{RED}b) Back to previous menu{RESET}")
        choice = input(f"{CYAN}Enter option number to edit, 's' to save, or 'b' to go back: {RESET}").strip()
        if choice in client_params:
            current_val = client_params[choice]["value"]
            prompt = f"Enter new value for {client_params[choice]['name']} (current: {GREEN}{current_val}{RESET})"
            new_val = input(f"{YELLOW}{prompt}: {RESET}").strip()
            if new_val != "":
                client_params[choice]["value"] = new_val
        elif choice.lower() == "s":
            new_cmd = binary
            for key in sorted(client_params, key=lambda x: int(x)):
                param = client_params[key]
                new_cmd += f" {param['flag']} {param['value']}"
            new_exec_line = "ExecStart=" + new_cmd + "\n"
            lines[exec_index] = new_exec_line
            with open(service_file, "w") as f:
                f.writelines(lines)
            run_cmd("systemctl daemon-reload")
            run_cmd("systemctl restart udp_tun_client")
            print(f"{GREEN}Client service updated and restarted successfully.{RESET}")
            input(f"{YELLOW}Press Enter to return to the main menu...{RESET}")
            break
        elif choice.lower() == "b":
            break
        else:
            print(f"{RED}Invalid option. Please try again.{RESET}")
            input("Press Enter to continue...")

def udp_edit():
    clear()
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[93mTUN \033[92mEdit Menu\033[0m")
    print('\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("1. \033[93mEdit Server Service\033[0m")
    print("2. \033[92mEdit Client Service\033[0m")
    print("0. Back to Main Menu")
    print("\033[93m╰───────────────────────────────────────╯\033[0m")
    choice = input(f"{MAGENTA}Enter your choice: {RESET}").strip()
    if choice == "1":
        edit_server_menu()
        udp_edit()
    elif choice == "2":
        edit_client_menu()
        udp_edit()
    elif choice == "0":
        main_menu()
    else:
        print(f"{RED}Invalid choice. Returning to main menu.{RESET}")
        main_menu()

def remove_reset_daemon_files():
    reset_daemon_script = "/usr/local/bin/udp_tun_reset_daemon.sh"
    reset_service_file = "/etc/systemd/system/udp_tun_reset_daemon.service"
    
    if os.path.exists(reset_daemon_script):
        try:
            run_cmd(f"rm -f {reset_daemon_script}")
            print(f"{GREEN}Reset daemon script removed successfully.{RESET}")
        except Exception as e:
            print(f"{RED}Failed to remove reset daemon script: {e}{RESET}")
    else:
        print(f"{YELLOW}Reset daemon script not found.{RESET}")
    
    if os.path.exists(reset_service_file):
        try:
            run_cmd(f"rm -f {reset_service_file}")
            print(f"{GREEN}Reset daemon service file removed successfully.{RESET}")
        except Exception as e:
            print(f"{RED}removing reset daemon service file failed: {e}{RESET}")
    else:
        print(f"{YELLOW}Reset daemon service file not found.{RESET}")


def uninstall_server():
    os.system("clear")
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[93mUninstall \033[93mMenu\033[0m")
    print('\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{BLUE}Uninstall UDP_TUN Server Service{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    service_file = "/etc/systemd/system/udp_tun_server.service"
    if not os.path.exists(service_file):
        print(f"{RED}UDP_TUN Server Service is not installed.{RESET}")
    else:
        choice = input(f"\033[93mDo you want to \033[92mstop and uninstall\033[96m the UDP_TUN Server Service\033[93m? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
        if choice in ["y", "yes"]:
            run_cmd("systemctl stop udp_tun_server")
            run_cmd("systemctl disable udp_tun_server")
            os.remove(service_file)
            print(f"{GREEN}UDP_TUN Server Service uninstalled successfully.{RESET}")
        else:
            print(f"{YELLOW}Skipping server service uninstall.{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    choice_dir = input(f"\033[93mDo you want to remove \033[92mthe clone directory \033[97m(/usr/local/bin/udp_tun)\033[93m as well? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
    if choice_dir in ["y", "yes"]:
        try:
            run_cmd("rm -rf /usr/local/bin/udp_tun")
            print(f"{GREEN}Clone directory removed successfully.{RESET}")
        except Exception as e:
            print(f"{RED}Failed to remove clone directory: {e}{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    choice_reset = input(f"\033[93mDo you want to \033[92mremove reset daemon files\033[93m as well? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
    if choice_reset in ["y", "yes"]:
        remove_reset_daemon_files()
    input(f"\n\033[97mPress Enter to return to the main menu...{RESET}")
    main_menu()


def uninstall_client():
    os.system("clear")
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[93mUninstall \033[93mMenu\033[0m")
    print('\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{BLUE}Uninstall UDP_TUN Client Service{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    service_file = "/etc/systemd/system/udp_tun_client.service"
    if not os.path.exists(service_file):
        print(f"{RED}UDP_TUN Client Service is not installed.{RESET}")
    else:
        choice = input(f"\033[93mDo you want to \033[92mstop and uninstall\033[93m the \033[96mUDP_TUN Client Service\033[93m? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
        if choice in ["y", "yes"]:
            run_cmd("systemctl stop udp_tun_client")
            run_cmd("systemctl disable udp_tun_client")
            os.remove(service_file)
            print(f"{GREEN}UDP_TUN Client Service uninstalled successfully.{RESET}")
        else:
            print(f"{YELLOW}Skipping client service uninstall.{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    choice_dir = input(f"\033[93mDo you want to remove \033[92mthe clone directory\033[97m (/usr/local/bin/udp_tun) \033[93mas well? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
    if choice_dir in ["y", "yes"]:
        try:
            run_cmd("rm -rf /usr/local/bin/udp_tun")
            print(f"{GREEN}Clone directory removed successfully.{RESET}")
        except Exception as e:
            print(f"{RED}Failed to remove clone directory: {e}{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    choice_reset = input(f"\033[93mDo you want to \033[92mremove reset daemon files\033[93m as well? (\033[92my\033[93m/\033[91mn\033[93m): {RESET}").strip().lower()
    if choice_reset in ["y", "yes"]:
        remove_reset_daemon_files()
    input(f"\n\033[97mPress Enter to return to the main menu...{RESET}")
    main_menu()


def udp_uninstall():
    os.system("clear")
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[92mUDP TUN Uninstall \033[93mMenu\033[0m")
    print(
        '\033[92m "-"\033[93m═══════════════════════════════════════════════════\033[0m'
    )
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("1.\033[93m Uninstall Server Service\033[0m")
    print("2.\033[92m Uninstall Client Service\033[0m")
    print("0. Back to Main Menu")
    print("\033[93m╰───────────────────────────────────────╯\033[0m")
    choice = input(f"{YELLOW}Enter your choice: {RESET}").strip()
    if choice == "1":
        uninstall_server()
    elif choice == "2":
        uninstall_client()
    elif choice == "0":
        main_menu()
    else:
        print(f"{RED}Invalid choice. Returning to main menu.{RESET}")
        main_menu()

def enable_udp_tun_reset_daemon():
    print("\033[93m───────────────────────────────────────\033[0m")
    print("\033[93mUDP TUN Reset Daemon Configuration\033[0m")
    print("\033[93m───────────────────────────────────────\033[0m")
    
    choice = input("\033[93mDo you want to \033[92medit the\033[96m Reset Timer\033[93m? (\033[92myes\033[93m/\033[91mno\033[93m): \033[0m").lower()
    if choice in ['yes', 'y']:
        print("\033[93m╭───────────────────────────────────────╮\033[0m")
        print("1. \033[92mHour\033[0m")
        print("2. \033[93mMinute\033[0m")
        print("\033[93m╰───────────────────────────────────────╮\033[0m")
        
        unit_choice = input("\033[93mEnter your choice: \033[0m").strip()
        if unit_choice == '1':
            unit = 'hour'
        elif unit_choice == '2':
            unit = 'minute'
        else:
            print("\033[91mInvalid choice.\033[0m")
            return
        
        time_value = input(f"\033[93mEnter the\033[96m desired time value\033[92m in {unit}(s)\033[93m: \033[0m").strip()
        try:
            time_int = int(time_value)
        except ValueError:
            print("\033[91mWrong numeric value. Please enter an integer.\033[0m")
            return
        
        interval_seconds = time_int * 3600 if unit == 'hour' else time_int * 60
        
        daemon_script = "/usr/local/bin/udp_tun_reset_daemon.sh"
        daemon_content = f"""#!/bin/bash
INTERVAL={interval_seconds}
while true; do
    systemctl restart udp_tun_server udp_tun_client
    sleep $INTERVAL
done
"""
        with open(daemon_script, "w") as f:
            f.write(daemon_content)
        subprocess.run(["chmod", "+x", daemon_script])
        print(f"\033[92mDaemon script created at {daemon_script} with an interval of {interval_seconds} seconds.\033[0m")
        
        service_file = "/etc/systemd/system/udp_tun_reset_daemon.service"
        service_content = f"""[Unit]
Description=UDP_TUN Reset Daemon
After=network.target

[Service]
ExecStart={daemon_script}
Restart=always

[Install]
WantedBy=multi-user.target
"""
        with open(service_file, "w") as f:
            f.write(service_content)
        
        subprocess.run(["systemctl", "daemon-reload"])
        subprocess.run(["systemctl", "enable", "udp_tun_reset_daemon.service"])
        subprocess.run(["systemctl", "restart", "udp_tun_reset_daemon.service"])
        print(f"\033[92mUDP_TUN Reset Daemon service enabled and restarted successfully.\033[0m")
    else:
        print("\033[93mSkipping UDP_TUN Reset Timer\033[0m")
    input(f"\n\033[97mPress Enter to return to the main menu...{RESET}")
    main_menu() 

def main_menu():
    clear()
    logo()
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[96mUDP TUN\033[93m Menu\033[0m")
    print('\033[92m "-"\033[93m══════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("\033[93mChoose what to do:\033[0m")
    print("1. \033[97mStatus\033[0m")
    print("2. \033[92mUDP TUN\033[0m")
    print("3. \033[93mEdit TUN\033[0m")
    print("4. \033[96mReset Timer\033[0m")
    print("5. \033[91mUninstall\033[0m")
    print("0. \033[94mExit\033[0m")
    print("\033[93m╰───────────────────────────────────────╯\033[0m")
    
    while True:
        choice = input("\033[38;5;205mEnter your choice Please: \033[0m").strip()
        if choice == "1":
            udp_status()
            break
        elif choice == "2":
            udp_tun()
            break
        elif choice == "3":
            clear()
            udp_edit()
            break
        elif choice == "4":
            enable_udp_tun_reset_daemon()
            break
        elif choice == "5":
            clear()
            udp_uninstall()
            break
        elif choice == "0":
            sys.exit(0)
        else:
            print(f"{RED}Invalid choice. Please try again.{RESET}")

def udp_tun():
    clear()
    print("\033[92m ^ ^\033[0m")
    print("\033[92m(\033[91mO,O\033[92m)\033[0m")
    print("\033[92m(   ) \033[93mUDP Tun\033[92m Config Menu\033[0m")
    print('\033[92m "-"\033[93m═════════════════════════════════════════\033[0m')
    print("\033[93m╭───────────────────────────────────────╮\033[0m")
    print("1. \033[93mServer \033[0m")
    print("2. \033[92mClient \033[0m")
    print("0. \033[97mBack to main menu\033[0m")
    print("\033[93m╰───────────────────────────────────────╯\033[0m")
    service_choice = input(f"Enter your choice (0-2): {RESET}").strip()
    
    dest_dir = "/usr/local/bin/udp_tun"
    repo_url = "https://github.com/Azumi67/udp_tun.git"
    print("\033[93m───────────────────────────────────────\033[0m")
    print(f"{GREEN}--- Repository & Compilation ---{RESET}")
    print("\033[93m───────────────────────────────────────\033[0m")
    install_prestuff()
    clone_repository(repo_url, dest_dir)
    os.chdir(dest_dir)
    
    if service_choice == "1":
        # Server
        compile_source("server.cpp", "server", dest_dir)
        server_cmd_args, tun_ip = ask_server_config()
        server_exec = f"{dest_dir}/server"
        full_server_cmd = server_exec + " " + server_cmd_args.split(" ", 1)[1]
        service_name = "udp_tun_server"
        description = "UDP_TUN Server Service"
        print("\033[93m───────────────────────────────────────\033[0m")
        print(f"\n{GREEN}--- Creating Server Service ---{RESET}")
        print("\033[93m───────────────────────────────────────\033[0m")
        create_service_file(service_name, full_server_cmd, description, dest_dir)
        display_box(f"Server TUN IP: {tun_ip}")
    elif service_choice == "2":
        # Client
        compile_source("client.cpp", "client", dest_dir)
        client_cmd_args, tun_ip = ask_client_config()
        client_exec = f"{dest_dir}/client"
        full_client_cmd = client_exec + " " + client_cmd_args.split(" ", 1)[1]
        service_name = "udp_tun_client"
        description = "UDP_TUN Client Service"
        print("\033[93m───────────────────────────────────────\033[0m")
        print(f"\n{GREEN}--- Creating Client Service ---{RESET}")
        print("\033[93m───────────────────────────────────────\033[0m")
        create_service_file(service_name, full_client_cmd, description, dest_dir)
        display_box(f"Client TUN IP: {tun_ip}")
    elif service_choice == "0":
        main_menu()
    else:
        print(f"{RED}Wrong input. Exiting.{RESET}")
        sys.exit(1)
        
main_menu()
