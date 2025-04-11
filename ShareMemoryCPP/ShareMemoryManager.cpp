/**
 * @file ShareMemoryManager.cpp
 * @brief 共享内存管理器的实现文件
 * @author gyg
 * @date 2024-04-02
 */

#include "ShareMemoryManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>

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
    , m_isMonitoring(false)
    , m_dataCallback(nullptr)
{
    Log("ShareMemoryManager constructed");
}

ShareMemoryManager::~ShareMemoryManager()
{
    StopMonitoring();
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
    Log("Initializing shared memory manager");

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
        SetError(ErrorCode::NoError, "Failed to create file mapping object");
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
        SetError(ErrorCode::NoError, "Failed to map view of file");
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
    
    // Initialize data info
    m_pHeader->info = {};  // Initialize all fields to 0 using default constructor
    
    // Clear error message
    memset(m_pHeader->ErrorMsg, 0, sizeof(m_pHeader->ErrorMsg));

    Log("Shared memory initialized successfully");
    return true;
}

bool ShareMemoryManager::WriteData(const uint8_t* data, size_t size, const DataInfo& info)
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

            // Copy data info
            m_pHeader->info = info;

            // Copy data
            memcpy(m_pData, data, size);

            // Calculate and set checksum
            m_pHeader->Checksum = CalculateChecksum(data, size);

            // Set status to ready
            m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Ready);

            std::stringstream ss;
            ss << "Data written successfully - Size: " << size 
               << " bytes, Frame ID: " << m_frameId
               << ", Type: ";
            
            switch (static_cast<FrameType>(info.dataType)) {
                case FrameType::IMAGE:
                    ss << "Image"
                       << ", Width: " << info.width
                       << ", Height: " << info.height
                       << ", Channels: " << info.channels;
                    break;
                case FrameType::POINTCLOUD:
                    ss << "PointCloud"
                       << ", Points: " << info.width
                       << ", Dimensions: " << info.height;
                    break;
                case FrameType::HEIGHTMAP:
                    ss << "HeightMap"
                       << ", Width: " << info.width
                       << ", Height: " << info.height
                       << ", Spacing: [" << info.xSpacing << ", " << info.ySpacing << "]";
                    break;
            }

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

bool ShareMemoryManager::ReadData(std::vector<uint8_t>& buffer, DataInfo& info)
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
        // Check if data is ready
        if (m_pHeader->Status != static_cast<uint32_t>(MemoryStatus::Ready)) {
            // Only log if status is not Empty (to reduce noise)
            if (m_pHeader->Status != static_cast<uint32_t>(MemoryStatus::Empty)) {
                Log("Data not ready");
            }
            success = false;
        }
        else {
            // Get data size
            size_t dataSize = m_pHeader->DataSize;

            // Resize buffer
            buffer.resize(dataSize);

            // Copy data
            memcpy(buffer.data(), m_pData, dataSize);

            // Verify checksum
            uint32_t checksum = CalculateChecksum(buffer.data(), dataSize);
            if (checksum != m_pHeader->Checksum) {
                Log("Checksum verification failed");
                success = false;
            }
            else {
                // Copy data info
                info = m_pHeader->info;

                // Set status to empty
                m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Empty);

                std::stringstream ss;
                ss << "Data read successfully - Size: " << dataSize 
                   << " bytes, Frame ID: " << m_pHeader->FrameId;
                Log(ss.str());
                success = true;
            }
        }
    }
    catch (const std::exception& e) {
        Log(std::string("Exception during read: ") + e.what());
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
        
        // Reset data info
        m_pHeader->info = {};  // Initialize all fields to 0
        
        // Clear error message
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

void ShareMemoryManager::SetDataReceivedCallback(DataReceivedCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_dataCallback = callback;
}

void ShareMemoryManager::StartMonitoring()
{
    if (!m_isMonitoring) {
        m_isMonitoring = true;
        m_monitorThread = std::thread(&ShareMemoryManager::MonitorThreadProc, this);
    }
}

void ShareMemoryManager::StopMonitoring()
{
    if (m_isMonitoring) {
        m_isMonitoring = false;
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
    }
}

void ShareMemoryManager::MonitorThreadProc()
{
    std::vector<uint8_t> buffer;
    DataInfo info;
    const int readInterval = 50; // Increase interval to 50ms to reduce CPU usage

    while (m_isMonitoring) {
        if (ReadData(buffer, info)) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_dataCallback) {
                m_dataCallback(buffer.data(), buffer.size(), info.dataType, info.width, info.height);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(readInterval));
    }
}

} // namespace SharedMemory 