// Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_TEST_DALI_TEST_H_
#define DALI_TEST_DALI_TEST_H_

#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dali/common.h"
#include "dali/error_handling.h"
#include "dali/image/jpeg.h"
#include "dali/pipeline/data/backend.h"
#include "dali/util/image.h"

namespace dali {

typedef enum {
  t_undefinedImgType = -1,
  t_jpegImgType,
  t_pngImgType,
  kLastImgType
} t_imgType;

const string image_folder = "/data/dali/test/test_images";  // NOLINT

struct DimPair { int h = 0, w = 0; };

// Some useful test 'types'
struct RGB {
  static const DALIImageType type = DALI_RGB;
};
struct BGR {
  static const DALIImageType type = DALI_BGR;
};
struct Gray {
  static const DALIImageType type = DALI_GRAY;
};

template <typename T>
void MeanStdDev(const vector<T> &diff, double *mean, double *std) {
  const size_t N = diff.size();
  // Avoid division by zero
  ASSERT_NE(N, 0);

  double sum = 0, var_sum = 0;
  for (auto &val : diff) {
    sum += val;
  }
  *mean = sum / N;
  for (auto &val : diff) {
    var_sum += (val - *mean)*(val - *mean);
  }
  *std = sqrt(var_sum / N);
}

template <typename T>
void MeanStdDevColorNorm(const vector<T> &diff, double *mean, double *std) {
  MeanStdDev(diff, mean, std);
  *mean /= (255. / 100.);    // normalizing to the color range and use percents
}

// Main testing fixture to provide common functionality across tests
class DALITest : public ::testing::Test {
 public:
  inline void SetUp() override {
    rand_gen_.seed(time(nullptr));
    imageDecrs_[t_jpegImgType].LoadImages(image_folder);
  }

  inline void TearDown() override {
    for (auto &ptr : images_[t_jpegImgType]) delete[] ptr;
  }

  inline int RandInt(int a, int b) {
    return std::uniform_int_distribution<>(a, b)(rand_gen_);
  }

  template <typename T>
  inline auto RandReal(int a, int b) -> T {
    return std::uniform_real_distribution<>(a, b)(rand_gen_);
  }

  void DecodeImage(const unsigned char *data, int data_size, int c, int img_type,
                          Tensor<CPUBackend> *out, unsigned char *out_dataPntr = nullptr) const {
    cv::Mat input(1, data_size, CV_8UC1, const_cast<unsigned char*>(data));

    cv::Mat tmp = cv::imdecode(input, c == 1 ? CV_LOAD_IMAGE_GRAYSCALE : CV_LOAD_IMAGE_COLOR);

    // if RGB needed, permute from BGR
    cv::Mat out_img(tmp.rows, tmp.cols, c != 1 ? CV_8UC3 : CV_8UC1);
    if (img_type == DALI_RGB) {
      // Convert from BGR to RGB for verification
      cv::cvtColor(tmp, out_img, CV_BGR2RGB);
    } else {
      out_img = tmp;
    }

    if (out) {
      out->Resize({tmp.rows, tmp.cols, c});
      out_dataPntr = out->mutable_data<unsigned char>();
    }

    std::memcpy(out_dataPntr, out_img.ptr(), out_img.rows * out_img.cols * c);
  }

  inline void DecodeImages(DALIImageType type, const ImgSetDescr &imgs,
                           vector<uint8*> *images, vector<DimPair> *image_dims) {
    c_ = IsColor(type) ? 3 : 1;
    const int flag = IsColor(type) ? CV_LOAD_IMAGE_COLOR : CV_LOAD_IMAGE_GRAYSCALE;
    const auto cType = IsColor(type) ? CV_8UC3 : CV_8UC1;
    const auto nImgs = imgs.nImages();
    images->resize(nImgs);
    image_dims->resize(nImgs);
    for (size_t i = 0; i < nImgs; ++i) {
      cv::Mat img;
      cv::Mat encode = cv::Mat(1, imgs.size(i), CV_8UC1, imgs.data(i));

      cv::imdecode(encode, flag, &img);

      const int h = (*image_dims)[i].h = img.rows;
      const int w = (*image_dims)[i].w = img.cols;
      cv::Mat out_img(h, w, cType);
      if (type == DALI_RGB) {
        // Convert from BGR to RGB for verification
        cv::cvtColor(img, out_img, CV_BGR2RGB);
      } else {
        out_img = img;
      }

      // Copy the decoded image out & save the dims
      ASSERT_TRUE(out_img.isContinuous());
      (*images)[i] = new uint8[h*w*c_];
      std::memcpy((*images)[i], out_img.ptr(), h*w*c_);
    }
  }

  inline void DecodeImages(DALIImageType type, t_imgType testImgType = t_jpegImgType) {
    DecodeImages(type, imageDecrs_[testImgType], images_+testImgType, image_dims_+testImgType);
  }

  inline void MakeDecodedBatch(int n, TensorList<CPUBackend> *tl, t_imgType type, const int c) {
    const vector<uint8*> &images = images_[type];
    DALI_ENFORCE(!images.empty(), "Images must be populated to create batches");

    const vector<DimPair> &image_dims = image_dims_[type];
    vector<Dims> shape(n);
    for (int i = 0; i < n; ++i) {
      shape[i] = {image_dims[i % images.size()].h,
                  image_dims[i % images.size()].w,
                  c};
    }
    tl->template mutable_data<uint8>();
    tl->Resize(shape);
    for (int i = 0; i < n; ++i) {
      std::memcpy(tl->template mutable_tensor<uint8>(i),
                  images[i % images.size()],
                  Product(tl->tensor_shape(i)));
    }
  }

