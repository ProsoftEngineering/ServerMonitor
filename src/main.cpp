#include <chrono>
#include <future>
#include <iostream>
#include <fstream>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include "json.hpp"

using json = nlohmann::json;

class ElapsedTime {
public:
    void start() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        const auto end = std::chrono::high_resolution_clock::now();
        duration_ = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count());
    }

    unsigned duration() const {
        return duration_;
    }
private:
    std::chrono::high_resolution_clock::time_point start_;
    unsigned duration_;
};

class Monitor {
public:
    bool run() {
        elapsedTime_.start();
        const bool result = execute();
        elapsedTime_.stop();
        return result;
    }

    const std::string& errorMessage() const {
        return errorMessage_;
    }
    
    unsigned duration() const {
        return elapsedTime_.duration();
    }
    
protected:
    virtual bool execute() = 0;
    
    std::string errorMessage_;
    
private:
    ElapsedTime elapsedTime_;
};

class WebsiteMonitor : public Monitor {
public:
    WebsiteMonitor(const std::string& url, unsigned timeout)
        : url_(url)
        , timeout_(timeout)
    {
    }
    
    virtual bool execute() override {
        extern bool HttpHead(const std::string& url, unsigned timeout, std::string& errorMessage);
        return HttpHead(url_, timeout_, errorMessage_);
    }
    
private:
    const std::string url_;
    const unsigned timeout_;
};

class ServiceMonitor : public Monitor {
public:
    ServiceMonitor(const std::string& host, unsigned port, unsigned timeout)
        : host_(host)
        , port_(std::to_string(port))
        , timeout_(timeout)
    {
    }
    
    virtual bool execute() override {
        struct ::addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        struct ::addrinfo *res = nullptr;
        const int getaddrinfo_error = ::getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res);
        if (getaddrinfo_error != 0) {
            errorMessage_ = "Can't get address: " + std::string(::gai_strerror(getaddrinfo_error));
            return false;
        }
        
        struct Socket {
            int value = -1;
            ~Socket() {
                if (value > 0) {
                    (void)::close(value);
                }
            }
        };
        
        Socket socket;
        socket.value = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (socket.value < 0) {
            errorMessage_ = "Can't create socket: " + std::string(::strerror(errno));
            return false;
        }
        
        if (::fcntl(socket.value, F_SETFL, O_NONBLOCK) != 0) {
            errorMessage_ = "Can't set to non-blocking: " + std::string(::strerror(errno));
            return false;
        }
        
        if (::connect(socket.value, res->ai_addr, res->ai_addrlen) == 0) {
            return true;
        }
        
        if (errno != EINPROGRESS) {
            errorMessage_ = "Can't connect: " + std::string(::strerror(errno));
            return false;
        }
        
        ::fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(socket.value, &rfds);
        
        struct ::timeval tv;
        tv.tv_sec = timeout_;
        tv.tv_usec = 0;
        
        const int retval = ::select(socket.value+1, nullptr, &rfds, nullptr, &tv);
        if (retval == -1) {
            errorMessage_ = "Select failed: " + std::string(::strerror(errno));
            return false;
        } else if (retval) {
            return FD_ISSET(socket.value, &rfds) != 0;
        }
        
        errorMessage_ = "Timed out";
        return false;
    }

private:
    const std::string host_;
    const std::string port_;
    const unsigned timeout_;
};

class Server {
public:
    using MonitorPtr = std::unique_ptr<Monitor>;
    
    Server(const std::string& name, MonitorPtr monitor)
        : name_(name)
        , monitor_(std::move(monitor))
    {
    }

    const std::string& name() const {
        return name_;
    }
    
    const MonitorPtr& monitor() const {
        return monitor_;
    }
    
    void setResult(bool result) {
        result_ = result;
    }
    
    bool result() const {
        return result_;
    }
    
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    
    Server(Server&& other) = default;
    Server& operator=(Server&&) = default;

private:
    std::string name_;
    MonitorPtr monitor_;
    bool result_;
};

class ServerMonitor {
public:
    ServerMonitor(const json& config)
        : config_(config)
    {
    }
    
    void run() {
        const auto config_end = config_.end();
        
        unsigned global_timeout = 5;
        const auto global_timeout_iter = config_.find("timeout");
        if (global_timeout_iter != config_end) {
            global_timeout = global_timeout_iter->get<unsigned>();
        }
        
        std::vector<Server> servers;
        const auto serversiter = config_.find("servers");
        if (serversiter == config_end) {
            throw std::runtime_error("Missing \"servers\" field");
        }

        for (const json& server : *serversiter) {
            const auto end = server.end();

            const auto name_iter = server.find("name");
            if (name_iter == end) {
                throw std::runtime_error("Missing required \"name\" field");
            }
            
            const auto name = name_iter->get<std::string>();
            
            unsigned timeout = global_timeout;
            const auto timeout_iter = server.find("timeout");
            if (timeout_iter != end) {
                timeout = timeout_iter->get<unsigned>();
            }
            
            const auto url = server.find("url");
            if (url != end) {
                servers.emplace_back(name, std::make_unique<WebsiteMonitor>(url->get<std::string>(), timeout));
                continue;
            }
            
            const auto host = server.find("host");
            const auto port = server.find("port");
            if (host != end && port != end) {
                servers.emplace_back(name, std::make_unique<ServiceMonitor>(host->get<std::string>(), port->get<unsigned>(), timeout));
                continue;
            }
            
            throw std::runtime_error("Invalid entry - either \"url\" or both \"host\" and \"port\" must be specified");
        }
        
        std::vector<std::future<void>> futures;

        ElapsedTime elapsedTime;
        
        elapsedTime.start();
        
        for (auto& server : servers) {
            futures.push_back(std::async(std::launch::async, [&server](){
                server.setResult(server.monitor()->run());
            }));
        }
        
        for (auto& future : futures) {
            future.get();
        }
        
        elapsedTime.stop();

        for (const auto& server : servers) {
            const auto& monitor = server.monitor();
            if (server.result()) {
                std::cout << server.name() << ": UP";
            } else {
                std::cout << server.name() << ": DOWN - " << monitor->errorMessage();
            }
            std::cout << " (" << monitor->duration() << " ms)" << std::endl;
        }
        
        std::cout << "Total time: " << elapsedTime.duration() << " ms" << std::endl;
    }
    
private:
    const json config_;
};

int main(int argc, const char * argv[]) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("Missing config arg");
        }

        std::ifstream filestream(argv[1]);
        if (!filestream.is_open()) {
            throw std::runtime_error("Can't open config file");
        }

        json config;
        filestream >> config;

        ServerMonitor mon(config);
        mon.run();

        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cout << "ERROR: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
