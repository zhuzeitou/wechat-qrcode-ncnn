using System;
using System.Buffers;
using ZZT.QRCode.Native;

namespace ZZT.QRCode
{
    /// <summary>
    /// QR Code detection results.
    /// <para>
    /// Contains the list of detected QR codes and the error code if any.
    /// </para>
    /// </summary>
    public class QrcodeResults
    {
        /// <summary>
        /// Point coordinates of the QR code position.
        /// </summary>
        public class ResultPoint
        {
            /// <summary>X coordinate.</summary>
            public float X { get; }
            /// <summary>Y coordinate.</summary>
            public float Y { get; }

            internal ResultPoint(float x, float y)
            {
                X = x;
                Y = y;
            }

            public override string ToString()
            {
                return $"({X}, {Y})";
            }
        }

        /// <summary>
        /// Single QR Code result.
        /// </summary>
        public class QrcodeResult
        {
            /// <summary>Decoded text content.</summary>
            public string Text { get; }
            /// <summary>Corner points of the QR code.</summary>
            public ResultPoint[] Points { get; }

            internal QrcodeResult(string text, ResultPoint[] points)
            {
                Text = text;
                Points = points;
            }

            public override string ToString()
            {
                return $"Text: {Text}, Points: [{string.Join<ResultPoint>(", ", Points)}]";
            }
        }

        /// <summary>
        /// Error codes for QR code detection.
        /// </summary>
        public enum ErrorCode
        {
            /// <summary>Success</summary>
            OK = 0,
            /// <summary>Invalid handle</summary>
            ErrorInvalidHandle = -1,
            /// <summary>Invalid index</summary>
            ErrorInvalidIndex = -2,
            /// <summary>Buffer too small</summary>
            ErrorBufferTooSmall = -3,
            /// <summary>Image decode failed</summary>
            ErrorDecodeFailed = -4,
            /// <summary>Invalid argument (e.g. null pointer or invalid size)</summary>
            ErrorInvalidArgument = -5,
            /// <summary>Out of memory</summary>
            ErrorOutOfMemory = -6,
        }

        /// <summary>Get detected QR codes. Empty if an error occurred.</summary>
        public QrcodeResult[] Results { get; }
        /// <summary>Error code of the operation.</summary>
        public ErrorCode Error { get; }

        private QrcodeResults(QrcodeResult[] results, ErrorCode error)
        {
            Results = results;
            Error = error;
        }

        public override string ToString()
        {
            if (Error != ErrorCode.OK)
            {
                return $"Error: {Error}";
            }

            return $"Results: [{string.Join<QrcodeResult>(", ", Results)}]";
        }

        internal static QrcodeResults FromErrorCode(ErrorCode errorCode)
        {
            return new QrcodeResults(Array.Empty<QrcodeResult>(), errorCode);
        }

        internal static QrcodeResults FromResults(QrcodeResult[] results)
        {
            return new QrcodeResults(results, ErrorCode.OK);
        }

        internal static QrcodeResults Parse(Bridge.NativeResult resultPtr, int err)
        {
            if (err != 0)
            {
                return FromErrorCode((ErrorCode)err);
            }

            int resultSize = 0;
            err = Bridge.GetResultSize(resultPtr, ref resultSize);
            if (err != 0 || resultSize <= 0)
            {
                return FromErrorCode((ErrorCode)err);
            }

            QrcodeResult[] ret = new QrcodeResult[resultSize];

            for (int i = 0; i < resultSize; i++)
            {
                string text = string.Empty;
                {
                    int bufSize = 0;
                    err = Bridge.GetResultText(resultPtr, i, null, ref bufSize);
                    if (err != 0)
                    {
                        return FromErrorCode((ErrorCode)err);
                    }

                    if (bufSize > 0)
                    {
                        byte[] textBuf = ArrayPool<byte>.Shared.Rent(bufSize);
                        try
                        {
                            bufSize = textBuf.Length;
                            err = Bridge.GetResultText(resultPtr, i, textBuf, ref bufSize);
                            if (err != 0)
                            {
                                return FromErrorCode((ErrorCode)err);
                            }

                            if (bufSize > 0)
                            {
                                int textLen = bufSize - 1;
                                text = System.Text.Encoding.UTF8.GetString(textBuf, 0, textLen);
                            }
                        }
                        finally
                        {
                            ArrayPool<byte>.Shared.Return(textBuf);
                        }
                    }
                }

                ResultPoint[] resultPoints = Array.Empty<ResultPoint>();
                {
                    int count = 0;
                    err = Bridge.GetResultPoints(resultPtr, i, null, ref count);
                    if (err != 0)
                    {
                        return FromErrorCode((ErrorCode)err);
                    }

                    if (count > 0)
                    {
                        float[] points = ArrayPool<float>.Shared.Rent(count);
                        try
                        {
                            count = points.Length;
                            err = Bridge.GetResultPoints(resultPtr, i, points, ref count);
                            if (err != 0)
                            {
                                return FromErrorCode((ErrorCode)err);
                            }

                            if (count > 0)
                            {
                                int pointCount = count / 2;
                                resultPoints = new ResultPoint[pointCount];
                                for (int j = 0; j < pointCount; j++)
                                {
                                    resultPoints[j] = new ResultPoint(points[j * 2], points[j * 2 + 1]);
                                }
                            }
                        }
                        finally
                        {
                            ArrayPool<float>.Shared.Return(points);
                        }
                    }
                }

                ret[i] = new QrcodeResult(text, resultPoints);
            }

            return FromResults(ret);
        }
    }
}