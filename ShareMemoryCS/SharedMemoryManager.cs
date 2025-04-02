/**
 * @file SharedMemoryManager.cs
 * @brief C#端共享内存管理类，用于消费C++端生产的数据
 * @author AI Assistant
 * @date 2024-04-02
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.IO;
using System.Text;
using System.Diagnostics;

namespace ShareMemoryCS
{
    /// <summary>
    /// 帧数据类型枚举
    /// </summary>
    public enum FrameType
    {
        /// <summary>图像数据</summary>
        Image = 0,
        /// <summary>点云数据</summary>
        PointCloud = 1
    }

    /// <summary>
    /// 帧头部信息结构，与C++端结构对应
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FrameHeader
    {
        /// <summary>帧ID</summary>
        public ulong FrameId;
        /// <summary>时间戳，毫秒</summary>
        public ulong Timestamp;
        /// <summary>帧类型</summary>
        public uint FrameType;
        /// <summary>数据大小（字节）</summary>
        public uint DataSize;
        
        // 图像特有参数
        /// <summary>图像宽度</summary>
        public uint Width;
        /// <summary>图像高度</summary>
        public uint Height;
        /// <summary>图像通道数</summary>
        public uint Channels;
        
        // 点云特有参数
        /// <summary>点云中点的数量</summary>
        public uint PointCount;
        /// <summary>每个点的字节大小</summary>
        public uint PointSize;
    }

    /// <summary>
    /// 控制块结构，存储环形缓冲区的元数据
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct ControlBlock
    {
        /// <summary>总缓冲区大小（字节）</summary>
        public uint BufferSize;
        /// <summary>最大可存储帧数</summary>
        public uint MaxFrames;
        /// <summary>写入位置索引</summary>
        public uint WriteIndex;
        /// <summary>读取位置索引</summary>
        public uint ReadIndex;
        /// <summary>当前缓冲区中的帧数量</summary>
        public uint FrameCount;
        /// <summary>最后写入的帧ID</summary>
        public ulong LastFrameId;
    }

    /// <summary>
    /// 共享内存头部结构
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct SharedMemoryHeader
    {
        public uint Magic;          // Magic number for validation (0x12345678)
        public uint Status;         // 0=Empty, 1=Writing, 2=Ready, 3=Error
        public uint DataSize;       // Size of data in bytes
        public uint Checksum;       // Simple checksum for data validation
        public uint FrameId;        // Incremental frame ID
        public uint DataType;       // 0=Image, 1=PointCloud
        public uint Width;          // Image width or point count
        public uint Height;         // Image height or point size
        public uint Reserved;       // Reserved for future use
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
        public byte[] ErrorMsg;     // Error message
    }

    /// <summary>
    /// 数据类型枚举
    /// </summary>
    public enum DataType
    {
        /// <summary>图像数据</summary>
        Image = 0,
        /// <summary>点云数据</summary>
        PointCloud = 1
    }

    /// <summary>
    /// 数据接收事件参数类
    /// </summary>
    public class DataReceivedEventArgs : EventArgs
    {
        /// <summary>帧ID</summary>
        public uint FrameId { get; set; }
        /// <summary>数据类型</summary>
        public DataType DataType { get; set; }
        /// <summary>接收到的数据</summary>
        public byte[] Data { get; set; }
        /// <summary>宽度（图像）或点数量（点云）</summary>
        public uint Width { get; set; }
        /// <summary>高度（图像）或点大小（点云）</summary>
        public uint Height { get; set; }
    }

    /// <summary>
    /// 共享内存管理器类
    /// </summary>
    public class SharedMemoryManager : IDisposable
    {
        #region Win32 API

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr OpenFileMapping(uint dwDesiredAccess, bool bInheritHandle, string lpName);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr MapViewOfFile(IntPtr hFileMappingObject, uint dwDesiredAccess, 
            uint dwFileOffsetHigh, uint dwFileOffsetLow, UIntPtr dwNumberOfBytesToMap);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UnmapViewOfFile(IntPtr lpBaseAddress);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr OpenMutex(uint dwDesiredAccess, bool bInheritHandle, string lpName);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReleaseMutex(IntPtr hMutex);

        private const uint FILE_MAP_ALL_ACCESS = 0xF001F;
        private const uint MUTEX_ALL_ACCESS = 0x1F0001;
        private const uint WAIT_OBJECT_0 = 0;
        private const uint MAGIC_NUMBER = 0x12345678;

        #endregion

        /// <summary>
        /// 数据接收事件
        /// </summary>
        public event EventHandler<DataReceivedEventArgs> DataReceived;

        private string _name;
        private int _size;
        private IntPtr _hMapFile = IntPtr.Zero;
        private IntPtr _pBuffer = IntPtr.Zero;
        private IntPtr _hMutex = IntPtr.Zero;
        private Thread _monitorThread;
        private volatile bool _isRunning;
        private readonly StreamWriter _logWriter;

        /// <summary>
        /// 构造函数
        /// </summary>
        /// <param name="name">共享内存名称</param>
        /// <param name="size">共享内存大小</param>
        public SharedMemoryManager(string name, int size)
        {
            _name = name;
            _size = size + Marshal.SizeOf<SharedMemoryHeader>();
            _logWriter = new StreamWriter("consumer_log.txt", true) { AutoFlush = true };
            Log("SharedMemoryManager constructed");
        }

        /// <summary>
        /// 初始化共享内存
        /// </summary>
        /// <returns>是否成功初始化</returns>
        public bool Initialize()
        {
            Log("Initializing shared memory");

            // Open mutex
            _hMutex = OpenMutex(MUTEX_ALL_ACCESS, false, _name + "_mutex");
            if (_hMutex == IntPtr.Zero)
            {
                Log($"Failed to open mutex: {Marshal.GetLastWin32Error()}");
                return false;
            }

            // Open shared memory
            _hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, _name);
            if (_hMapFile == IntPtr.Zero)
            {
                Log($"Failed to open file mapping: {Marshal.GetLastWin32Error()}");
                return false;
            }

            // Map memory view
            _pBuffer = MapViewOfFile(_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, (UIntPtr)_size);
            if (_pBuffer == IntPtr.Zero)
            {
                Log($"Failed to map view of file: {Marshal.GetLastWin32Error()}");
                return false;
            }

            // Start monitor thread
            _isRunning = true;
            _monitorThread = new Thread(MonitorThreadProc)
            {
                IsBackground = true,
                Name = "SharedMemoryMonitor"
            };
            _monitorThread.Start();

            Log("Shared memory initialized successfully");
            return true;
        }

        private void MonitorThreadProc()
        {
            Log("Monitor thread started");
            
            while (_isRunning)
            {
                try
                {
                    // Log memory status periodically for debugging
                    if (_pBuffer != IntPtr.Zero)
                    {
                        var header = Marshal.PtrToStructure<SharedMemoryHeader>(_pBuffer);
                        Log($"Memory status check - Magic: {header.Magic:X8}, Status: {header.Status}, FrameId: {header.FrameId}");
                    }

                    if (TryReadData(out byte[] data, out uint frameId))
                    {
                        var header = Marshal.PtrToStructure<SharedMemoryHeader>(_pBuffer);
                        OnDataReceived(new DataReceivedEventArgs
                        {
                            FrameId = frameId,
                            DataType = (DataType)header.DataType,
                            Data = data,
                            Width = header.Width,
                            Height = header.Height
                        });
                    }

                    // Sleep for a short time to prevent CPU overuse
                    Thread.Sleep(10);
                }
                catch (Exception ex)
                {
                    Log($"Monitor thread error: {ex.Message}");
                    Thread.Sleep(100); // Longer sleep on error
                }
            }
            
            Log("Monitor thread stopped");
        }

        private bool TryReadData(out byte[] data, out uint frameId)
        {
            data = null;
            frameId = 0;

            uint waitResult = WaitForSingleObject(_hMutex, 1000);
            if (waitResult != WAIT_OBJECT_0)
            {
                Log($"Failed to acquire mutex: {waitResult}");
                return false;
            }

            try
            {
                var header = Marshal.PtrToStructure<SharedMemoryHeader>(_pBuffer);

                // Validate header
                if (header.Magic != MAGIC_NUMBER)
                {
                    Log($"Invalid magic number in header: {header.Magic:X8}, expected: {MAGIC_NUMBER:X8}");
                    return false;
                }

                // Log current status for debugging
                Log($"Current memory status: {header.Status}, DataSize: {header.DataSize}, FrameId: {header.FrameId}");

                // Check if data is ready
                if (header.Status != 2) // Ready
                {
                    // Only log when status changes to avoid spam
                    if (header.Status != 0) // Don't log Empty status
                    {
                        Log($"Data not ready, current status: {header.Status}");
                    }
                    return false;
                }

                // Read data
                data = new byte[header.DataSize];
                Marshal.Copy(IntPtr.Add(_pBuffer, Marshal.SizeOf<SharedMemoryHeader>()), 
                    data, 0, (int)header.DataSize);

                // Validate checksum
                uint checksum = CalculateChecksum(data);
                if (checksum != header.Checksum)
                {
                    Log($"Checksum mismatch. Expected: {header.Checksum}, Got: {checksum}");
                    return false;
                }

                frameId = header.FrameId;

                string dataTypeStr = header.DataType == 0 ? "Image" : "PointCloud";
                Log($"Successfully read {dataTypeStr} data - Size: {data.Length} bytes, Frame ID: {frameId}, " +
                    $"Width: {header.Width}, Height: {header.Height}");

                // Mark memory as empty
                header.Status = 0; // Empty
                Marshal.StructureToPtr(header, _pBuffer, false);

                return true;
            }
            catch (Exception ex)
            {
                Log($"Error reading data: {ex.Message}");
                return false;
            }
            finally
            {
                ReleaseMutex(_hMutex);
            }
        }

        private uint CalculateChecksum(byte[] data)
        {
            uint checksum = 0;
            for (int i = 0; i < data.Length; i++)
            {
                checksum = ((checksum << 5) + checksum) + data[i];
            }
            return checksum;
        }

        protected virtual void OnDataReceived(DataReceivedEventArgs args)
        {
            try
            {
                DataReceived?.Invoke(this, args);
            }
            catch (Exception ex)
            {
                Log($"数据接收事件处理器发生错误: {ex.Message}");
            }
        }

        public void Dispose()
        {
            Log("SharedMemoryManager disposing...");
            
            _isRunning = false;
            
            if (_monitorThread != null && _monitorThread.IsAlive)
            {
                _monitorThread.Join(1000);
            }

            if (_pBuffer != IntPtr.Zero)
            {
                UnmapViewOfFile(_pBuffer);
                _pBuffer = IntPtr.Zero;
            }

            if (_hMapFile != IntPtr.Zero)
            {
                CloseHandle(_hMapFile);
                _hMapFile = IntPtr.Zero;
            }

            if (_hMutex != IntPtr.Zero)
            {
                CloseHandle(_hMutex);
                _hMutex = IntPtr.Zero;
            }

            // 最后再关闭日志文件
            if (_logWriter != null)
            {
                try
                {
                    _logWriter.WriteLine($"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [Consumer] SharedMemoryManager disposed");
                    _logWriter.Flush();
                    _logWriter.Dispose();
                }
                catch (ObjectDisposedException)
                {
                    // Ignore if already disposed
                }
            }
        }

        private void Log(string message)
        {
            string logEntry = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [Consumer] {message}";
            Console.WriteLine(logEntry);
            _logWriter.WriteLine(logEntry);
        }
    }
} 