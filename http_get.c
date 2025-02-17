//
//    A contribution to https://github.com/xairline/xa-snow by zodiac1214
//
//    Copyright (C) 2025  Holger Teutsch
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Lesser General Public
//    License as published by the Free Software Foundation; either
//    version 2.1 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
//    USA
//

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

extern void log_msg(const char *fmt, ...);

#if IBM == 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinHttp.h>

extern void log_msg(const char *fmt, ...);

bool
HttpGet(const char *url, FILE *f, int timeout)
{
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    BOOL  bResults = FALSE;
    HINTERNET  hSession = NULL,
               hConnect = NULL,
               hRequest = NULL;

    int result = 0;

    int url_len = strlen(url);
    WCHAR *url_wc = alloca((url_len + 1) * sizeof(WCHAR));
    WCHAR *host_wc = alloca((url_len + 1) * sizeof(WCHAR));
    WCHAR *path_wc = alloca((url_len + 1) * sizeof(WCHAR));

    mbstowcs_s(NULL, url_wc, url_len + 1, url, _TRUNCATE);

    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);

    urlComp.lpszHostName = host_wc;
    urlComp.dwHostNameLength  = (DWORD)(url_len + 1);

    urlComp.lpszUrlPath = path_wc;
    urlComp.dwUrlPathLength   = (DWORD)(url_len + 1);

    // Crack the url_wc.
    if (!WinHttpCrackUrl(url_wc, 0, 0, &urlComp)) {
        log_msg("Error %u in WinHttpCrackUrl.", GetLastError());
        goto error_out;
    }

    char buffer[16 * 1024];

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen( L"toliss_sb",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0 );

    if (NULL == hSession) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    timeout *= 1000;
    if (! WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout)) {
        log_msg("can't set timeouts");
        goto error_out;
    }

    hConnect = WinHttpConnect(hSession, host_wc, urlComp.nPort, 0);
    if (NULL == hConnect) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path_wc, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (NULL == hRequest) {
        log_msg("Can't open HTTP request: %u", GetLastError());
        goto error_out;
    }

    bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (! bResults) {
        log_msg("Can't send HTTP request: %u", GetLastError());
        goto error_out;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (! bResults) {
        log_msg("Can't receive response", GetLastError());
        goto error_out;
    }

    while (1) {
        DWORD res = WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (!res) {
            log_msg("%d, Error %u in WinHttpQueryDataAvailable.", res, GetLastError());
            goto error_out;
        }

        // log_msg("dwSize %d", dwSize);
        if (0 == dwSize) {
            break;
        }

        while (dwSize > 0) {
            int get_len = (dwSize < sizeof(buffer) ? dwSize : sizeof(buffer));

            bResults = WinHttpReadData(hRequest, buffer, get_len, &dwDownloaded);
            if (! bResults){
               log_msg("Error %u in WinHttpReadData.", GetLastError());
               goto error_out;
            }

            if (NULL != f) {
                fwrite(buffer, 1, dwDownloaded, f);
                if (ferror(f)) {
                    log_msg("error wrinting file");
                    goto error_out;
                }
            }

            dwSize -= dwDownloaded;
        }
    }

    result = 1;

error_out:
    // Close any open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    log_msg("tlsb_http_get result: %d", result);
    return result;
}

#else   // Linux or MacOS
#include <curl/curl.h>
bool
HttpGet(const char *url, FILE *f, int timeout)
{
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl == NULL)
        return 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    res = curl_easy_perform(curl);

    // Check for errors
    if(res != CURLE_OK) {
        log_msg("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }

    curl_off_t dl_size;
    res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T , &dl_size);
    if(res == CURLE_OK)
        log_msg("Downloaded %d bytes", (int)dl_size);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return true;
}
#endif
