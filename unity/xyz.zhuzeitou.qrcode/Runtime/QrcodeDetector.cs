#define ZZT_QRCODE_DEBUG

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
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

        private static void FlipVertical(Span<byte> pixels, int width, int height, int bpp)
        {
#if ZZT_QRCODE_DEBUG
            Debug.Log($"DetectAndDecode texture flip {Now}");
#endif
            int rowBytes = width * bpp;
            byte[] rowBuffer = ArrayPool<byte>.Shared.Rent(rowBytes);

            try
            {
                Span<byte> tempRow = rowBuffer.AsSpan(0, rowBytes);

                for (int y = 0; y < height / 2; y++)
                {
                    int topRowIndex = (height - 1 - y) * rowBytes;
                    int bottomRowIndex = y * rowBytes;

                    Span<byte> topRow = pixels.Slice(topRowIndex, rowBytes);
                    Span<byte> bottomRow = pixels.Slice(bottomRowIndex, rowBytes);
                    topRow.CopyTo(tempRow);
                    bottomRow.CopyTo(topRow);
                    tempRow.CopyTo(bottomRow);
                }

#if ZZT_QRCODE_DEBUG
                Debug.Log($"DetectAndDecode texture flip done {Now}");
#endif
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(rowBuffer);
            }
        }

        private QrcodeResults DetectAndDecodeInternal<T>(Memory<T> data, PixelFormat format, int width,
            int height, int stride) where T : unmanaged
        {
            if (data.IsEmpty)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            int expectedStride = width * GetBpp(format);
            if (stride > 0 && stride < expectedStride)
            {
                Debug.LogWarning($"Stride {stride} may be too small, expected {expectedStride}");
            }

            Span<byte> pixels = MemoryMarshal.AsBytes(data.Span);
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

            return DetectAndDecodeInternal(pixels.AsMemory(), format, width, height, stride);
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

        private bool GetTexturePixels(Texture2D texture, out object pixels, out int width, out int height,
            out PixelFormat format)
        {
#if ZZT_QRCODE_DEBUG
            Debug.Log(
                $"DetectAndDecode texture read {Now} format={texture.format} width={texture.width} height={texture.height}");
#endif

            if (!texture)
            {
                Debug.LogError("Texture is null");
                width = default;
                height = default;
                pixels = default;
                format = default;
                return false;
            }

            if (!texture.isReadable)
            {
                Debug.LogError("Texture is not readable");
                width = default;
                height = default;
                pixels = default;
                format = default;
                return false;
            }

            width = texture.width;
            height = texture.height;
            if (FormatMap.TryGetValue(texture.format, out format))
            {
                Memory<byte> data = texture.GetRawTextureData();

#if ZZT_QRCODE_DEBUG
                Debug.Log($"DetectAndDecode texture read raw done {Now}");
#endif

                FlipVertical(data.Span, width, height, GetBpp(format));
                pixels = data;
                return true;
            }
            else
            {
                Memory<Color32> data = texture.GetPixels32();

#if ZZT_QRCODE_DEBUG
                Debug.Log($"DetectAndDecode texture read pixels done {Now}");
#endif

                FlipVertical(MemoryMarshal.AsBytes(data.Span), width, height, 4);
                pixels = data;
                return true;
            }
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
            if (GetTexturePixels(texture, out var pixels, out var width, out var height, out var format))
            {
                if (pixels is Memory<byte> memory)
                {
                    return DetectAndDecodeInternal(memory, format, width, height, 0);
                }

                if (pixels is Memory<Color32> color32)
                {
                    return DetectAndDecodeInternal(color32, format, width, height, 0);
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
            if (GetTexturePixels(texture, out var pixels, out var width, out var height, out var format))
            {
                if (pixels is Memory<byte> memory)
                {
                    await GlobalSemaphore.WaitAsync();
                    try
                    {
                        return DetectAndDecodeInternal(memory, format, width, height, 0);
                    }
                    finally
                    {
                        GlobalSemaphore.Release();
                    }
                }

                if (pixels is Memory<Color32> color32)
                {
                    await GlobalSemaphore.WaitAsync();
                    try
                    {
                        return DetectAndDecodeInternal(color32, format, width, height, 0);
                    }
                    finally
                    {
                        GlobalSemaphore.Release();
                    }
                }
            }

            return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
        }
    }
}