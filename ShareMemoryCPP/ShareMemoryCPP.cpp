/**
 * @file ShareMemoryCPP.cpp
 * @brief C++ Shared Memory Test Program
 * @author AI Assistant
 * @date 2024-04-02
 */

#define NOMINMAX  // 防止Windows.h定义min和max宏
#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include "ShareMemoryManager.h"
#include <cmath>

using namespace SharedMemory;

/**
 * @brief Generate test image data (gradient image)
 * @param width Image width
 * @param height Image height
 * @param channels Image channels
 * @return Test image data
 */
std::vector<uint8_t> GenerateTestImage(int width, int height, int channels)
{
    std::vector<uint8_t> imageData(width * height * channels);
    static std::default_random_engine generator;
    static std::uniform_int_distribution<int> distribution(0, 255);
    
    for (size_t i = 0; i < imageData.size(); ++i) {
        imageData[i] = static_cast<uint8_t>(distribution(generator));
    }
    
    return imageData;
}

/**
 * @brief Generate test point cloud data
 * @param pointCount Number of points
 * @param pointSize Size of each point in bytes
 * @return Test point cloud data
 */
std::vector<uint8_t> GenerateTestPointCloud(int numPoints)
{
    std::vector<uint8_t> pointCloudData(numPoints * sizeof(float) * 3); // xyz
    static std::default_random_engine generator;
    static std::uniform_real_distribution<float> distribution(-10.0f, 10.0f);
    
    float* points = reinterpret_cast<float*>(pointCloudData.data());
    for (int i = 0; i < numPoints * 3; ++i) {
        points[i] = distribution(generator);
    }
    
    return pointCloudData;
}

/**
 * @brief 生成测试用的高度图数据
 * @param width 宽度方向的点数
 * @param height 高度方向的点数
 * @return 高度值数组（每个点的z值）
 */
std::vector<float> GenerateTestHeightMap(uint32_t width, uint32_t height)
{
    std::vector<float> heightData(width * height);
    
    // 生成一个包含多个正弦波的地形
    const float frequency1 = 2.0f * 3.14159f / width;  // X方向的频率
    const float frequency2 = 2.0f * 3.14159f / height; // Y方向的频率
    
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            // 生成多个正弦波叠加的地形
            float z = 0.0f;
            
            // 主要地形波
            z += 5.0f * std::sin(frequency1 * x);
            z += 3.0f * std::cos(frequency2 * y);
            
            // 添加一些小的起伏
            z += 1.0f * std::sin(frequency1 * 3 * x + frequency2 * 2 * y);
            
            // 添加一个中心隆起
            float dx = x - width / 2.0f;
            float dy = y - height / 2.0f;
            float distance = std::sqrt(dx * dx + dy * dy);
            z += 2.0f * std::exp(-distance * 0.01f);
            
            // 存储高度值
            heightData[y * width + x] = z;
        }
    }
    
    return heightData;
}

int main()
{
    try
    {
        std::cout << "Shared Memory Test Program Starting..." << std::endl;
        
        // Create shared memory managers for producer and consumer
        const std::string memoryName = "TestSharedMemory";
        const size_t memorySize = 1024 * 1024 * 10; // 10MB
        
        // Create producer and consumer with the same memory name
        ShareMemoryManager producer(memoryName, memorySize);
        if (!producer.Initialize())
        {
            std::cerr << "Failed to initialize producer shared memory" << std::endl;
            return 1;
        }
        
        // Create consumer with the same memory name
        ShareMemoryManager consumer(memoryName, memorySize);
        if (!consumer.Initialize())
        {
            std::cerr << "Failed to initialize consumer shared memory" << std::endl;
            return 1;
        }

        // Set up consumer callback
        consumer.SetDataReceivedCallback([](const uint8_t* data, size_t size, uint32_t dataType, uint32_t width, uint32_t height) {
            std::cout << "\n[Consumer] Received data:"
                     << "\n - Type: " << (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP) ? "HeightMap" : (dataType == 0 ? "Image" : "PointCloud"))
                     << "\n - Size: " << size << " bytes"
                     << "\n - Width: " << width
                     << "\n - Height: " << height
                     << "\n - First byte: 0x" << std::hex << (int)data[0] 
                     << std::dec << std::endl;

            if (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP)) {
                const float* heightData = reinterpret_cast<const float*>(data);
                float minHeight = heightData[0];
                float maxHeight = heightData[0];
                for (size_t i = 1; i < width * height; i++) {
                    minHeight = std::min(minHeight, heightData[i]);
                    maxHeight = std::max(maxHeight, heightData[i]);
                }
                std::cout << " - Height range: [" << minHeight << ", " << maxHeight << "]" << std::endl;
            }
        });

        // Start consumer monitoring
        consumer.StartMonitoring();
        
        std::cout << "Starting test data exchange..." << std::endl;
        
        bool isImage = true;
        int frameCount = 0;
        const int totalFrames = 10; // Send 10 frames and then exit

        while (frameCount < totalFrames)
        {
            std::vector<uint8_t> data;
            uint32_t dataType, width, height, channels, dimensions;
            
            if (frameCount % 3 == 0) {  // 每三帧发送一次图像数据
                // Generate 640x480 RGB test image
                channels = 3; // RGB image
                data = GenerateTestImage(640, 480, channels);
                dataType = 0; // Image
                width = 640;
                height = 480;
                dimensions = 0; // Not used for images
                std::cout << "Preparing to write RGB image data..." << std::endl;
            } 
            else if (frameCount % 3 == 1) {  // 每三帧发送一次点云数据
                // Generate point cloud data with 1000 points
                dimensions = 3; // XYZ point cloud
                data = GenerateTestPointCloud(1000);
                dataType = 1; // PointCloud
                width = 1000; // Number of points
                height = sizeof(float); // Size per component
                channels = 0; // Not used for point clouds
                std::cout << "Preparing to write XYZ point cloud data..." << std::endl;
            }
            else {  // 每三帧发送一次高度图数据
                // 生成200x200的高度图数据
                width = 200;
                height = 200;
                std::vector<float> heightData = GenerateTestHeightMap(width, height);
                std::cout << "Preparing to write height map data..." << std::endl;
                
                // 写入高度图数据
                bool writeSuccess = producer.WriteHeightMapData(heightData.data(), width, height, 0.1f, 0.1f);
                if (writeSuccess) {
                    frameCount++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;  // 跳过后面的WriteData调用
            }

            // Try to write data
            bool writeSuccess = producer.WriteData(data.data(), dataType, width, height, channels, dimensions);
            
            if (writeSuccess) {
                frameCount++;
            }
            
            // Wait before next frame
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Stop consumer monitoring
        consumer.StopMonitoring();
        
        std::cout << "Test completed successfully." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Program exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

// 运行程序: Ctrl + F5 或调试 >"开始执行(不调试)"菜单
// 调试程序: F5 或调试 >"开始调试"菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到">"项目">"添加新项"以创建新的代码文件，或转到">"项目">"添加现有项"以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到"文件">"打开">"项目"并选择 .sln 文件
