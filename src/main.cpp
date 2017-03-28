#include <chrono>
#include <future>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include "json.hpp"
#include "process.hpp"

#include "curl.hpp"

namespace {

    using json = nlohmann::json;

    static const TimeoutType kDefaultTimeout = 5;

    json read_json_file(const std::string& path, bool required) {
        std::ifstream filestream(path);
        json data;
        if (!filestream.is_open()) {
            if (required) {
                throw std::runtime_error("Can't open json file");
            }
            return data;
        }
        filestream >> data;
        return data;
    }

    template <typename StringType>
    StringType trim(const StringType& s) {
        auto it = s.begin();
        while (it != s.end() && isspace(*it)) {
            it++;
        }
        auto rit = s.rbegin();
        while (rit.base() != it && isspace(*rit)) {
            rit++;
        }
        return {it, rit.base()};
    }

    class Task {
    public:
        Task(const std::string& command)
            : cmd_(command)
        {
        }
        
        int run() {
            const auto read_stdout = [this](const char *bytes, size_t n) {
                stdout_.append(bytes, n);
            };
            const auto read_stderr = [this](const char *bytes, size_t n) {
                stderr_.append(bytes, n);
            };
            Process process(cmd_, {}, read_stdout, read_stderr);
            return process.get_exit_status();
        }
        
        const std::string& out() const {
            return stdout_;
        }
        
        const std::string err() const {
            return stderr_;
        }
        
    private:
        const std::string cmd_;
        std::string stdout_;
        std::string stderr_;
    };
}

class ElapsedTime {
public:
    using ClockType = std::chrono::high_resolution_clock;
    
    void start() {
        start_ = ClockType::now();
    }

    void stop() {
        const auto end = ClockType::now();
        duration_ = static_cast<DurationType>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count());
    }

    DurationType duration() const {
        return duration_;
    }
private:
    ClockType::time_point start_;
    DurationType duration_;
};

class Monitor {
public:
    Monitor(TimeoutType timeout)
        : timeout_(timeout)
    {
    }
    
    bool run() {
        elapsedTime_.start();
        const bool result = execute();
        elapsedTime_.stop();
        return result;
    }
    
    TimeoutType timeout() const {
        return timeout_;
    }

    const std::string& errorMessage() const {
        return errorMessage_;
    }
    
    DurationType duration() const {
        return elapsedTime_.duration();
    }
    
protected:
    virtual bool execute() = 0;
    
    std::string errorMessage_;
    
private:
    const TimeoutType timeout_;
    ElapsedTime elapsedTime_;
};

class WebsiteMonitor : public Monitor {
public:
    WebsiteMonitor(const std::string& url, TimeoutType timeout, bool verifypeer)
        : Monitor(timeout)
    {
        params_.url = url;
        params_.verifypeer = verifypeer;
    }
    
    virtual bool execute() override {
        params_.timeout = timeout();
        return HttpHead(params_, errorMessage_);
    }
    
private:
    HttpParams params_;
};

class ServiceMonitor : public Monitor {
public:
    ServiceMonitor(const std::string& host, PortType port, TimeoutType timeout)
        : Monitor(timeout)
        , host_(host)
        , port_(std::to_string(port))
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
        tv.tv_sec = timeout();
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
};

class CommandMonitor : public Monitor {
public:
    CommandMonitor(TimeoutType timeout)
        : Monitor(timeout)
    {
    }
    
    CommandMonitor(const std::string& command, TimeoutType timeout)
        : Monitor(timeout)
        , command_(command)
    {
    }

    void setCommand(const std::string& command) {
        command_ = command;
    }
    
    virtual bool execute() override {
        Task task{command_};
        const int status = task.run();
        if (status != 0) {
            const auto& stdout_str = task.out();
            const auto& stderr_str = task.err();
            const auto output = trim(stdout_str + stderr_str);
            if (!output.empty()) {
                errorMessage_ = output;
            } else {
                errorMessage_ = "command failed with exit code " + std::to_string(status);
            }
            return false;
        }
        return true;
    }
    
private:
    std::string command_;
};


class PingMonitor : public CommandMonitor {
public:
    PingMonitor(const std::string& host, TimeoutType timeout)
        : CommandMonitor(timeout)
    {
        setCommand("ping -t " + std::to_string(timeout) + " -c 1 \"" + host + "\"");
    }
};

