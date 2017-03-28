#ifndef KAINJOW_SERVERMONITOR_CURL_HPP
#define KAINJOW_SERVERMONITOR_CURL_HPP

#include "types.hpp"
#include <string>

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

#endif
