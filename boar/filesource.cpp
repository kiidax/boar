/* Boar - Boar is a toolkit to modify text files.
* Copyright (C) 2017 Katsuya Iida. All rights reserved.
*/

#include "stdafx.h"

#include "boar.h"
#include "filesource.h"

namespace boar
{
    typedef std::function<void(const void*, size_t)> DataSourceCallbackType;

    static const int NUM_OVERLAPS = 3;
    static const size_t CHUNK_SIZE = 64L * 1024;

    void FileSourceWithMemoryMapping(const boost::filesystem::path& fileName, DataSourceCallbackType callback)
    {
        bool success = false;
        LPCWSTR lpfileName = fileName.native().c_str();
        HANDLE hFile = CreateFileW(lpfileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMapping != NULL)
            {
                VOID* lpAddress = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
                if (lpAddress != NULL)
                {
                    MEMORY_BASIC_INFORMATION mbi;
                    if (VirtualQuery(lpAddress, &mbi, sizeof mbi) != 0)
                    {
                        callback(lpAddress, mbi.RegionSize);
                        success = true;
                    }
                    UnmapViewOfFile(lpAddress);
                }
                CloseHandle(hMapping);
            }
            CloseHandle(hFile);
        }
        if (!success)
        {
            DWORD dwErrorCode = GetLastError();
            TCHAR buf[1024];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwErrorCode, 0, buf, sizeof buf, NULL);
            std::wstring s(reinterpret_cast<wchar_t*>(buf));
            std::wcout << s.substr(0, s.length() - 2) << std::endl;
        }
    }

    void FileSourceWithFileRead(const boost::filesystem::path& fileName, DataSourceCallbackType f)
    {
        bool success = false;
        LPCWSTR lpfileName = fileName.native().c_str();
        HANDLE hFile = CreateFileW(lpfileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            BYTE *buf = new BYTE[CHUNK_SIZE];
            if (buf != nullptr)
            {
                DWORD readBytes;
                while (true)
                {
                    if (!ReadFile(hFile, buf, CHUNK_SIZE, &readBytes, NULL))
                        break;
                    if (readBytes == 0)
                        break;
                    f(buf, readBytes);
                }
                delete[] buf;
            }
            CloseHandle(hFile);
        }
        if (!success)
        {
            DWORD dwErrorCode = GetLastError();
            TCHAR buf[1024];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwErrorCode, 0, buf, sizeof buf, NULL);
            std::wstring s(reinterpret_cast<wchar_t*>(buf));
            std::wcout << s.substr(0, s.length() - 2) << std::endl;
        }
    }

    void FileSourceWithOverlapRead(const boost::filesystem::path& fileName, DataSourceCallbackType callback)
    {
        bool success = false;
        LPCWSTR lpfileName = fileName.native().c_str();
        HANDLE hFile = CreateFileW(lpfileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            BYTE* buf = new BYTE[NUM_OVERLAPS * CHUNK_SIZE];
            if (buf != nullptr)
            {
                OVERLAPPED ol[NUM_OVERLAPS];
                int processIndex = 0;
                int numWaiting = 0;
                size_t offset = 0;
                while (true)
                {
                    if (numWaiting < NUM_OVERLAPS)
                    {
                        int readIndex = (processIndex + numWaiting) % NUM_OVERLAPS;
                        ZeroMemory(&ol[readIndex], sizeof ol[readIndex]);
                        ol[readIndex].Offset = static_cast<DWORD>(offset);
                        ol[readIndex].OffsetHigh = static_cast<DWORD>(offset >> 32);
                        offset += CHUNK_SIZE;
                        if (ReadFile(hFile, buf + readIndex * CHUNK_SIZE, CHUNK_SIZE, NULL, &ol[readIndex]) || GetLastError() != ERROR_IO_PENDING)
                        {
                            break;
                        }
                        numWaiting++;
                    }
                    else
                    {
                        DWORD readBytes;
                        while (true)
                        {
                            if (!GetOverlappedResult(hFile, &ol[processIndex], &readBytes, TRUE))
                            {
                                readBytes = 0;
                                if (GetLastError() == ERROR_HANDLE_EOF)
                                {
                                    success = true;
                                }
                                break;
                            }
                            if (readBytes > 0)
                            {
                                assert(readBytes <= CHUNK_SIZE);
                                break;
                            }
                        }
                        if (readBytes == 0)
                            break;
                        callback(buf + processIndex * CHUNK_SIZE, readBytes);
                        processIndex = (processIndex + 1) % NUM_OVERLAPS;
                        numWaiting--;
                    }
                }
                delete[] buf;
            }
            CloseHandle(hFile);
        }
        if (!success)
        {
            DWORD dwErrorCode = GetLastError();
            TCHAR buf[1024];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwErrorCode, 0, buf, sizeof buf, NULL);
            std::wstring s(reinterpret_cast<wchar_t*>(buf));
            std::wcout << s.substr(0, s.length() - 2) << std::endl;
        }
    }
}