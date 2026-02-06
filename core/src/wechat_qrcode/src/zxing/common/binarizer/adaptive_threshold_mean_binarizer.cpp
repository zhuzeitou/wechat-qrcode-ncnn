// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#include "../../../precomp.hpp"
#include "adaptive_threshold_mean_binarizer.hpp"
using zxing::AdaptiveThresholdMeanBinarizer;

namespace {
const int BLOCK_SIZE = 25;
const int Bias = 10;
}  // namespace

// Generate a 1D Gaussian kernel
static double *generateGaussianKernel(int kernelSize) {
    double sigma = 0.3 * (((kernelSize - 1) / 2.0) - 1) + 0.8;
    if (sigma < 0.1) sigma = 0.1; // 确保 sigma 不会太小

    double *kernel = new double[kernelSize];
    double sum = 0.0;
    int center = kernelSize / 2;

    for (int x = 0; x < kernelSize; ++x) {
        double val = std::exp(-0.5 * std::pow((x - center) / sigma, 2));
        kernel[x] = val;
        sum += val;
    }

    // Normalize the Gaussian kernel
    for (int x = 0; x < kernelSize; ++x) {
        kernel[x] /= sum;
    }
    return kernel;
}

// 1D convolution
static double *convolve1D(const double *signal, int signalSize, const double *kernel, int kernelSize) {
    int padding = kernelSize / 2;
    double *paddedSignal = new double[signalSize + 2 * padding];
    // Border padding, here using the method of copying edge pixels
    for (int i = 0; i < padding; ++i) {
        paddedSignal[i] = signal[0];
    }
    for (int i = 0; i < signalSize; ++i) {
        paddedSignal[padding + i] = signal[i];
    }
    for (int i = 0; i < padding; ++i) {
        paddedSignal[padding + signalSize + i] = signal[signalSize - 1];
    }

    double *output = new double[signalSize];
    for (int i = 0; i < signalSize; ++i) {
        output[i] = 0.0;
        for (int j = 0; j < kernelSize; ++j) {
            output[i] += paddedSignal[i + j] * kernel[j];
        }
    }
    delete[] paddedSignal;
    return output;
}

// 2D Gaussian blur
static double *gaussianBlur(const double *image, int rows, int cols, int kernelSize) {
    double *kernel = generateGaussianKernel(kernelSize);

    // Horizontal blur
    double *blurredHorizontally = new double[rows * cols];
    for (int i = 0; i < rows; ++i) {
        double *row = new double[cols];
        for (int j = 0; j < cols; ++j) row[j] = image[i * cols + j];
        double *blurredRow = convolve1D(row, cols, kernel, kernelSize);
        for (int j = 0; j < cols; ++j) blurredHorizontally[i * cols + j] = blurredRow[j];
        delete[] row;
        delete[] blurredRow;
    }

    // Vertical blur
    double *blurredImage = new double[rows * cols];
    for (int j = 0; j < cols; ++j) {
        double *column = new double[rows];
        for (int i = 0; i < rows; ++i) column[i] = blurredHorizontally[i * cols + j];
        double *blurredColumn = convolve1D(column, rows, kernel, kernelSize);
        for (int i = 0; i < rows; ++i) blurredImage[i * cols + j] = blurredColumn[i];
        delete[] column;
        delete[] blurredColumn;
    }

    delete[] kernel;
    delete[] blurredHorizontally;
    return blurredImage;
}

// Gaussian adaptive thresholding
static void adaptiveThresholdGaussian(const unsigned char *grayImage, unsigned char *outputImage, int rows, int cols,
                                      int kernelSize, int C) {
    double *doubleImage = new double[rows * cols];
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            doubleImage[i * cols + j] = static_cast<double>(grayImage[i * cols + j]);
        }
    }

    double *blurredImage = gaussianBlur(doubleImage, rows, cols, kernelSize);

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            double thresholdValue = blurredImage[i * cols + j] - C;
            outputImage[i * cols + j] = (grayImage[i * cols + j] > thresholdValue) ? 255 : 0;
        }
    }

    delete[] doubleImage;
    delete[] blurredImage;
}

