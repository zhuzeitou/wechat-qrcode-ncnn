#define ZZT_QRCODE_DEBUG

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Unity.Collections;
using UnityEngine;
using ZZT.QRCode.Native;
using Debug = UnityEngine.Debug;

#if ZZT_QRCODE_DEBUG
using System.Diagnostics;
#endif

namespace ZZT.QRCode
{
    /// <summary>
    /// QR Code Detector.
    /// <para>
    /// This class provides methods to detect and decode QR codes from image files, textures or raw pixel data.
    /// It wraps the native C++ implementation.
    /// </para>
    /// </summary>
    public class QrcodeDetector : IDisposable
    {
        /// <summary>
        /// Pixel format definition.
        /// </summary>
        public enum PixelFormat
        {
            /// <summary>Single channel Gray</summary>
            Gray = 0,
            /// <summary>3 channels RGB</summary>
            RGB = 1,
            /// <summary>3 channels BGR</summary>
            BGR = 2,
            /// <summary>4 channels RGBA</summary>
            RGBA = 3,
            /// <summary>4 channels BGRA</summary>
            BGRA = 4,
            /// <summary>4 channels ARGB</summary>
            ARGB = 5,
            /// <summary>4 channels ABGR</summary>
            ABGR = 6
        }

        private static readonly Dictionary<TextureFormat, PixelFormat> FormatMap = new()
        {
            { TextureFormat.Alpha8, PixelFormat.Gray },
            { TextureFormat.R8, PixelFormat.Gray },
            { TextureFormat.RGB24, PixelFormat.RGB },
            { TextureFormat.RGBA32, PixelFormat.RGBA },
            { TextureFormat.BGRA32, PixelFormat.BGRA },
            { TextureFormat.ARGB32, PixelFormat.ARGB }
        };

        private static int GetBpp(PixelFormat fmt)
        {
            switch (fmt)
            {
                case PixelFormat.Gray: return 1;
                case PixelFormat.RGB: return 3;
                case PixelFormat.BGR: return 3;
                case PixelFormat.RGBA: return 4;
                case PixelFormat.BGRA: return 4;
                case PixelFormat.ARGB: return 4;
                case PixelFormat.ABGR: return 4;
                default: return 0;
            }
        }

        private Bridge.NativeDetector _detector;

        private static readonly SemaphoreSlim GlobalSemaphore = new(Math.Max(1, Environment.ProcessorCount - 1));

#if ZZT_QRCODE_DEBUG
        private static readonly Stopwatch Stopwatch = Stopwatch.StartNew();
        private static double Now => (double)Stopwatch.ElapsedTicks / Stopwatch.Frequency;
#endif

        /// <summary>
        /// Creates a new QrcodeDetector instance.
        /// <para>
        /// Initializes the native detector.
        /// </para>
        /// </summary>
        public QrcodeDetector()
        {
            _detector = Bridge.CreateDetector();
        }

        /// <summary>
        /// Releases the native detector resources.
        /// <para>
        /// Should be called when the detector is no longer needed to prevent memory leaks.
        /// </para>
        /// </summary>
        public void Dispose()
        {
            Bridge.ReleaseDetector(_detector);
        }

