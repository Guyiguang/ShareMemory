# 共享内存通信框架设计文档

## 1. 设计目标

本框架旨在实现C++和C#程序间的高效数据传输，主要用于图像和点云数据的实时共享。设计目标包括：

- 高性能：通过共享内存实现零拷贝数据传输
- 可靠性：使用互斥锁和校验和确保数据一致性
- 灵活性：支持不同类型数据的传输（图像和点云）
- 安全性：防止数据竞争和内存泄漏

## 2. 架构设计

### 2.1 核心组件

1. **共享内存管理器**
   - C++端：`ShareMemoryManager`类（生产者）
   - C#端：`SharedMemoryManager`类（消费者）

2. **内存布局**
   ```
   +------------------+
   |  Header (固定大小) |
   +------------------+
   |  Data (可变大小)   |
   +------------------+
   ```

3. **同步机制**
   - 互斥锁：确保独占访问
   - 状态标志：Empty、Writing、Ready、Error

### 2.2 数据结构

#### 共享内存头部 (SharedMemoryHeader)
```cpp
struct SharedMemoryHeader {
    uint32_t Magic;          // 魔数 (0x12345678)
    uint32_t Status;         // 状态
    uint32_t DataSize;       // 数据大小
    uint32_t Checksum;       // 校验和
    uint32_t FrameId;        // 帧ID
    uint32_t DataType;       // 数据类型
    uint32_t Width;          // 宽度/点数
    uint32_t Height;         // 高度/点大小
    uint32_t Reserved;       // 保留
    char ErrorMsg[128];      // 错误信息
};
```

### 2.1 共享内存结构详解

#### 内存布局
```
+----------------------------------------+
|              共享内存区域                |
+----------------------------------------+
|          SharedMemoryHeader             |  <- 固定大小：164字节
|----------------------------------------|
| - Magic (4字节)                         |  <- 固定值：0x12345678
| - Status (4字节)                        |  <- 状态枚举：0=Empty, 1=Writing, 2=Ready, 3=Error
| - DataSize (4字节)                      |  <- 实际数据大小
| - Checksum (4字节)                      |  <- 数据校验和
| - FrameId (4字节)                       |  <- 帧序号，从1开始
| - DataType (4字节)                      |  <- 0=图像, 1=点云
| - Width (4字节)                         |  <- 图像宽度/点数量
| - Height (4字节)                        |  <- 图像高度/点大小
| - Reserved (4字节)                      |  <- 保留字段
| - ErrorMsg[128] (128字节)               |  <- 错误信息缓冲区
|----------------------------------------|
|              数据区域                    |  <- 可变大小
|          (Data Section)                 |
|                                        |
| - 图像数据: Width * Height * Channels    |
|   或                                    |
| - 点云数据: PointCount * PointSize       |
+----------------------------------------+
```

#### 状态转换图
```
      Initialize
          ↓
    +------------+
    |   Empty    | ←--------+
    +------------+          |
          ↓ WriteData      |
    +------------+          |
    |  Writing   |          |
    +------------+          |
          ↓                |
    +------------+          |
    |   Ready    |          |
    +------------+          |
          ↓ ReadData       |
          +----------------+
```

#### 数据类型说明

1. **图像数据 (DataType = 0)**
   ```
   - Width: 图像宽度（像素）
   - Height: 图像高度（像素）
   - DataSize: Width * Height * Channels
   示例：640x480 RGB图像
   - Width = 640
   - Height = 480
   - DataSize = 640 * 480 * 3 = 921,600字节
   ```

2. **点云数据 (DataType = 1)**
   ```
   - Width: 点的数量（pointCount）
   - Height: 每个点的字节大小（pointSize）
   - DataSize: pointCount * pointSize
   示例：1000个点的点云，每个点12字节(XYZ坐标，每个坐标4字节)
   - Width = 1000
   - Height = 12
   - DataSize = 1000 * 12 = 12,000字节
   ```

#### 内存管理说明

1. **内存分配**
   - 总大小 = sizeof(SharedMemoryHeader) + 数据区域大小
   - 头部固定占用164字节
   - 数据区域大小根据实际需求设置（示例中为10MB）

2. **内存对齐**
   - 使用`#pragma pack(1)`确保内存紧凑对齐
   - C++和C#端保持相同的对齐方式
   - 避免因平台差异导致的内存布局不一致

3. **访问控制**
   - 使用互斥锁控制对共享内存的访问
   - 锁的粒度覆盖整个读写操作
   - 超时机制避免死锁（默认5000ms）

4. **数据校验**
   - Magic Number检查确保内存映射正确
   - Checksum验证数据完整性
   - FrameId检查帧序列完整性

## 3. 工作流程

### 3.1 写入流程（C++端）

1. 获取互斥锁
2. 检查内存状态（必须为Empty）
3. 设置状态为Writing
4. 写入数据和元信息
5. 计算校验和
6. 设置状态为Ready
7. 释放互斥锁

### 3.2 读取流程（C#端）

