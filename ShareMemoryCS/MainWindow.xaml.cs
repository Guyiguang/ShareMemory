/**
 * @file MainWindow.xaml.cs
 * @brief 主窗口的代码，处理共享内存数据接收和显示
 * @author AI Assistant
 * @date 2024-04-02
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Windows.Threading;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace ShareMemoryCS
{
    /// <summary>
    /// MainWindow.xaml 的交互逻辑
    /// </summary>
    public partial class MainWindow : Window
    {
        private SharedMemoryManager _sharedMemory;
        private DispatcherTimer _statusTimer;
        private const int SHARED_MEMORY_SIZE = 1024 * 1024 * 10; // 10MB
        private const string SHARED_MEMORY_NAME = "TestSharedMemory";

        /// <summary>
        /// 主窗口构造函数
        /// </summary>
        public MainWindow()
        {
            InitializeComponent();
            InitializeSharedMemory();
            InitializeStatusTimer();
        }

        private void InitializeSharedMemory()
        {
            try
            {
                Log("正在初始化共享内存管理器...");
                _sharedMemory = new SharedMemoryManager(SHARED_MEMORY_NAME, SHARED_MEMORY_SIZE);
                _sharedMemory.DataReceived += SharedMemory_DataReceived;
                
                if (_sharedMemory.Initialize())
                {
                    Log("共享内存管理器初始化成功");
                    UpdateStatus("已连接");
                }
                else
                {
                    Log("共享内存管理器初始化失败");
                    UpdateStatus("连接失败");
                }
            }
            catch (Exception ex)
            {
                Log($"初始化共享内存时发生错误: {ex.Message}");
                UpdateStatus("初始化错误");
            }
        }

        private void InitializeStatusTimer()
        {
            _statusTimer = new DispatcherTimer();
            _statusTimer.Interval = TimeSpan.FromSeconds(1);
            _statusTimer.Tick += StatusTimer_Tick;
            _statusTimer.Start();
        }

        private void StatusTimer_Tick(object sender, EventArgs e)
        {
            try
            {
                // 更新UI状态
                if (_sharedMemory != null)
                {
                    txtStatus.Text = "已连接";
                }
                else
                {
                    txtStatus.Text = "未连接";
                }
            }
            catch (Exception ex)
            {
                Log($"更新状态时发生错误: {ex.Message}");
            }
        }

        private void SharedMemory_DataReceived(object sender, DataReceivedEventArgs e)
        {
            try
            {
                switch (e.DataType)
                {
                    case DataType.Image:
                        if (e.Width > 0 && e.Height > 0 && e.Data.Length == e.Width * e.Height * 3)
                        {
                            ProcessImageFrame(e.Data, (int)e.Width, (int)e.Height);
                            Log($"成功处理图像帧: {e.Width}x{e.Height}");
                        }
                        else
                        {
                            Log($"无效的图像数据: 宽度={e.Width}, 高度={e.Height}, 数据大小={e.Data.Length}");
                        }
                        break;

                    case DataType.PointCloud:
                        if (e.Width > 0 && e.Height > 0 && e.Data.Length == e.Width * e.Height)
                        {
                            ProcessPointCloudFrame(e.Data, (int)e.Width, (int)e.Height);
                            Log($"成功处理点云数据: 点数={e.Width}, 点大小={e.Height}字节");
                        }
                        else
                        {
                            Log($"无效的点云数据: 点数={e.Width}, 点大小={e.Height}, 数据大小={e.Data.Length}");
                        }
                        break;

                    default:
                        Log($"未知的数据类型: {e.DataType}");
                        break;
                }
            }
            catch (Exception ex)
            {
                Log($"处理数据帧时发生错误: {ex.Message}");
            }
        }

        private void ProcessImageFrame(byte[] data, int width, int height)
        {
            try
            {
                // 直接使用原始数据，不需要跳过宽度和高度信息
                byte[] imageData = new byte[width * height * 3];
                Array.Copy(data, 0, imageData, 0, imageData.Length);

                // 固定图像数据
                var handle = GCHandle.Alloc(imageData, GCHandleType.Pinned);
                try
                {
                    var bitmap = BitmapSource.Create(
                        width,
                        height,
                        96,
                        96,
                        PixelFormats.Rgb24,
                        null,
                        handle.AddrOfPinnedObject(),
                        imageData.Length,
                        width * 3);

                    // 在UI线程上更新图像
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        imgDisplay.Source = bitmap;
                    }));
                }
                finally
                {
                    handle.Free();
                }
            }
            catch (Exception ex)
            {
                Log($"处理图像帧时发生错误: {ex.Message}");
            }
        }

        private void ProcessPointCloudFrame(byte[] data, int pointCount, int pointSize)
        {
            // TODO: 实现点云数据的处理和显示
            // 这里可以添加点云数据的可视化逻辑
        }

        private void btnTestConnection_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Log("测试共享内存连接...");
                if (_sharedMemory != null)
                {
                    Log("共享内存管理器已初始化");
                }
                else
                {
                    Log("共享内存管理器未初始化");
                }
            }
            catch (Exception ex)
            {
                Log($"测试连接时发生错误: {ex.Message}");
            }
        }

        private void btnClearLog_Click(object sender, RoutedEventArgs e)
        {
            txtDebugLog.Clear();
        }

        private void Log(string message)
        {
            string logEntry = $"{DateTime.Now:HH:mm:ss.fff} - {message}";
            Dispatcher.BeginInvoke(new Action(() =>
            {
                txtDebugLog.AppendText(logEntry + Environment.NewLine);
                txtDebugLog.ScrollToEnd();
            }));
        }

        private void UpdateStatus(string status)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                txtStatus.Text = status;
            }));
        }

        protected override void OnClosed(EventArgs e)
        {
            _statusTimer?.Stop();
            _sharedMemory?.Dispose();
            base.OnClosed(e);
        }
    }

    /// <summary>
    /// 将控制台输出重定向到TextBox的辅助类
    /// </summary>
    public class TextBoxOutputWriter : TextWriter
    {
        private TextBox _textBox;

        public TextBoxOutputWriter(TextBox textBox)
        {
            _textBox = textBox;
        }

        public override void Write(char value)
        {
            base.Write(value);
            _textBox.Dispatcher.BeginInvoke(new Action(() =>
            {
                _textBox.AppendText(value.ToString());
                _textBox.ScrollToEnd();
            }));
        }

        public override void Write(string value)
        {
            base.Write(value);
            _textBox.Dispatcher.BeginInvoke(new Action(() =>
            {
                _textBox.AppendText(value);
                _textBox.ScrollToEnd();
            }));
        }

        public override Encoding Encoding
        {
            get { return Encoding.UTF8; }
        }
    }
}
