#include "curl.hpp"
#include <curl/curl.h>
#include <string>
#include <iostream>
#include <vector>

#define HANDLE_CURL_CODE(what) \
    code = (what); \
    if (code != ::CURLE_OK) { \
        errorMessage = std::string("CURL error: ") + ::curl_easy_strerror(code); \
        return false; \
    }

namespace {
    struct CURLHandle {
        ::CURL *value;
        CURLHandle()
            : value(nullptr)
        {}
        ~CURLHandle() {
            if (value) {
                ::curl_easy_cleanup(value);
            }
        }
        operator ::CURL*() {
            return value;
        }
    };

    struct CURLSlist {
        struct ::curl_slist *list = nullptr;
        CURLSlist(const char *str) {
            append(str);
        }
        ~CURLSlist() {
            ::curl_slist_free_all(list);
        }
        void append(const char *str) {
            list = ::curl_slist_append(list, str);
        }
        const ::curl_slist* get() const {
            return list;
        }
    };
}

bool HttpHead(const HttpParams& params, std::string& errorMessage) {
    CURLHandle handle;
    handle.value = ::curl_easy_init();
    if (!handle.value) {
        errorMessage = "CURL init failed";
        return false;
    }
    ::CURLcode code;
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_NOBODY, 1L)); // HEAD request
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_URL, params.url.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_TIMEOUT, static_cast<long>(params.timeout)));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_FOLLOWLOCATION, 1L));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_SSL_VERIFYPEER, params.verifypeer ? 1L : 0L));
    HANDLE_CURL_CODE(::curl_easy_perform(handle.value));
    long http_code = 0;
    HANDLE_CURL_CODE(curl_easy_getinfo(handle.value, ::CURLINFO_RESPONSE_CODE, &http_code));
    if (http_code != params.status) {
        errorMessage = "HTTP response code: " + std::to_string(http_code);
        return false;
    }
    return true;
}

struct EmailHelper {
    size_t lines_read = 0;
    std::vector<std::string> payload_text;
    size_t payload_source(void *ptr, size_t size, size_t nmemb);
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    EmailHelper *helper = reinterpret_cast<EmailHelper*>(userp);
    return helper->payload_source(ptr, size, nmemb);
}

size_t EmailHelper::payload_source(void *ptr, size_t size, size_t nmemb)
{
    if (size == 0 || nmemb == 0 || lines_read >= payload_text.size()) {
        return 0;
    }
    // Note: this is assuming nmemb is big enough for each line, which it usually is set
    // to CURL_MAX_WRITE_SIZE which is default 16384.
    const std::string& str = payload_text.at(lines_read);
    const size_t len = str.size();
    std::memcpy(ptr, str.c_str(), len);
    ++lines_read;
    return len;
}

bool Email(const EmailParams& params, unsigned timeout, std::string& errorMessage) {
    CURLHandle handle;
    handle.value = ::curl_easy_init();
    if (!handle.value) {
        errorMessage = "CURL init failed";
        return false;
    }
    ::CURLcode code;
    const CURLSlist recipients(params.to.c_str());
    EmailHelper helper;
    
    const std::string crlf = "\r\n";
    helper.payload_text.push_back("To: " + params.to + crlf);
    helper.payload_text.push_back("From: " + params.from + crlf);
    helper.payload_text.push_back("Subject: " + params.subject + crlf);
    helper.payload_text.push_back(crlf);
    helper.payload_text.push_back(params.body + crlf);
    
    const std::string url = "smtp://" + params.smtp_host;
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_USERNAME, params.smtp_user.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_PASSWORD, params.smtp_password.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_URL, url.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_MAIL_FROM, params.from.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_MAIL_RCPT, recipients.get()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_READFUNCTION, payload_source));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_READDATA, &helper));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_UPLOAD, 1L));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL)));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_TIMEOUT, static_cast<long>(timeout)));
    HANDLE_CURL_CODE(curl_easy_setopt(handle, ::CURLOPT_VERBOSE, 0L)); // set to 1 to debug connection issues
    HANDLE_CURL_CODE(::curl_easy_perform(handle));
    long response_code = 0;
    HANDLE_CURL_CODE(curl_easy_getinfo(handle.value, ::CURLINFO_RESPONSE_CODE, &response_code));
    if (response_code != 250) {
        errorMessage = "SMTP response code: " + std::to_string(response_code);
        return false;
    }
    return true;
}