class Server {
public:
    using MonitorPtr = std::unique_ptr<Monitor>;
    
    Server(const std::string& name, MonitorPtr monitor, const std::string& action)
        : name_(name)
        , monitor_(std::move(monitor))
        , action_(action)
        , result_(false)
    {
    }

    const std::string& name() const {
        return name_;
    }
    
    const MonitorPtr& monitor() const {
        return monitor_;
    }
    
    const std::string& action() const {
        return action_;
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
    std::string action_;
    bool result_;
};

std::string replace_variables(const std::string& input, const Server& server) {
    std::string str{input};
    const std::unordered_map<std::string, std::string> map{
        {"name", server.name()},
        {"status", server.result() ? "up" : "down"},
        {"Status", server.result() ? "Up" : "Down"},
        {"STATUS", server.result() ? "UP" : "DOWN"},
    };
    for (const auto& item : map) {
        std::string what = "{{" + item.first + "}}";
        std::string::size_type pos;
        while ((pos = str.find(what)) != std::string::npos) {
            str.replace(pos, what.size(), item.second);
        }
    }
    return str;
}

class Action {
public:
    Action(TimeoutType timeout)
        : timeout_(timeout)
    {
    }
    TimeoutType timeout() const {
        return timeout_;
    }
    virtual void run(const Server& server) = 0;
private:
    TimeoutType timeout_;
};

class CommandAction : public Action {
public:
    CommandAction(TimeoutType timeout, const std::string& command)
        : Action(timeout)
        , cmd_(command)
    {
    }
    
    virtual void run(const Server& server) override {
        Task task{replace_variables(cmd_, server)};
        (void)task.run();
    }
    
private:
    const std::string cmd_;
};

class EmailAction : public Action {
public:
    EmailAction(TimeoutType timeout, const EmailParams& params)
        : Action(timeout)
        , params_(params)
    {
    }
    
    virtual void run(const Server& server) override {
        EmailParams params = params_;
        params.subject = replace_variables(params.subject, server);
        std::string errorMessage;
        (void)Email(params, timeout(), errorMessage);
    }

private:
    const EmailParams params_;
};

class ServerMonitor {
public:
    ServerMonitor(const json& config, const std::string& status_path)
        : config_(config)
        , status_path_(status_path)
    {
    }
    
