// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#include "precomp.hpp"
#include "opencv2/wechat_qrcode.hpp"
#include "decodermgr.hpp"
#include "detector/align.hpp"
#include "detector/ssd_detector.hpp"
#include "scale/super_scale.hpp"
#include "zxing/result.hpp"
namespace cv {
namespace wechat_qrcode {
class WeChatQRCode::Impl {
public:
    Impl() {}
    ~Impl() {}
    /**
     * @brief detect QR codes from the given image
     *
     * @param img supports grayscale or color (BGR) image.
     * @return vector<Mat> detected QR code bounding boxes.
     */
    std::vector<Mat> detect(const Mat& img);
    /**
     * @brief decode QR codes from detected points
     *
     * @param img supports grayscale or color (BGR) image.
     * @param candidate_points detected points. we name it "candidate points" which means no
     * all the qrcode can be decoded.
     * @param points succussfully decoded qrcode with bounding box points.
     * @return vector<string>
     */
    std::vector<std::string> decode(const Mat& img,
                                    const std::vector<Mat>& candidate_points,
                                    std::vector<Mat>& points);
    int applyDetector(const Mat& img, std::vector<Mat>& points);
    Mat cropObj(const Mat& img, const Mat& point, Align& aligner);
    std::vector<float> getScaleList(const int width, const int height);
    std::shared_ptr<SSDDetector> detector_;
    std::shared_ptr<SuperScale> super_resolution_model_;
    bool use_nn_detector_, use_nn_sr_;
    float scaleFactor = -1.f;
};

WeChatQRCode::WeChatQRCode() {
    p = make_shared<WeChatQRCode::Impl>();

    // initialize detector model (caffe)
    {
        p->use_nn_detector_ = true;
        p->detector_ = make_shared<SSDDetector>();
        auto ret = p->detector_->init();
    }

    // initialize super_resolution_model
    // it could also support non model weights by cubic resizing
    // so, we initialize it first.
    {
        p->super_resolution_model_ = make_shared<SuperScale>();
        p->use_nn_sr_ = true;
        // initialize dnn model (caffe format)
        auto ret = p->super_resolution_model_->init();
    }
}

vector<string> WeChatQRCode::detectAndDecode(cv::Mat &img, std::vector<cv::Mat> &points) {
    if (img.cols <= 20 || img.rows <= 20) {
        return vector<string>();  // image data is not enough for providing reliable results
    }
    Mat input_img;
    int incn = img.channels();
    if (incn == 3 || incn == 4) {
        int type = incn == 3 ? ncnn::Mat::PIXEL_BGR2GRAY : ncnn::Mat::PIXEL_BGRA2GRAY;
        ncnn::Mat ncnn_img = ncnn::Mat::from_pixels(img.data, type, img.cols, img.rows);
        input_img.create(img.rows, img.cols, CV_8UC1);
        ncnn_img.to_pixels(input_img.data, ncnn::Mat::PIXEL_GRAY);
    } else {
        input_img = img;
    }
    auto candidate_points = p->detect(input_img);
    auto res_points = vector<Mat>();
    auto ret = p->decode(input_img, candidate_points, res_points);
    // opencv type convert
    vector<Mat> tmp_points;
    for (size_t i = 0; i < res_points.size(); i++) {
        Mat tmp_point = res_points[i];
        tmp_points.push_back(tmp_point);
    }
    points = tmp_points;
    return ret;
}

void WeChatQRCode::setScaleFactor(float _scaleFactor) {
    if (_scaleFactor > 0 && _scaleFactor <= 1.f)
        p->scaleFactor = _scaleFactor;
    else
        p->scaleFactor = -1.f;
};

float WeChatQRCode::getScaleFactor() {
    return p->scaleFactor;
};

vector<string> WeChatQRCode::Impl::decode(const Mat& img,
                                          const vector<Mat>& candidate_points,
                                          vector<Mat>& points) {
    if (candidate_points.size() == 0) {
        return vector<string>();
    }
    vector<string> decode_results;
    for (const auto& point : candidate_points) {
        Mat cropped_img;
        Align aligner;
        if (use_nn_detector_) {
            cropped_img = cropObj(img, point, aligner);
        } else {
            cropped_img = img;
        }
        // scale_list contains different scale ratios
        auto scale_list = getScaleList(cropped_img.cols, cropped_img.rows);
        for (auto cur_scale : scale_list) {
            Mat scaled_img =
                super_resolution_model_->processImageScale(cropped_img, cur_scale, use_nn_sr_);
            string result;
            DecoderMgr decodemgr;
            vector<vector<Point2f>> zxing_points, check_points;
            auto ret = decodemgr.decodeImage(scaled_img, use_nn_detector_, decode_results, zxing_points);
            if (ret == 0) {
                for(size_t i = 0; i <zxing_points.size(); i++){
                    vector<Point2f> points_qr = zxing_points[i];
                    for (auto&& pt: points_qr) {
                        pt.x /= cur_scale;
                        pt.y /= cur_scale;
                    }

                    if (use_nn_detector_)
                        points_qr = aligner.warpBack(points_qr);

                    int num_points = static_cast<int>(points_qr.size());
                    auto point_to_save = Mat(num_points, 2, CV_32FC1);
                    for (int j = 0; j < num_points; ++j) {
                        point_to_save.ptr<float>(j)[0] = points_qr[j].x;
                        point_to_save.ptr<float>(j)[1] = points_qr[j].y;
                    }
                    // try to find duplicate qr corners
                    bool isDuplicate = false;
                    for (const auto &tmp_points: check_points) {
                        const float eps = 10.f;
                        for (size_t j = 0; j < tmp_points.size(); j++) {
                            if (abs(tmp_points[j].x - points_qr[j].x) < eps &&
                                abs(tmp_points[j].y - points_qr[j].y) < eps) {
                                isDuplicate = true;
                            }
                            else {
                                isDuplicate = false;
                                break;
                            }
                        }
                    }
                    if (isDuplicate == false) {
                        points.push_back(point_to_save);
                        check_points.push_back(points_qr);
                    }
                    else {
                        decode_results.erase(decode_results.begin() + i, decode_results.begin() + i + 1);
                    }
                }
                break;
            }
        }
    }

    return decode_results;
}

vector<Mat> WeChatQRCode::Impl::detect(const Mat& img) {
    auto points = vector<Mat>();

    if (use_nn_detector_) {
        // use cnn detector
        auto ret = applyDetector(img, points);
    } else {
        auto width = img.cols, height = img.rows;
        // if there is no detector, use the full image as input
        auto point = Mat(4, 2, CV_32FC1);
        point.ptr<float>(0)[0] = 0;
        point.ptr<float>(0)[1] = 0;
        point.ptr<float>(1)[0] = width - 1;
        point.ptr<float>(1)[1] = 0;
        point.ptr<float>(2)[0] = width - 1;
        point.ptr<float>(2)[1] = height - 1;
        point.ptr<float>(3)[0] = 0;
        point.ptr<float>(3)[1] = height - 1;
        points.push_back(point);
    }
    return points;
}

int WeChatQRCode::Impl::applyDetector(const Mat& img, vector<Mat>& points) {
    int img_w = img.cols;
    int img_h = img.rows;

    const float targetArea = 400.f * 400.f;
    // hard code input size
    const float tmpScaleFactor = scaleFactor == -1.f ? min(1.f, sqrt(targetArea / (img_w * img_h))) : scaleFactor;
    int detect_width = img_w * tmpScaleFactor;
    int detect_height = img_h * tmpScaleFactor;

    points = detector_->forward(img, detect_width, detect_height);

    return 0;
}

Mat WeChatQRCode::Impl::cropObj(const Mat& img, const Mat& point, Align& aligner) {
    // make some padding to boost the qrcode details recall.
    float padding_w = 0.1f, padding_h = 0.1f;
    auto min_padding = 15;
    auto cropped = aligner.crop(img, point, padding_w, padding_h, min_padding);
    return cropped;
}

// empirical rules
vector<float> WeChatQRCode::Impl::getScaleList(const int width, const int height) {
    if (width < 320 || height < 320) return {1.0, 2.0, 0.5};
    if (width < 640 && height < 640) return {1.0, 0.5};
    return {0.5, 1.0};
}
}  // namespace wechat_qrcode
}  // namespace cv
