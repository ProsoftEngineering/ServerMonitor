#include <chrono>
#include <future>
#include <iostream>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

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
        extern bool UrlDownload(const std::string& url, unsigned timeout, std::string& errorMessage);
        return UrlDownload(url_, timeout_, errorMessage_);
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
    Server(const std::string& name, const std::shared_ptr<Monitor>& monitor)
        : name_(name)
        , monitor_(monitor)
    {
    }

    const std::string& name() const {
        return name_;
    }
    
    const std::shared_ptr<Monitor>& monitor() const {
        return monitor_;
    }
    
    void setResult(bool result) {
        result_ = result;
    }
    
    bool result() const {
        return result_;
    }
private:
    const std::string name_;
    const std::shared_ptr<Monitor> monitor_;
    bool result_;
};

class ServerMonitor {
public:
    
    bool run() {
        const unsigned timeout = 5;
        
        std::vector<Server> servers;
        servers.emplace_back("Apple Website", std::make_shared<WebsiteMonitor>("http://www.apple.com", timeout));
        servers.emplace_back("Apple Secure Website", std::make_shared<WebsiteMonitor>("https://www.apple.com", timeout));
        servers.emplace_back("Apple SSL", std::make_shared<ServiceMonitor>("apple.com", 443, timeout));
        
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
            auto monitor = server.monitor();
            printf("%s: %d: %s (%u ms)\n", server.name().c_str(), server.result(), monitor->errorMessage().c_str(), monitor->duration());
        }
        
        printf("Total time: %u ms\n", elapsedTime.duration());
        
        return true;
    }
    
};

int main() {
    ServerMonitor mon;
    return mon.run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