AdaptiveThresholdMeanBinarizer::AdaptiveThresholdMeanBinarizer(Ref<LuminanceSource> source)
    : GlobalHistogramBinarizer(source) {}

AdaptiveThresholdMeanBinarizer::~AdaptiveThresholdMeanBinarizer() {}

Ref<Binarizer> AdaptiveThresholdMeanBinarizer::createBinarizer(Ref<LuminanceSource> source) {
    return Ref<Binarizer>(new AdaptiveThresholdMeanBinarizer(source));
}

Ref<BitArray> AdaptiveThresholdMeanBinarizer::getBlackRow(int y, Ref<BitArray> row,
                                                          ErrorHandler& err_handler) {
    // First call binarize image in child class to get matrix0_ and binCache
    if (!matrix0_) {
        binarizeImage(err_handler);
        if (err_handler.ErrCode()) return Ref<BitArray>();
    }

    // Call parent getBlackMatrix to get current matrix
    return Binarizer::getBlackRow(y, row, err_handler);
}

Ref<BitMatrix> AdaptiveThresholdMeanBinarizer::getBlackMatrix(ErrorHandler& err_handler) {
    // First call binarize image in child class to get matrix0_ and binCache
    if (!matrix0_) {
        binarizeImage(err_handler);
        if (err_handler.ErrCode()) return Ref<BitMatrix>();
    }
    return Binarizer::getBlackMatrix(err_handler);
}

int AdaptiveThresholdMeanBinarizer::binarizeImage(ErrorHandler& err_handler) {
    if (width >= BLOCK_SIZE && height >= BLOCK_SIZE) {
        LuminanceSource& source = *getLuminanceSource();
        Ref<BitMatrix> matrix(new BitMatrix(width, height, err_handler));
        if (err_handler.ErrCode()) return -1;
        auto src = (unsigned char*)source.getMatrix()->data();
        auto dst = matrix->getPtr();
        cv::Mat mDst;
        mDst.create(height, width, CV_8UC1);
        TransBufferToMat(src, mDst, width, height);
        cv::Mat result;
        result.create(height, width, CV_8UC1);
        int bs = width / 10;
        bs = bs + bs % 2 - 1;
        if (!(bs % 2 == 1 && bs > 1)) return -1;
        adaptiveThresholdGaussian(mDst.data, result.data, height, width, bs, Bias);
        TransMatToBuffer(result, dst, width, height);
        if (err_handler.ErrCode()) return -1;
        matrix0_ = matrix;
    } else {
        matrix0_ = GlobalHistogramBinarizer::getBlackMatrix(err_handler);
        if (err_handler.ErrCode()) return 1;
    }
    return 0;
}

int AdaptiveThresholdMeanBinarizer::TransBufferToMat(unsigned char* pBuffer, cv::Mat& mDst,
                                                     int nWidth, int nHeight) {
    for (int j = 0; j < nHeight; ++j) {
        unsigned char* data = mDst.ptr<unsigned char>(j);
        unsigned char* pSubBuffer = pBuffer + (nHeight - 1 - j) * nWidth;
        memcpy(data, pSubBuffer, nWidth);
    }
    return 0;
}

int AdaptiveThresholdMeanBinarizer::TransMatToBuffer(cv::Mat mSrc, unsigned char* ppBuffer,
                                                     int& nWidth, int& nHeight) {
    nWidth = mSrc.cols;
    // nWidth = ((nWidth + 3) / 4) * 4;
    nHeight = mSrc.rows;
    for (int j = 0; j < nHeight; ++j) {
        unsigned char* pdi = ppBuffer + j * nWidth;
        for (int z = 0; z < nWidth; ++z) {
            int nj = nHeight - j - 1;
            int value = *(uint8_t*)(mSrc.ptr<uint8_t>(nj) + z);
            if (value > 120)
                pdi[z] = 0;
            else
                pdi[z] = 1;
        }
    }
    return 0;
}
