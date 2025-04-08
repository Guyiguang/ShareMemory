/**
 * @file ShareMemoryManager.h
 * @brief 共享内存管理器的头文件，提供C++端共享内存操作的接口
 * @author AI Assistant
 * @date 2024-04-02
 */

#pragma once

// 确保Windows.h包含正确
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // 注意使用小写的windows.h而不是Windows.h

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <thread>

namespace SharedMemory {

    // Memory status
    enum class MemoryStatus {
        Empty = 0,
        Writing = 1,
        Ready = 2,
        Error = 3
    };

    // Error codes
    enum class ErrorCode {
        NoError = 0,
        MemoryNotEmpty = 1,
        DataTooLarge = 2,
        ChecksumError = 3
    };

    // Shared memory header structure
    #pragma pack(push, 1)
    struct SharedMemoryHeader {
        uint32_t Magic;          // Magic number for validation (0x12345678)
        uint32_t Status;         // Status from MemoryStatus enum
        uint32_t DataSize;       // Size of data in bytes
        uint32_t Checksum;       // Simple checksum for data validation
        uint32_t FrameId;        // Incremental frame ID
        uint32_t DataType;       // 0=Image, 1=PointCloud
        uint32_t Width;          // Image width or point count
        uint32_t Height;         // Image height or point size
        uint32_t Reserved;       // Reserved for future use
        char ErrorMsg[128];      // Error message
    };
    #pragma pack(pop)

    /**
     * @brief 共享内存中的帧数据类型
     */
    enum class FrameType {
        IMAGE = 0,       ///< 图像数据
        POINTCLOUD = 1   ///< 点云数据
    };

    /**
     * @brief 共享内存帧头部信息结构
     */
    #pragma pack(push, 1) // 确保内存对齐
    struct FrameHeader {
        uint64_t frameId;        ///< 帧ID，递增
        uint64_t timestamp;      ///< 时间戳，毫秒
        uint32_t frameType;      ///< 帧类型，对应FrameType枚举
        uint32_t dataSize;       ///< 实际数据大小（字节）
        
        // 针对图像的额外信息
        uint32_t width;          ///< 图像宽度
        uint32_t height;         ///< 图像高度
        uint32_t channels;       ///< 图像通道数
        
        // 针对点云的额外信息
        uint32_t pointCount;     ///< 点的数量
        uint32_t pointSize;      ///< 每个点的字节大小
    };
    #pragma pack(pop)

    /**
     * @brief 控制区域结构，存储环形缓冲区的元数据
     */
    #pragma pack(push, 1)
    struct ControlBlock {
        uint32_t bufferSize;             ///< 总缓冲区大小（字节）
        uint32_t maxFrames;              ///< 最大可存储帧数
        uint32_t writeIndex;             ///< 写入位置索引
        uint32_t readIndex;              ///< 读取位置索引
        uint32_t frameCount;             ///< 当前缓冲区中的帧数量
        uint64_t lastFrameId;            ///< 最后写入的帧ID
    };
    #pragma pack(pop)

    /**
     * @brief 数据接收回调函数类型
     */
    using DataReceivedCallback = std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, uint32_t)>;

    /**
     * @brief 共享内存管理器类，负责创建和管理共享内存区域
     */
    class ShareMemoryManager {
    public:
        /**
         * @brief 构造函数，创建或打开共享内存
         * @param name 共享内存名称
         * @param size 共享内存大小（字节）
         */
        ShareMemoryManager(const std::string& name, size_t size);
        
        /**
         * @brief 析构函数，释放共享内存资源
         */
        ~ShareMemoryManager();

        bool Initialize();
        /**
         * @brief 写入数据到共享内存
         * @param data 数据指针
         * @param dataType 数据类型（0=图像，1=点云）
         * @param width 宽度（图像）或点数量（点云）
         * @param height 高度（图像）或每点大小（点云）
         * @param channels 图像通道数（用于图像类型）
         * @param dimensions 点云维度（用于点云类型，如3表示XYZ，6表示XYZRGB）
         * @return 是否成功写入
         */
        bool WriteData(const uint8_t* data, uint32_t dataType, uint32_t width, uint32_t height, 
                      uint32_t channels = 1, uint32_t dimensions = 3);
        
        /**
         * @brief 尝试读取数据
         * @param buffer 输出缓冲区
         * @param bufferSize 缓冲区大小
         * @param dataType 输出数据类型
         * @param width 输出宽度
         * @param height 输出高度
         * @return 是否成功读取数据
         */
        bool TryReadData(std::vector<uint8_t>& buffer, uint32_t& dataType, uint32_t& width, uint32_t& height);

        /**
         * @brief 设置数据接收回调函数
         * @param callback 回调函数
         */
        void SetDataReceivedCallback(DataReceivedCallback callback);

        /**
         * @brief 启动监听线程
         */
        void StartMonitoring();

        /**
         * @brief 停止监听线程
         */
        void StopMonitoring();

        std::string GetLastError() const { return m_lastError; }
        void LogStatus(const std::string& operation);

        /**
         * @brief 清空共享内存，将状态重置为Empty
         * @return 是否成功清空
         */
        bool ClearMemory();

    private:
        std::string m_name;
        size_t m_size;
        HANDLE m_hMapFile;
        uint8_t* m_pBuffer;
        SharedMemoryHeader* m_pHeader;
        uint8_t* m_pData;
        HANDLE m_hMutex;
        std::string m_lastError;
        uint32_t m_frameId;

        // 新增成员变量
        bool m_isMonitoring;
        std::thread m_monitorThread;
        DataReceivedCallback m_dataCallback;
        std::mutex m_callbackMutex;

        uint32_t CalculateChecksum(const uint8_t* data, size_t size);
        void SetError(ErrorCode code, const std::string& message);
        void Log(const std::string& message);
        
        /**
         * @brief 监听线程函数
         */
        void MonitorThreadProc();
    };

} // namespace SharedMemory 