  inline void MakeImageBatch(int n, TensorList<CPUBackend> *tl,
                             DALIImageType type = DALI_RGB, t_imgType imageType = t_jpegImgType) {
    if (images_[imageType].empty())
      DecodeImages(type, imageType);

    MakeDecodedBatch(n, tl, imageType, c_);
  }

  // Make a batch (in TensorList) of arbitrary raw data
  inline void MakeEncodedBatch(TensorList<CPUBackend> *tl, int n, t_imgType imageType) {
    MakeEncodedBatch(tl, n, imageDecrs_[imageType]);
  }

  inline void MakeEncodedBatch(TensorList<CPUBackend> *tl, int n, const ImgSetDescr &imgs) {
    const auto nImgs = imgs.nImages();
    DALI_ENFORCE(nImgs > 0, "data must be populated to create batches");

    vector<Dims> shape(n);
    for (int i = 0; i < n; ++i)
      shape[i] = imgs.shape(i % nImgs);

    tl->template mutable_data<uint8>();
    tl->Resize(shape);

    for (int i = 0; i < n; ++i)
      imgs.copyImage(i % nImgs, tl->template mutable_tensor<uint8>(i));
  }

  // Make a batch (of vector<Tensor>) of arbitrary raw data
  inline void MakeEncodedBatch(vector<Tensor<CPUBackend>> *t, int n, t_imgType imageType) {
    const ImgSetDescr &imgs = imageDecrs_[imageType];
    const auto nImgs = imgs.nImages();
    DALI_ENFORCE(nImgs > 0, "data must be populated to create batches");

    t->resize(n);
    for (int i = 0; i < n; ++i) {
      const auto imgIdx = i % nImgs;
      auto& ti = t->at(i);
      ti = Tensor<CPUBackend>{};
      ti.Resize(imgs.shape(imgIdx));
      ti.template mutable_data<uint8>();
      imgs.copyImage(imgIdx, ti.raw_mutable_data());
    }
  }

  inline void MakeJPEGBatch(TensorList<CPUBackend> *tl, int n) {
    MakeEncodedBatch(tl, n, t_jpegImgType);
  }

  inline void MakeJPEGBatch(vector<Tensor<CPUBackend>> *t, int n) {
    MakeEncodedBatch(t, n, t_jpegImgType);
  }

  // From OCV example :
  // docs.opencv.org/2.4/doc/tutorials/gpu/gpu-basics-similarity/gpu-basics-similarity.html
  cv::Scalar MSSIM(uint8 *a, uint8 *b, int h, int w, int c) {
    cv::Mat i1 = cv::Mat(h, w, c == 3 ? CV_8UC3 : CV_8UC1, a);
    cv::Mat i2 = cv::Mat(h, w, c == 3 ? CV_8UC3 : CV_8UC1, b);

    const double C1 = 6.5025, C2 = 58.5225;
    /***************************** INITS **********************************/
    int d     = CV_32F;

    cv::Mat I1, I2;
    i1.convertTo(I1, d);           // cannot calculate on one byte large values
    i2.convertTo(I2, d);

    cv::Mat I2_2   = I2.mul(I2);        // I2^2
    cv::Mat I1_2   = I1.mul(I1);        // I1^2
    cv::Mat I1_I2  = I1.mul(I2);        // I1 * I2

    /*************************** END INITS **********************************/

    cv::Mat mu1, mu2;   // PRELIMINARY COMPUTING
    cv::GaussianBlur(I1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(I2, mu2, cv::Size(11, 11), 1.5);

    cv::Mat mu1_2   =   mu1.mul(mu1);
    cv::Mat mu2_2   =   mu2.mul(mu2);
    cv::Mat mu1_mu2 =   mu1.mul(mu2);

    cv::Mat sigma1_2, sigma2_2, sigma12;

    cv::GaussianBlur(I1_2, sigma1_2, cv::Size(11, 11), 1.5);
    sigma1_2 -= mu1_2;

    cv::GaussianBlur(I2_2, sigma2_2, cv::Size(11, 11), 1.5);
    sigma2_2 -= mu2_2;

    cv::GaussianBlur(I1_I2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    ///////////////////////////////// FORMULA ////////////////////////////////
    cv::Mat t1, t2, t3;

    t1 = 2 * mu1_mu2 + C1;
    t2 = 2 * sigma12 + C2;
    t3 = t1.mul(t2);                    // t3 = ((2*mu1_mu2 + C1).*(2*sigma12 + C2))

    t1 = mu1_2 + mu2_2 + C1;
    t2 = sigma1_2 + sigma2_2 + C2;
    t1 = t1.mul(t2);                    // t1 =((mu1_2 + mu2_2 + C1).*(sigma1_2 + sigma2_2 + C2))

    cv::Mat ssim_map;
    cv::divide(t3, t1, ssim_map);       // ssim_map =  t3./t1;

    cv::Scalar mssim = mean(ssim_map);  // mssim = average of ssim map
    return mssim;
  }

 protected:
  int GetNumColorComp() const                   { return c_; }
  const ImgSetDescr &Imgs(t_imgType type) const { return imageDecrs_[type]; }

  std::mt19937 rand_gen_;
  ImgSetDescr imageDecrs_[kLastImgType];

  // Decoded images
  vector<uint8*> images_[kLastImgType];
  vector<DimPair> image_dims_[kLastImgType];
  int c_;
};
}  // namespace dali

#endif  // DALI_TEST_DALI_TEST_H_
