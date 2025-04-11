/**
 * @file ShareMemoryManager.h
 * @brief 共享内存管理器的头文件，提供C++端共享内存操作的接口
 * @author gyg
 * @date 2024-04-02
 */

#pragma once

#define NOMINMAX  // 防止Windows.h定义min和max宏
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

    /**
     * @brief 共享内存中的帧数据类型
     */
    enum class FrameType {
        IMAGE = 0,       ///< 图像数据
        POINTCLOUD = 1,  ///< 点云数据
        HEIGHTMAP = 2    ///< 高度图数据
    };

    /**
     * @brief 统一的数据信息结构
     */
    #pragma pack(push, 1)
    struct DataInfo {
        uint32_t width;          ///< 宽度（图像/高度图）或点数量（点云）
        uint32_t height;         ///< 高度（图像/高度图）或点维度（点云）
        uint32_t channels;       ///< 通道数（图像）
        float xSpacing;          ///< X方向间距（高度图）
        float ySpacing;          ///< Y方向间距（高度图）
        uint32_t dataType;       ///< 数据类型（FrameType枚举）
        uint64_t timestamp;      ///< 时间戳
    };
    #pragma pack(pop)

    /**
     * @brief 共享内存头部结构
     */
    #pragma pack(push, 1)
    struct SharedMemoryHeader {
        uint32_t Magic;          ///< 用于验证的魔数 (0x12345678)
        uint32_t Status;         ///< 内存状态，来自 MemoryStatus 枚举
        uint32_t DataSize;       ///< 数据大小（字节）
        uint32_t Checksum;       ///< 数据校验和
        uint32_t FrameId;        ///< 递增的帧ID
        DataInfo info;           ///< 统一的数据信息
        char ErrorMsg[128];      ///< 错误信息
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
         * @brief 统一的数据写入接口
         * @param data 数据指针
         * @param size 数据大小
         * @param info 数据信息
         * @return 是否成功写入
         */
        bool WriteData(const uint8_t* data, size_t size, const DataInfo& info);
        
        /**
         * @brief 统一的数据读取接口
         * @param buffer 输出缓冲区
         * @param info 输出数据信息
         * @return 是否成功读取
         */
        bool ReadData(std::vector<uint8_t>& buffer, DataInfo& info);

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