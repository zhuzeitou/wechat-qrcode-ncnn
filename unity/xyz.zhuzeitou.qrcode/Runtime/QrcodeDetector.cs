using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using ZZT.QRCode.Native;

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
            int err = Bridge.DetectAndDecode(_detector, path, out Bridge.NativeResult resultPtr);

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
                return await Task.Run(() => DetectAndDecodeSync(path));
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

            int err = Bridge.DetectAndDecode(_detector, in data[0], data.Length, out Bridge.NativeResult resultPtr);

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
                return await Task.Run(() => DetectAndDecodeSync(data));
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private QrcodeResults DetectAndDecodeInternal(Span<byte> pixels, PixelFormat format, int width,
            int height, int stride, bool flipVertical = false)
        {
            if (pixels.IsEmpty || width <= 0 || height <= 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            int rowBytes = width * GetBpp(format);
            int exactStride = stride <= 0 ? rowBytes : stride;
            int pixelBytes = pixels.Length;
            int expectedPixelBytes = exactStride * (height - 1) + rowBytes;
            if (pixelBytes < expectedPixelBytes)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            if (flipVertical)
            {
                byte[] rowBuffer = ArrayPool<byte>.Shared.Rent(rowBytes);
                Span<byte> tempRow = rowBuffer.AsSpan(0, rowBytes);

                try
                {
                    for (int y = 0; y < height / 2; y++)
                    {
                        int srcOffset = (height - 1 - y) * exactStride;
                        int dstOffset = y * exactStride;

                        Span<byte> srcRow = pixels.Slice(srcOffset, rowBytes);
                        Span<byte> dstRow = pixels.Slice(dstOffset, rowBytes);

                        srcRow.CopyTo(tempRow);
                        dstRow.CopyTo(srcRow);
                        tempRow.CopyTo(dstRow);
                    }
                }
                finally
                {
                    ArrayPool<byte>.Shared.Return(rowBuffer);
                }
            }

            int err = Bridge.DetectAndDecode(_detector, MemoryMarshal.GetReference(pixels),
                (int)format, width, height, stride, out Bridge.NativeResult resultPtr);

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
                return await Task.Run(() => DetectAndDecodeSync(pixels, format, width, height, stride));
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private bool GetTextureData(Texture2D texture, out byte[] raw, out Color32[] colors,
            out int width, out int height, out PixelFormat format)
        {
            width = 0;
            height = 0;
            raw = null;
            colors = null;
            format = default;

            if (!texture)
            {
                return false;
            }

            if (!texture.isReadable)
            {
                return false;
            }

            width = texture.width;
            height = texture.height;
            if (FormatMap.TryGetValue(texture.format, out format))
            {
                raw = texture.GetRawTextureData();
                return true;
            }
            else
            {
                colors = texture.GetPixels32();
                format = PixelFormat.RGBA;
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
            if (GetTextureData(texture, out var raw, out var colors, out var width, out var height,
                    out var format))
            {
                if (raw != null)
                {
                    return DetectAndDecodeInternal(raw, format, width, height, 0, true);
                }

                if (colors != null)
                {
                    return DetectAndDecodeInternal(MemoryMarshal.AsBytes(colors.AsSpan()), format, width, height, 0, true);
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
            if (GetTextureData(texture, out var raw, out var colors, out var width, out var height,
                    out var format))
            {
                await GlobalSemaphore.WaitAsync();
                try
                {
                    return await Task.Run(() =>
                    {
                        if (raw != null)
                        {
                            return DetectAndDecodeInternal(raw, format, width, height, 0, true);
                        }

                        if (colors != null)
                        {
                            return DetectAndDecodeInternal(MemoryMarshal.AsBytes(colors.AsSpan()), format, width, height, 0, true);
                        }

                        return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
                    });
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
