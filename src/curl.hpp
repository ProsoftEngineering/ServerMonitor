#ifndef KAINJOW_SERVERMONITOR_CURL_HPP
#define KAINJOW_SERVERMONITOR_CURL_HPP

#include "types.hpp"

bool HttpHead(const std::string& url, TimeoutType timeout, std::string& errorMessage, bool verifypeer);

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