        /// <summary>
        /// Detects and decodes a QR code from an image file path synchronously.
        /// </summary>
        /// <param name="path">The path to the image file.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(string path)
        {
#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode path detect {Now} path={path}");
#endif

            int err = Bridge.DetectAndDecode(_detector, path, out Bridge.NativeResult resultPtr);
#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode path detect done {Now}");
#endif

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from an image file path asynchronously.
        /// </summary>
        /// <param name="path">The path to the image file.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(string path)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return DetectAndDecodeSync(path);
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        /// <summary>
        /// Detects and decodes a QR code from image file data (byte array) synchronously.
        /// </summary>
        /// <param name="data">The image file data (e.g., JPEG or PNG bytes).</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(byte[] data)
        {
            if (data == null || data.Length == 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }
#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode data detect {Now}");
#endif

            int err = Bridge.DetectAndDecode(_detector, in data[0], data.Length, out Bridge.NativeResult resultPtr);

#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode data detect done {Now}");
#endif

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from image file data (byte array) asynchronously.
        /// </summary>
        /// <param name="data">The image file data (e.g., JPEG or PNG bytes).</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(byte[] data)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return DetectAndDecodeSync(data);
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private QrcodeResults DetectAndDecodeInternal(Span<byte> pixels, PixelFormat format, int width,
            int height, int stride)
        {
            if (pixels.IsEmpty || width <= 0 || height <= 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            int expectedStride = width * GetBpp(format);
            if (stride > 0 && stride < expectedStride)
            {
                Debug.LogWarning($"Stride {stride} may be too small, expected {expectedStride}");
            }

            int exactStride = stride <= 0 ? width * GetBpp(format) : stride;
            int pixelBytes = pixels.Length;
            if (pixelBytes < exactStride * height)
            {
                Debug.LogError($"Invalid pixel data size: {pixelBytes}, expected: {exactStride * height}");
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode pixel detect {Now}");
#endif

            int err = Bridge.DetectAndDecode(_detector, MemoryMarshal.GetReference(pixels),
                (int)format, width, height, stride, out Bridge.NativeResult resultPtr);

#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode pixel detect done {Now}");
#endif

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from raw pixel data synchronously.
        /// </summary>
        /// <param name="pixels">The raw pixel data.</param>
        /// <param name="format">The pixel format.</param>
        /// <param name="width">The image width.</param>
        /// <param name="height">The image height.</param>
        /// <param name="stride">The image row stride (bytes). If 0, it is automatically calculated based on width and format.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(byte[] pixels, PixelFormat format, int width, int height,
            int stride)
        {
            if (pixels == null || pixels.Length == 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            return DetectAndDecodeInternal(pixels, format, width, height, stride);
        }

        /// <summary>
        /// Detects and decodes a QR code from raw pixel data asynchronously.
        /// </summary>
        /// <param name="pixels">The raw pixel data.</param>
        /// <param name="format">The pixel format.</param>
        /// <param name="width">The image width.</param>
        /// <param name="height">The image height.</param>
        /// <param name="stride">The image row stride (bytes). If 0, it is automatically calculated based on width and format.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(byte[] pixels, PixelFormat format, int width, int height,
            int stride)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return DetectAndDecodeSync(pixels, format, width, height, stride);
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private bool GetTextureData(Texture2D texture, out NativeArray<byte> nativeArray, out Color32[] colors,
            out int width, out int height, out PixelFormat format)
        {
            width = 0;
            height = 0;
            nativeArray = default;
            colors = null;
            format = default;

            if (!texture)
            {
                Debug.LogError("Texture is null");
                return false;
            }

            if (!texture.isReadable)
            {
                Debug.LogError("Texture is not readable");
                return false;
            }
            
#if ZZT_QRCODE_DEBUG
            Debug.Log(
                $"DetectAndDecode texture read {Now} format={texture.format} width={texture.width} height={texture.height}");
#endif

            width = texture.width;
            height = texture.height;
            if (FormatMap.TryGetValue(texture.format, out format))
            {
                nativeArray = texture.GetRawTextureData<byte>();

#if ZZT_QRCODE_DEBUG
                Debug.Log($"DetectAndDecode texture read raw done {Now}");
#endif

                return true;
            }
            else
            {
                colors = texture.GetPixels32();
                format = PixelFormat.RGBA;

#if ZZT_QRCODE_DEBUG
                Debug.Log($"DetectAndDecode texture read pixels done {Now}");
#endif

                return true;
            }
        }

        private static void FlipVertical(Span<byte> pixels, int width, int height, int bpp, out NativeArray<byte> output)
        {
#if ZZT_QRCODE_DEBUG
            Debug.Log($"FlipVertical {Now}");
#endif
            
            int rowBytes = width * bpp;
            
            output = new NativeArray<byte>(rowBytes * height, Allocator.Temp);
            Span<byte> outputSpan = output.AsSpan();

            for (int y = 0; y < height; y++)
            {
                int srcOffset = (height - 1 - y) * rowBytes;
                int dstOffset = y * rowBytes;
                pixels.Slice(srcOffset, rowBytes).CopyTo(outputSpan.Slice(dstOffset, rowBytes));
            }

#if ZZT_QRCODE_DEBUG
            Debug.Log($"FlipVertical done {Now}");
#endif
        }
        
        /// <summary>
        /// Detects and decodes a QR code from a Texture2D synchronously.
        /// <para>
        /// The texture must be readable.
        /// </para>
        /// <para>
        /// <b>Must be called from the main thread.</b>
        /// </para>
        /// </summary>
        /// <param name="texture">The Unity Texture2D.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(Texture2D texture)
        {
            if (GetTextureData(texture, out var nativeArray, out var colors, out var width, out var height,
                    out var format))
            {
                NativeArray<byte> pixels = default;
                if (nativeArray.IsCreated)
                {
                    FlipVertical(nativeArray.AsSpan(), width, height, GetBpp(format), out pixels);
                }
                else if (colors != null)
                {
                    FlipVertical(MemoryMarshal.AsBytes(colors.AsSpan()), width, height, 4, out pixels);  
                }
                
                using (pixels)
                {
                    return DetectAndDecodeInternal(pixels, format, width, height, 0);
                }
            }

            return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
        }

        /// <summary>
        /// Detects and decodes a QR code from a Texture2D asynchronously.
        /// <para>
        /// The texture must be readable.
        /// </para>
        /// <para>
        /// <b>Must be called from the main thread.</b>
        /// </para>
        /// </summary>
        /// <param name="texture">The Unity Texture2D.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(Texture2D texture)
        {
            if (GetTextureData(texture, out var nativeArray, out var colors, out var width, out var height,
                    out var format))
            {
                await GlobalSemaphore.WaitAsync();

                try
                {
                    NativeArray<byte> pixels = default;
                    if (nativeArray.IsCreated)
                    {
                        FlipVertical(nativeArray.AsSpan(), width, height, GetBpp(format), out pixels);
                    }
                    else if (colors != null)
                    {
                        FlipVertical(MemoryMarshal.AsBytes(colors.AsSpan()), width, height, 4, out pixels);
                    }

                    using (pixels)
                    {
                        return DetectAndDecodeInternal(pixels, format, width, height, 0);
                    }
                }
                finally
                {
                    GlobalSemaphore.Release();
                }
            }

            return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
        }
    }
}