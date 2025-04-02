/**
 * @file ShareMemoryManager.cpp
 * @brief 共享内存管理器的实现文件
 * @author AI Assistant
 * @date 2024-04-02
 */

#include "ShareMemoryManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace SharedMemory {

ShareMemoryManager::ShareMemoryManager(const std::string& name, size_t size)
    : m_name(name)
    , m_size(size + sizeof(SharedMemoryHeader))
    , m_hMapFile(NULL)
    , m_pBuffer(nullptr)
    , m_pHeader(nullptr)
    , m_pData(nullptr)
    , m_hMutex(NULL)
    , m_frameId(0)
{
    Log("ShareMemoryManager constructed");
}

ShareMemoryManager::~ShareMemoryManager()
{
    Log("Cleaning up resources");
    if (m_pBuffer) {
        UnmapViewOfFile(m_pBuffer);
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
    }
    if (m_hMutex) {
        CloseHandle(m_hMutex);
    }
    Log("ShareMemoryManager destroyed");
}

bool ShareMemoryManager::Initialize()
{
    Log("Initializing shared memory");

    // Create mutex
    m_hMutex = CreateMutexA(NULL, FALSE, (m_name + "_mutex").c_str());
    if (m_hMutex == NULL) {
        SetError(ErrorCode::NoError, "Failed to create mutex");
        return false;
    }

    // Create shared memory
    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(m_size),
        m_name.c_str()
    );

    if (m_hMapFile == NULL) {
        SetError(ErrorCode::NoError, "Could not create file mapping object");
        return false;
    }

    // Map memory view
    m_pBuffer = static_cast<uint8_t*>(
        MapViewOfFile(m_hMapFile,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            m_size)
    );

    if (m_pBuffer == nullptr) {
        SetError(ErrorCode::NoError, "Could not map view of file");
        return false;
    }

    // Setup header and data pointers
    m_pHeader = reinterpret_cast<SharedMemoryHeader*>(m_pBuffer);
    m_pData = m_pBuffer + sizeof(SharedMemoryHeader);

    // Initialize header
    m_pHeader->Magic = 0x12345678;
    m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Empty);
    m_pHeader->DataSize = 0;
    m_pHeader->Checksum = 0;
    m_pHeader->FrameId = 0;
    m_pHeader->DataType = 0;
    m_pHeader->Width = 0;
    m_pHeader->Height = 0;
    m_pHeader->Reserved = 0;
    memset(m_pHeader->ErrorMsg, 0, sizeof(m_pHeader->ErrorMsg));

    Log("Shared memory initialized successfully");
    return true;
}

bool ShareMemoryManager::WriteData(const uint8_t* data, size_t size)
{
    if (!m_pBuffer || size > (m_size - sizeof(SharedMemoryHeader))) {
        Log("Data size exceeds buffer capacity");
        return false;
    }

    // Wait for mutex
    DWORD waitResult = WaitForSingleObject(m_hMutex, 5000);
    if (waitResult != WAIT_OBJECT_0) {
        Log("Failed to acquire mutex");
        return false;
    }

    bool success = false;
    try {
        // Check if memory is empty
        if (m_pHeader->Status != static_cast<uint32_t>(MemoryStatus::Empty)) {
            Log("Memory not empty, previous data not consumed");
            success = false;
        }
        else {
            // Set status to writing
            m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Writing);
            m_pHeader->DataSize = static_cast<uint32_t>(size);
            m_pHeader->FrameId = ++m_frameId;

            // Set data type and dimensions based on data size
            if (size == 640 * 480 * 3) { // Image data
                m_pHeader->DataType = 0; // Image
                m_pHeader->Width = 640;
                m_pHeader->Height = 480;
            }
            else { // Point cloud data
                m_pHeader->DataType = 1; // PointCloud
                m_pHeader->Width = 1000; // Number of points
                m_pHeader->Height = sizeof(float) * 3; // Size per point
            }

            // Copy data
            memcpy(m_pData, data, size);

            // Calculate and set checksum
            m_pHeader->Checksum = CalculateChecksum(data, size);

            // Set status to ready
            m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Ready);

            std::stringstream ss;
            ss << "Data written successfully - Size: " << size 
               << " bytes, Frame ID: " << m_frameId
               << ", Type: " << (m_pHeader->DataType == 0 ? "Image" : "PointCloud");
            Log(ss.str());
            success = true;
        }
    }
    catch (const std::exception& e) {
        Log(std::string("Exception during write: ") + e.what());
        success = false;
    }

    ReleaseMutex(m_hMutex);
    return success;
}

uint32_t ShareMemoryManager::CalculateChecksum(const uint8_t* data, size_t size)
{
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 5) + checksum) + data[i];
    }
    return checksum;
}

void ShareMemoryManager::SetError(ErrorCode code, const std::string& message)
{
    m_lastError = message;
    if (m_pHeader) {
        m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Error);
        strncpy_s(m_pHeader->ErrorMsg, message.c_str(), sizeof(m_pHeader->ErrorMsg) - 1);
    }
    Log("ERROR: " + message);
}

void ShareMemoryManager::LogStatus(const std::string& operation)
{
    if (!m_pHeader) return;

    std::stringstream ss;
    ss << operation << " - Status: ";
    switch (static_cast<MemoryStatus>(m_pHeader->Status)) {
        case MemoryStatus::Empty: ss << "Empty"; break;
        case MemoryStatus::Writing: ss << "Writing"; break;
        case MemoryStatus::Ready: ss << "Ready"; break;
        case MemoryStatus::Error: ss << "Error"; break;
        default: ss << "Unknown"; break;
    }
    ss << ", Frame: " << m_pHeader->FrameId
       << ", Size: " << m_pHeader->DataSize;
    Log(ss.str());
}

void ShareMemoryManager::Log(const std::string& message)
{
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    
    // Use localtime_s instead of localtime
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);
    
    std::stringstream ss;
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");

    // Format log message
    std::string logMessage = ss.str() + " [Producer] " + message;

    // Output to console
    std::cout << logMessage << std::endl;

    // Write to log file
    std::ofstream logFile("producer_log.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << logMessage << std::endl;
        logFile.close();
    }
}

bool ShareMemoryManager::ClearMemory()
{
    if (!m_pBuffer) {
        Log("Shared memory not initialized");
        return false;
    }

    // Wait for mutex
    DWORD waitResult = WaitForSingleObject(m_hMutex, 5000);
    if (waitResult != WAIT_OBJECT_0) {
        Log("Failed to acquire mutex");
        return false;
    }

    bool success = false;
    try {
        // Reset header to initial state
        m_pHeader->Magic = 0x12345678;
        m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Empty);
        m_pHeader->DataSize = 0;
        m_pHeader->Checksum = 0;
        m_pHeader->FrameId = 0;
        m_pHeader->DataType = 0;
        m_pHeader->Width = 0;
        m_pHeader->Height = 0;
        m_pHeader->Reserved = 0;
        memset(m_pHeader->ErrorMsg, 0, sizeof(m_pHeader->ErrorMsg));

        // Reset frame ID counter
        m_frameId = 0;

        Log("Shared memory cleared successfully");
        success = true;
    }
    catch (const std::exception& e) {
        Log(std::string("Exception during clear: ") + e.what());
        success = false;
    }

    ReleaseMutex(m_hMutex);
    return success;
}

} // namespace SharedMemory 