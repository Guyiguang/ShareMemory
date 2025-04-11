/**
 * @file ShareMemoryCPP.cpp
 * @brief C++ Shared Memory Test Program
 * @author gyg
 * @date 2024-04-02
 */

#pragma once
#pragma execution_character_set("utf-8")

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
 * @brief Generate test height map data
 * @param width Number of points in width direction
 * @param height Number of points in height direction
 * @return Array of height values (z value for each point)
 */
std::vector<float> GenerateTestHeightMap(uint32_t width, uint32_t height)
{
    std::vector<float> heightData(width * height);
    
    // Generate terrain with multiple sine waves
    const float frequency1 = 2.0f * 3.14159f / width;   // Frequency in X direction
    const float frequency2 = 2.0f * 3.14159f / height;  // Frequency in Y direction
    
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            // Generate terrain by combining multiple sine waves
            float z = 0.0f;
            
            // Main terrain waves
            z += 5.0f * std::sin(frequency1 * x);
            z += 3.0f * std::cos(frequency2 * y);
            
            // Add small variations
            z += 1.0f * std::sin(frequency1 * 3 * x + frequency2 * 2 * y);
            
            // Add central elevation
            float dx = x - width / 2.0f;
            float dy = y - height / 2.0f;
            float distance = std::sqrt(dx * dx + dy * dy);
            z += 2.0f * std::exp(-distance * 0.01f);
            
            // Store height value
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
        
        // Create shared memory manager
        const std::string memoryName = "TestSharedMemory";
        const size_t memorySize = 1024 * 1024 * 10; // 10MB
        
        // Create producer
        ShareMemoryManager producer(memoryName, memorySize);
        if (!producer.Initialize())
        {
            std::cerr << "Failed to initialize producer shared memory" << std::endl;
            return 1;
        }
        
        // Create consumer
        ShareMemoryManager consumer(memoryName, memorySize);
        if (!consumer.Initialize())
        {
            std::cerr << "Failed to initialize consumer shared memory" << std::endl;
            return 1;
        }

        // Set up consumer callback
        consumer.SetDataReceivedCallback([](const uint8_t* data, size_t size, uint32_t dataType, uint32_t width, uint32_t height) {
            std::cout << "\n[Consumer] Received data:"
                     << "\n - Type: " << (dataType == static_cast<uint32_t>(FrameType::HEIGHTMAP) ? "HeightMap" : 
                                        (dataType == static_cast<uint32_t>(FrameType::IMAGE) ? "Image" : "PointCloud"))
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
        
        int frameCount = 0;
        const int totalFrames = 10; // Send 10 frames and exit

        while (frameCount < totalFrames)
        {
            std::vector<uint8_t> data;
            DataInfo info = {};
            info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            if (frameCount % 3 == 0) {  // Send image data every three frames
                // Generate 640x480 RGB test image
                info.channels = 3; // RGB image
                info.width = 640;
                info.height = 480;
                info.dataType = static_cast<uint32_t>(FrameType::IMAGE);
                data = GenerateTestImage(info.width, info.height, info.channels);
                std::cout << "Preparing to write RGB image data..." << std::endl;
            } 
            else if (frameCount % 3 == 1) {  // Send point cloud data every three frames
                // Generate point cloud with 1000 points
                info.width = 1000;  // Number of points
                info.height = 3;    // XYZ dimensions
                info.dataType = static_cast<uint32_t>(FrameType::POINTCLOUD);
                data = GenerateTestPointCloud(info.width);
                std::cout << "Preparing to write XYZ point cloud data..." << std::endl;
            }
            else {  // Send height map data every three frames
                // Generate 200x200 height map
                info.width = 200;
                info.height = 200;
                info.xSpacing = 0.1f;
                info.ySpacing = 0.1f;
                info.dataType = static_cast<uint32_t>(FrameType::HEIGHTMAP);
                
                std::vector<float> heightData = GenerateTestHeightMap(info.width, info.height);
                
                // Convert to byte data
                data.resize(heightData.size() * sizeof(float));
                memcpy(data.data(), heightData.data(), data.size());
                std::cout << "Preparing to write height map data..." << std::endl;
            }

            // Write data
            bool writeSuccess = producer.WriteData(data.data(), data.size(), info);
            
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