    void run() {
        const json status_prev{read_json_file(status_path_, false)};
        
        const auto config_end = config_.end();
        
        TimeoutType global_timeout = kDefaultTimeout;
        const auto global_timeout_iter = config_.find("timeout");
        if (global_timeout_iter != config_end) {
            global_timeout = global_timeout_iter->get<TimeoutType>();
        }
        
        using ActionPtr = std::unique_ptr<Action>;
        std::unordered_map<std::string, ActionPtr> actions;
        const auto actions_iter = config_.find("actions");
        if (actions_iter != config_end && actions_iter->is_object()) {
            for (auto it = (*actions_iter).cbegin(); it != (*actions_iter).cend(); ++it) {
                const auto& value = it.value();
                if (value.is_object()) {
                    const std::string name = it.key();
                    
                    const auto end = value.end();
                    
                    const auto cmd_iter = value.find("cmd");
                    if (cmd_iter != end) {
                        actions[name] = std::make_unique<CommandAction>(global_timeout, cmd_iter.value().get<std::string>());
                        continue;
                    }
                    
                    const auto smtp_host_iter = value.find("smtp_host");
                    const auto smtp_user_iter = value.find("smtp_user");
                    const auto smtp_password_iter = value.find("smtp_password");
                    const auto from_iter = value.find("from");
                    const auto to_iter = value.find("to");
                    const auto subject_iter = value.find("subject");
                    const auto body_iter = value.find("body");
                    if (smtp_host_iter != end &&
                        smtp_user_iter != end &&
                        smtp_password_iter != end &&
                        from_iter != end &&
                        to_iter != end &&
                        subject_iter != end &&
                        body_iter != end) {
                        EmailParams params;
                        params.smtp_host = smtp_host_iter.value().get<std::string>();
                        params.smtp_user = smtp_user_iter.value().get<std::string>();
                        params.smtp_password = smtp_password_iter.value().get<std::string>();
                        params.from = from_iter.value().get<std::string>();
                        params.to = to_iter.value().get<std::string>();
                        params.subject = subject_iter.value().get<std::string>();
                        params.body = body_iter.value().get<std::string>();
                        actions[name] = std::make_unique<EmailAction>(global_timeout, params);
                        continue;
                    }
                    
                    throw std::runtime_error("Invalid action entry");
                }
            }
        }
        
        bool global_verifypeer = true;
        const auto global_verifypeer_iter = config_.find("verifypeer");
        if (global_verifypeer_iter != config_end) {
            global_verifypeer = global_verifypeer_iter->get<bool>();
        }
        
        const auto serversiter = config_.find("servers");
        if (serversiter == config_end) {
            throw std::runtime_error("Missing \"servers\" field");
        }

        std::vector<Server> servers;
        std::unordered_set<std::string> names;

        for (const auto& server : *serversiter) {
            const auto end = server.end();

            const auto name_iter = server.find("name");
            if (name_iter == end) {
                throw std::runtime_error("Missing required \"name\" field");
            }
            
            const auto name = name_iter->get<std::string>();
            if (names.find(name) != names.end()) {
                throw std::runtime_error("Name \"" + name + "\" is already used");
            }
            names.insert(name);
            
            TimeoutType timeout = global_timeout;
            const auto timeout_iter = server.find("timeout");
            if (timeout_iter != end) {
                timeout = timeout_iter->get<TimeoutType>();
            }
            
            bool verifypeer = global_verifypeer;
            const auto verifypeer_iter = server.find("verifypeer");
            if (verifypeer_iter != end) {
                verifypeer = verifypeer_iter->get<bool>();
            }

            std::string action;
            const auto action_iter = server.find("action");
            if (action_iter != end) {
                action = action_iter->get<std::string>();
                if (actions.find(action) == actions.end()) {
                    throw std::runtime_error("Unknown action \"" + action + "\"");
                }
            }

            const auto url = server.find("url");
            if (url != end) {
                servers.emplace_back(name, std::make_unique<WebsiteMonitor>(url->get<std::string>(), timeout, verifypeer), action);
                continue;
            }
            
            const auto host = server.find("host");
            const auto port = server.find("port");
            if (host != end && port != end) {
                servers.emplace_back(name, std::make_unique<ServiceMonitor>(host->get<std::string>(), port->get<PortType>(), timeout), action);
                continue;
            }
            
            const auto ping_host = server.find("ping");
            if (ping_host != end) {
                servers.emplace_back(name, std::make_unique<PingMonitor>(ping_host->get<std::string>(), timeout), action);
                continue;
            }
            
            const auto cmd = server.find("cmd");
            if (cmd != end) {
                servers.emplace_back(name, std::make_unique<CommandMonitor>(cmd->get<std::string>(), timeout), action);
                continue;
            }
            
            throw std::runtime_error("Invalid server entry for \"" + name + "\"");
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

        json status;
        
        for (const auto& server : servers) {
            const auto& monitor = server.monitor();
            const auto name = server.name();
            const bool result = server.result();
            if (result) {
                std::cout << name << ": UP";
            } else {
                std::cout << name << ": DOWN - " << monitor->errorMessage();
            }
            std::cout << " (" << monitor->duration() << " ms)" << std::endl;
            json server_info;
            server_info["status"] = result;
            if (!result) {
                server_info["error"] = monitor->errorMessage();
            }
            status[name] = server_info;
            
            const auto status_prev_it = status_prev.find(name);
            if (status_prev_it != status_prev.end() && status_prev_it->is_boolean() && status_prev_it->get<bool>() != result) {
                std::cout << "  Handle " << (result ? "UP" : "DOWN") << std::endl;
                if (!server.action().empty()) {
                    const auto action_iter = actions.find(server.action());
                    if (action_iter != actions.end()) {
                        action_iter->second->run(server);
                    }
                }
            }
        }
        
        std::cout << "Total time: " << elapsedTime.duration() << " ms" << std::endl;
        
        std::ofstream output_file{status_path_};
        if (!output_file.is_open()) {
            throw std::runtime_error("Can't open status file");
        }
        output_file << status.dump(4) << std::endl;
    }
    
private:
    const json config_;
    const std::string status_path_;
};

int main(int argc, const char * argv[]) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("Usage: ServerMonitor <input_config.json> <output_status.json>");
        }
        
        const std::string config_path{argv[1]};
        const std::string status_path{argv[2]};

        const json config{read_json_file(config_path, true)};

        ServerMonitor mon(config, status_path);
        mon.run();

        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cout << "ERROR: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
