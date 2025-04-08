/**
 * @file ShareMemoryCPP.cpp
 * @brief C++ Shared Memory Test Program
 * @author AI Assistant
 * @date 2024-04-02
 */

#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include "ShareMemoryManager.h"

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
                     << "\n - Type: " << (dataType == 0 ? "Image" : "PointCloud")
                     << "\n - Size: " << size << " bytes"
                     << "\n - Width: " << width
                     << "\n - Height: " << height
                     << "\n - First byte: 0x" << std::hex << (int)data[0] 
                     << std::dec << std::endl;
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
            
            if (isImage) {
                // Generate 640x480 RGB test image
                channels = 3; // RGB image
                data = GenerateTestImage(640, 480, channels);
                dataType = 0; // Image
                width = 640;
                height = 480;
                dimensions = 0; // Not used for images
                std::cout << "Preparing to write RGB image data..." << std::endl;
            } else {
                // Generate point cloud data with 1000 points
                dimensions = 3; // XYZ point cloud
                data = GenerateTestPointCloud(1000);
                dataType = 1; // PointCloud
                width = 1000; // Number of points
                height = sizeof(float); // Size per component
                channels = 0; // Not used for point clouds
                std::cout << "Preparing to write XYZ point cloud data..." << std::endl;
            }

            // Try to write data
            bool writeSuccess = producer.WriteData(data.data(), dataType, width, height, channels, dimensions);
            
            if (writeSuccess) {
                frameCount++;
                isImage = !isImage;
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
