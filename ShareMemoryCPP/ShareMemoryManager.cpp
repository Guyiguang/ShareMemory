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

bool ShareMemoryManager::WriteData(const uint8_t* data, uint32_t dataType, uint32_t width, uint32_t height,
                                 uint32_t channels, uint32_t dimensions)
{
    // Calculate data size based on type and parameters
    size_t size = 0;
    if (dataType == static_cast<uint32_t>(FrameType::IMAGE)) { // Image
        size = width * height * channels;
    } else if (dataType == static_cast<uint32_t>(FrameType::POINTCLOUD)) { // Point cloud
        size = width * sizeof(float) * dimensions;
    } else if (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP)) { // Height map
        size = width * height * sizeof(float);
    } else {
        Log("Invalid data type");
        return false;
    }

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

            // Set data type and dimensions
            m_pHeader->DataType = dataType;
            m_pHeader->Width = width;
            m_pHeader->Height = height;
            m_pHeader->Reserved = dataType == static_cast<uint32_t>(FrameType::IMAGE) ? channels : dimensions;

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
            
            // 根据数据类型输出不同的信息
            if (dataType == static_cast<uint32_t>(FrameType::IMAGE)) {
                ss << "Image"
                   << ", Width: " << width
                   << ", Height: " << height
                   << ", Channels: " << channels;
            }
            else if (dataType == static_cast<uint32_t>(FrameType::POINTCLOUD)) {
                ss << "PointCloud"
                   << ", Points: " << width
                   << ", Dimensions: " << dimensions;
            }
            else if (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP)) {
                ss << "HeightMap"
                   << ", Width: " << width
                   << ", Height: " << height;
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

bool ShareMemoryManager::WriteHeightMapData(const float* heightData, 
                                          uint32_t width, 
                                          uint32_t height,
                                          float xSpacing, 
                                          float ySpacing)
{
    if (!m_pBuffer || !heightData) {
        Log("Buffer or height data is null");
        return false;
    }

    // 计算数据大小
    size_t dataSize = width * height * sizeof(float);
    
    // 检查数据大小是否超出限制
    if (dataSize > m_size - sizeof(SharedMemoryHeader)) {
        Log("Height map data too large");
        return false;
    }

    // 获取互斥锁
    DWORD waitResult = WaitForSingleObject(m_hMutex, 1000);
    if (waitResult != WAIT_OBJECT_0) {
        Log("Failed to acquire mutex");
        return false;
    }

    bool success = false;
    try {
        // 检查内存状态
        if (m_pHeader->Status != static_cast<uint32_t>(MemoryStatus::Empty)) {
            Log("Memory not empty, previous data not consumed");
            success = false;
        }
        else {
            // 计算高度范围
            float minHeight = heightData[0];
            float maxHeight = heightData[0];
            for (size_t i = 1; i < width * height; i++) {
                if (heightData[i] < minHeight) minHeight = heightData[i];
                if (heightData[i] > maxHeight) maxHeight = heightData[i];
            }

            // 设置状态为写入中
            m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Writing);
            m_pHeader->DataSize = static_cast<uint32_t>(dataSize);
            m_pHeader->FrameId = ++m_frameId;
            m_pHeader->DataType = 2; // 高度图类型
            m_pHeader->Width = width;
            m_pHeader->Height = height;

            // 复制高度图数据
            memcpy(m_pData, heightData, dataSize);

            // 计算校验和
            m_pHeader->Checksum = CalculateChecksum(reinterpret_cast<const uint8_t*>(heightData), dataSize);
            
            // 更新状态为就绪
            m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Ready);

            std::stringstream ss;
            ss << "Height map data written successfully - "
               << "Size: " << dataSize << " bytes, "
               << "Frame ID: " << m_frameId << ", "
               << "Width: " << width << ", "
               << "Height: " << height << ", "
               << "X Spacing: " << xSpacing << ", "
               << "Y Spacing: " << ySpacing << ", "
               << "Height Range: [" << minHeight << ", " << maxHeight << "]";
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

bool ShareMemoryManager::TryReadData(std::vector<uint8_t>& buffer, uint32_t& dataType, uint32_t& width, uint32_t& height)
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
        // Validate magic number
        if (m_pHeader->Magic != 0x12345678) {
            Log("Invalid magic number");
            success = false;
        }
        // Check if data is ready
        else if (m_pHeader->Status != static_cast<uint32_t>(MemoryStatus::Ready)) {
            success = false;
        }
        else {
            // Read data
            buffer.resize(m_pHeader->DataSize);
            memcpy(buffer.data(), m_pData, m_pHeader->DataSize);

            // Validate checksum
            uint32_t checksum = CalculateChecksum(buffer.data(), buffer.size());
            if (checksum != m_pHeader->Checksum) {
                Log("Checksum mismatch");
                success = false;
            }
            else {
                // Get metadata
                dataType = m_pHeader->DataType;
                width = m_pHeader->Width;
                height = m_pHeader->Height;

                // Mark memory as empty
                m_pHeader->Status = static_cast<uint32_t>(MemoryStatus::Empty);

                std::stringstream ss;
                ss << "Data read successfully - Size: " << buffer.size()
                   << " bytes, Frame ID: " << m_pHeader->FrameId
                   << ", Type: ";

                if (dataType == static_cast<uint32_t>(FrameType::IMAGE)) {
                    ss << "Image";
                }
                else if (dataType == static_cast<uint32_t>(FrameType::POINTCLOUD)) {
                    ss << "PointCloud";
                }
                else if (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP)) {
                    ss << "HeightMap";
                }

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
    uint32_t dataType = 0, width = 0, height = 0;

    while (m_isMonitoring) {
        if (TryReadData(buffer, dataType, width, height)) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_dataCallback) {
                m_dataCallback(buffer.data(), buffer.size(), dataType, width, height);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace SharedMemory 