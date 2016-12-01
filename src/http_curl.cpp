#include <curl/curl.h>
#include <string>

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
    };
}

bool HttpHead(const std::string& url, unsigned timeout, std::string& errorMessage, bool verifypeer) {
    CURLHandle handle;
    handle.value = ::curl_easy_init();
    if (!handle.value) {
        errorMessage = "CURL init failed";
        return false;
    }
    ::CURLcode code;
#define HANDLE_CURL_CODE(asdf) \
    code = (asdf); \
    if (code != ::CURLE_OK) { \
        errorMessage = std::string("CURL error: ") + ::curl_easy_strerror(code); \
        return false; \
    }
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_NOBODY, 1L)); // HEAD request
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_URL, url.c_str()));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_TIMEOUT, static_cast<long>(timeout)));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_FOLLOWLOCATION, 1L));
    HANDLE_CURL_CODE(curl_easy_setopt(handle.value, ::CURLOPT_SSL_VERIFYPEER, verifypeer ? 1L : 0L));
    HANDLE_CURL_CODE(::curl_easy_perform(handle.value));
    long http_code = 0;
    HANDLE_CURL_CODE(curl_easy_getinfo(handle.value, ::CURLINFO_RESPONSE_CODE, &http_code));
    if (http_code != 200) {
        errorMessage.assign("HTTP Status " + std::to_string(http_code));
        return false;
    }
#undef HANDLE_CURL_CODE
    return true;
}
