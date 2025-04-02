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
        bool WriteData(const uint8_t* data, size_t size);
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

        uint32_t CalculateChecksum(const uint8_t* data, size_t size);
        void SetError(ErrorCode code, const std::string& message);
        void Log(const std::string& message);
    };

} // namespace SharedMemory 