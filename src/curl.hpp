#pragma once

#include "types.hpp"
#include <string>

struct CurlGlobal {
    CurlGlobal();
    ~CurlGlobal();

    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
    
    CurlGlobal(CurlGlobal&& other) = delete;
    CurlGlobal& operator=(CurlGlobal&&) = delete;
};

struct HttpParams {
    std::string url;
    int status; // expected HTTP status, usually 200
    TimeoutType timeout;
    bool verifypeer;
};

bool HttpHead(const HttpParams& params, std::string& errorMessage);

struct EmailParams {
    std::string smtp_host;
    std::string smtp_user;
    std::string smtp_password;
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
};

bool Email(const EmailParams& params, TimeoutType timeout, std::string& errorMessage);
