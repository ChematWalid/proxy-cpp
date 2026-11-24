#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <csignal>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <netdb.h>

enum class RequestType {
    GET,
    POST,
    PUT,
    DELETE,
    UNKNOWN
};

struct Request {
    bool valid = false;
    RequestType type = RequestType::UNKNOWN;
    std::string method_str;
    std::string raw_url;
    std::string version;
    std::string host;
    int port = 80;
    std::string path = "/";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string get_header(std::string key) const {
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto it = headers.find(key);
        return (it != headers.end()) ? it->second : "";
    }
};

// Function to parse the request line (e.g., "GET /path HTTP/1.1")
bool parse_request_line(const std::string& line, Request& req) {
    std::stringstream ss(line);
    if (!(ss >> req.method_str >> req.raw_url >> req.version)) {
        return false;
    }

    if (req.method_str == "GET") req.type = RequestType::GET;
    else if (req.method_str == "POST") req.type = RequestType::POST;
    else if (req.method_str == "PUT") req.type = RequestType::PUT;
    else if (req.method_str == "DELETE") req.type = RequestType::DELETE;
    else req.type = RequestType::UNKNOWN;

    return true;
}

// Function to parse headers from the request
void parse_headers(const std::string& header_block, Request& req) {
    std::stringstream ss(header_block);
    std::string line;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);

            size_t first = val.find_first_not_of(" \t");
            if (first != std::string::npos) val = val.substr(first);

            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            req.headers[key] = val;
        }
    }
}

// Function to parse host and port from a string like "example.com:8080" to "example.com" and 8080
void parse_host_port(const std::string& host_port, std::string& host, int& port) {
    size_t colon = host_port.find(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        try {
            port = std::stoi(host_port.substr(colon + 1));
        } catch (...) {
            port = 80;
        }
    } else {
        host = host_port;
        port = 80;
    }
}

// Function to parse the target from the request (e.g., "http://example.com/path" or "/path")
void parse_target(Request& req) {
    std::string target = req.raw_url;
    std::string host_port;

    if (target.rfind("http://", 0) == 0) {
        target = target.substr(7);
        size_t slash = target.find('/');
        host_port = (slash == std::string::npos) ? target : target.substr(0, slash);
        req.path = (slash == std::string::npos) ? "/" : target.substr(slash);
    } else {
        req.path = target.empty() ? "/" : target;
        host_port = req.get_header("host");
    }

    parse_host_port(host_port, req.host, req.port);
}

// Function to parse the entire HTTP request from a raw string
Request parse_request(const std::string& raw_req) {
    Request req;

    size_t header_end = raw_req.find("\r\n\r\n");
    if (header_end == std::string::npos) return req;

    size_t first_line_end = raw_req.find("\r\n");
    if (first_line_end == std::string::npos || first_line_end > header_end) return req;

    if (!parse_request_line(raw_req.substr(0, first_line_end), req)) {
        return req;
    }

    parse_headers(raw_req.substr(first_line_end + 2, header_end - (first_line_end + 2)), req);
    parse_target(req);

    if (header_end + 4 < raw_req.size()) {
        req.body = raw_req.substr(header_end + 4);
    }

    req.valid = !req.host.empty();
    return req;
}

//From a Request struct to a string representation of the HTTP request
std::string request_to_string(const Request& req) {
    std::stringstream ss;
    ss << req.method_str << " " << req.path << " " << req.version << "\r\n";
    std::string host_header = req.host;
    if (req.port != 80) {
        host_header += ":" + std::to_string(req.port);
    }
    ss << "host: " << host_header << "\r\n";
    ss << "connection: close\r\n";
    for (const auto& [key, val] : req.headers) {
        if (key == "host" || key == "connection" || key == "proxy-connection") {
            continue;
        }
        ss << key << ": " << val << "\r\n";
    }
    ss << "\r\n";
    ss << req.body;
    return ss.str();
}

//send all data to a socket that is possible then send the bytes after it etc until all bytes are sent
bool send_all(int fd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, data + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}


//return a socket connected to the host and port specified, or -1 on failure
int connect_to_host(const std::string& host, int port) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return -1;
    }

    int remote_fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        remote_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (remote_fd < 0) continue;
        if (connect(remote_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(remote_fd);
        remote_fd = -1;
    }

    freeaddrinfo(res);
    return remote_fd;
}

// Create a listening socket on the specified port
int make_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(fd, 128) < 0) { perror("listen"); exit(1); }
    return fd;
}

//handle client thread (parse request , connect to remote host , forward request , forward response)
void handle_client(int client_fd) {
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, n);
    }

    Request parsed = parse_request(req);
    if (!parsed.valid) {
        const char* err = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        return;
    }

    int remote_fd = connect_to_host(parsed.host, parsed.port);
    if (remote_fd < 0) {
        const char* err = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        return;
    }

    std::string forward_req = request_to_string(parsed);
    if (send_all(remote_fd, forward_req.data(), forward_req.size())) {
        while (true) {
            ssize_t n = recv(remote_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            if (!send_all(client_fd, buf, n)) break;
        }
    }
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    if (getpeername(client_fd, (sockaddr*)&client_addr, &client_len) == 0) {
        std::cout << "Client Treated : Client ip " << inet_ntoa(client_addr.sin_addr) << " requested " << parsed.method_str << " " << parsed.raw_url << "\n";
    }
    close(remote_fd);
    close(client_fd);
}

int main(int argc, char** argv) {
    // ignore sigpipe (ki client ydeconnecti , y7bs el proxy bs7 bhadi my7bssh)
    std::signal(SIGPIPE, SIG_IGN);
    int port = argc > 1 ? std::atoi(argv[1]) : 8888;
    int listener = make_listener(port);
    std::cout << "Proxy listening on port " << port << "\n";

    while (true) {
        int client = accept(listener, nullptr, nullptr);
        if (client < 0) { perror("accept"); continue; }
        //concurrent proxy threads
        std::thread(handle_client, client).detach();
    }
}