1. 获取互斥锁
2. 检查内存状态（必须为Ready）
3. 验证魔数
4. 读取数据
5. 验证校验和
6. 设置状态为Empty
7. 释放互斥锁

## 4. 使用方法

### 4.1 C++端（生产者）

```cpp
// 创建共享内存管理器
SharedMemory::ShareMemoryManager sharedMemory("TestSharedMemory", 1024 * 1024 * 10);

// 初始化
if (!sharedMemory.Initialize()) {
    // 处理错误
}

// 写入数据
std::vector<uint8_t> data = GenerateData();
if (!sharedMemory.WriteData(data.data(), data.size())) {
    // 处理错误
}

// 清空共享内存（需要时）
sharedMemory.ClearMemory();
```

### 4.2 C#端（消费者）

```csharp
// 创建共享内存管理器
using var sharedMemory = new SharedMemoryManager("TestSharedMemory", 1024 * 1024 * 10);

// 初始化
if (!sharedMemory.Initialize()) {
    // 处理错误
}

// 注册数据接收事件
sharedMemory.DataReceived += (sender, args) => {
    // 处理接收到的数据
    ProcessData(args.Data, args.DataType, args.Width, args.Height);
};
```

## 5. 错误处理

### 5.1 主要错误类型

1. 初始化错误
   - 互斥锁创建失败
   - 共享内存创建失败
   - 内存映射失败

2. 运行时错误
   - 内存未初始化
   - 数据大小超限
   - 校验和不匹配
   - 状态不一致

### 5.2 错误恢复

1. 使用`ClearMemory()`方法重置共享内存状态
2. 重新初始化共享内存
3. 记录详细日志用于问题诊断

## 6. 性能优化

1. 零拷贝传输：直接在共享内存中读写数据
2. 校验和计算优化：使用简单快速的算法
3. 状态检查优化：避免不必要的日志记录
4. 互斥锁超时设置：防止死锁

## 7. 注意事项

1. 确保C++和C#端使用相同的内存布局（结构体对齐）
2. 正确处理程序异常退出的情况
3. 定期检查和清理共享内存状态
4. 合理设置缓冲区大小，避免内存浪费
5. 注意32位和64位程序的兼容性

## 8. 调试方法

1. 使用日志文件
   - producer_log.txt：C++端日志
   - consumer_log.txt：C#端日志

2. 状态监控
   - 使用`LogStatus()`方法查看当前状态
   - 检查帧ID连续性
   - 监控数据大小变化

3. 常见问题排查
   - 检查魔数是否匹配
   - 验证数据大小是否合理
   - 确认互斥锁是否正常释放
   - 查看校验和计算是否正确

## 9. 发布注意事项

### 9.1 日志处理

类库中包含了详细的日志记录功能，主要用于开发和调试阶段：

1. **日志输出位置**
   - 控制台输出：`std::cout`（C++）和`Console.WriteLine`（C#）
   - 文件输出：`producer_log.txt`（C++）和`consumer_log.txt`（C#）

2. **日志类型**
   - 状态变化日志
   - 错误信息日志
   - 性能监控日志
   - 数据传输日志

3. **发布环境配置**
   
   在正式环境部署时，建议进行以下调整：

   ```cpp
   // C++端 (ShareMemoryManager.cpp)
   void ShareMemoryManager::Log(const std::string& message)
   {
   #ifdef _DEBUG
       // 开发环境：完整日志
       // 输出到控制台
       std::cout << logMessage << std::endl;
       // 输出到文件
       std::ofstream logFile("producer_log.txt", std::ios::app);
       if (logFile.is_open()) {
           logFile << logMessage << std::endl;
           logFile.close();
       }
   #else
       // 生产环境：仅记录关键错误
       if (message.find("ERROR") != std::string::npos) {
           // 仅输出错误信息到文件
           std::ofstream logFile("producer_error.log", std::ios::app);
           if (logFile.is_open()) {
               logFile << logMessage << std::endl;
               logFile.close();
           }
       }
   #endif
   }
   ```

   ```csharp
   // C#端 (SharedMemoryManager.cs)
   private void Log(string message)
   {
   #if DEBUG
       // 开发环境：完整日志
       Console.WriteLine(logEntry);
       _logWriter?.WriteLine(logEntry);
   #else
       // 生产环境：仅记录关键错误
       if (message.Contains("ERROR"))
       {
           using var errorLog = new StreamWriter("consumer_error.log", true);
           errorLog.WriteLine(logEntry);
       }
   #endif
   }
   ```

4. **性能考虑**
   - 频繁的日志写入会影响性能
   - 文件IO操作可能造成延迟
   - 建议在生产环境中最小化日志输出

5. **日志清理**
   - 实现日志文件自动归档
   - 定期清理旧日志文件
   - 控制单个日志文件大小

### 9.2 发布检查清单

1. **性能优化**
   - [ ] 禁用不必要的状态打印
   - [ ] 关闭调试信息输出
   - [ ] 优化日志写入频率
   - [ ] 配置错误日志筛选级别