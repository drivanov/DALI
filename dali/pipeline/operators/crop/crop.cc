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

#include "dali/pipeline/operators/crop/crop.h"
#include "dali/image/transform.h"
#include "dali/util/half.hpp"

namespace dali {

DALI_SCHEMA(Crop)
    .DocStr(R"code(Perform a random crop.)code")
    .NumInput(1)
    .NumOutput(1)
    .AllowMultipleInputSets()
    .AddOptionalArg("crop_pos_x",
                    R"code(Horizontal position of the crop in image coordinates (0.0 - 1.0))code",
                    0.5f, true)
    .AddOptionalArg("crop_pos_y",
                    R"code(Vertical position of the crop in image coordinates (0.0 - 1.0))code",
                    0.5f, true)
    .AddOptionalArg("image_type",
                    R"code(The color space of input and output image)code", DALI_RGB, false)
    .AddArg("crop",
            R"code(Size of the cropped image. If only a single value `c` is provided,
 the resulting crop will be square with size `(c,c)`)code", DALI_INT_VEC)
    .EnforceInputLayout(DALI_NHWC);


template<>
Crop<CPUBackend>::Crop(const OpSpec &spec) : Operator<CPUBackend>(spec), CropAttr(spec) {
  Init(num_threads_);
}

template<typename Out>
void CropKernel(
  const int C,
  const int H,
  const int W,
  const unsigned char *input_ptr,
  const int in_stride,
  DALITensorLayout layout,
  Out *output_ptr) {
  if (layout == DALI_NCHW) {
    for (int c = 0; c < C; ++c) {
      for (int h = 0; h < H; ++h) {
        for (int w = 0; w < W; ++w) {
          // From HWC
          const int in_idx = h * in_stride + w * C + c;
          // To CHW
          const int out_idx = (c * H + h) * W + w;
          output_ptr[out_idx] = static_cast<Out>(input_ptr[in_idx]);
        }
      }
    }
  } else {  // Layout == DALI_NHWC
    for (int c = 0; c < C; ++c) {
      for (int h = 0; h < H; ++h) {
        for (int w = 0; w < W; ++w) {
          // From HWC
          const int in_idx = h * in_stride + w * C + c;
          // To HWC
          const int out_idx = (h * W + w) * C + c;
          output_ptr[out_idx] = static_cast<Out>(input_ptr[in_idx]);
        }
      }
    }
  }
}

template<typename Out>
DALIError_t ValidateCrop(const uint8 *in_img, int H, int W, int C, const Out *out_img) {
  DALI_ASSERT(H > 0);
  DALI_ASSERT(W > 0);
  DALI_ASSERT(C == 1 || C == 3);
  DALI_ASSERT(in_img != nullptr);
  DALI_ASSERT(out_img != nullptr);
  return DALISuccess;
}

template<>
template<typename Out>
void Crop<CPUBackend>::ValidateHelper(const Tensor<CPUBackend> *input, Tensor<CPUBackend> *output) {
  // Validate parameters
  DALI_CALL(ValidateCrop(
    input->template data<uint8>(),
    crop_[0], crop_[1], C_,
    output->template mutable_data<Out>()));
}

template<>
template<typename Out>
void Crop<CPUBackend>::RunHelper(SampleWorkspace *ws, const int idx) {
  const auto &input = ws->Input<CPUBackend>(idx);
  auto output = ws->Output<CPUBackend>(idx);

  // Validate
  ValidateHelper<Out>(&input, output);

  const int dataIdx = ws->thread_idx();
  const int H = per_sample_dimensions_[dataIdx].first;
  const int W = per_sample_dimensions_[dataIdx].second;

  const int crop_y = per_sample_crop_[dataIdx].first;
  const int crop_x = per_sample_crop_[dataIdx].second;

  CropKernel<Out>(C_, crop_[0], crop_[1],
                              input.template data<uint8>() + (crop_y * W + crop_x) * C_,
                              W * C_, output_layout_,
                              output->template mutable_data<Out>());
}

template<>
void Crop<CPUBackend>::RunImpl(SampleWorkspace *ws, const int idx) {
  const auto &input = ws->Input<CPUBackend>(idx);
  auto output = ws->Output<CPUBackend>(idx);

  DALITensorLayout outLayout;
  output->Resize(GetOutShape(input.GetLayout(), &outLayout));
  output->SetLayout(outLayout);

  CheckParam(input, "CropCPUBackend");
  if (output_type_ == DALI_FLOAT16)
    RunHelper<half_float::half>(ws, idx);
  else
    CallRunHelper(ws, idx);
}

template<>
void Crop<CPUBackend>::SetupSharedSampleParams(SampleWorkspace *ws) {
  if (output_type_ == DALI_NO_TYPE) {
    const auto &input = ws->Input<CPUBackend>(0);
    output_type_ = input.type().id();
  }

  SetupSharedSampleParams(ws, CheckShapes(ws), ws->thread_idx(), ws->data_idx());
}

// Register operator
DALI_REGISTER_OPERATOR(Crop, Crop<CPUBackend>, CPU);

}  // namespace dali
