// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#include "../precomp.hpp"
#include "align.hpp"

using std::max;
using std::min;
namespace cv {
namespace wechat_qrcode {

static Mat transposeMat(const Mat &src) {
    Mat dst(src.cols, src.rows, src.type());
    size_t elemSize = src.type(); // Number of bytes per element

    for (int i = 0; i < src.rows; ++i) {
        const uchar *srcRow = src.ptr<uchar>(i); // Source matrix row pointer
        for (int j = 0; j < src.cols; ++j) {
            const uchar *srcElem = srcRow + j * elemSize; // Address of (i,j) in source
            uchar *dstElem = dst.ptr<uchar>(j) + i * elemSize; // Address of (j,i) in destination
            std::memcpy(dstElem, srcElem, elemSize); // Copy element data
        }
    }

    return dst;
}

Align::Align() { rotate90_ = false; }

vector<Point2f> Align::warpBack(const vector<Point2f> &dst_pts) {
    vector<Point2f> src_pts;
    for (size_t j = 0; j < dst_pts.size(); j++) {
        auto src_x = (rotate90_ ? dst_pts[j].y : dst_pts[j].x) + crop_x_;
        auto src_y = (rotate90_ ? dst_pts[j].x : dst_pts[j].y) + crop_y_;
        src_pts.push_back(Point2f(src_x, src_y));
    }
    return src_pts;
}

Mat Align::crop(const Mat &inputImg, const Mat &srcPts, const float paddingW, const float paddingH,
                const int minPadding) {
    int x0 = srcPts.ptr<float>(0)[0];
    int y0 = srcPts.ptr<float>(0)[1];
    int x2 = srcPts.ptr<float>(2)[0];
    int y2 = srcPts.ptr<float>(2)[1];

    int width = x2 - x0 + 1;
    int height = y2 - y0 + 1;

    int padx = max(paddingW * width, static_cast<float>(minPadding));
    int pady = max(paddingH * height, static_cast<float>(minPadding));

    crop_x_ = max(x0 - padx, 0);
    crop_y_ = max(y0 - pady, 0);
    int end_x = min(x2 + padx, inputImg.cols - 1);
    int end_y = min(y2 + pady, inputImg.rows - 1);

    Rect crop_roi(crop_x_, crop_y_, (end_x - crop_x_ + 1) & -2, (end_y - crop_y_ + 1) & -2);

    Mat dst = inputImg(crop_roi).clone();
    if (rotate90_) dst = transposeMat(dst);  // transpose
    return dst;
}

}  // namespace wechat_qrcode
}  // namespace cv
