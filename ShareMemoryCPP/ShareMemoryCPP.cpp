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
        std::cout << "Shared Memory Producer Starting..." << std::endl;
        
        // Create shared memory manager
        const std::string memoryName = "TestSharedMemory";
        const size_t memorySize = 1024 * 1024 * 10; // 10MB
        ShareMemoryManager sharedMemory(memoryName, memorySize);
        
        if (!sharedMemory.Initialize())
        {
            std::cerr << "Failed to initialize shared memory" << std::endl;
            return 1;
        }
        
        std::cout << "Shared memory initialized successfully, starting test data generation..." << std::endl;
        
        bool isImage = true;
        while (true)
        {
            std::vector<uint8_t> data;
            if (isImage) {
                // Generate 640x480 RGB test image
                data = GenerateTestImage(640, 480, 3);
                std::cout << "Preparing to write image data..." << std::endl;
            } else {
                // Generate point cloud data with 1000 points
                data = GenerateTestPointCloud(1000);
                std::cout << "Preparing to write point cloud data..." << std::endl;
            }

            // Try to write data
            bool writeSuccess = sharedMemory.WriteData(data.data(), data.size());
            
            if (!writeSuccess) {
                // If write fails, wait and retry
                std::cout << "Write failed, waiting for data to be consumed..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Switch data type after successful write
            isImage = !isImage;
            
            // Wait before sending next frame
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
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
