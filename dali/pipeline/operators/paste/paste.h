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

#ifndef DALI_PIPELINE_OPERATORS_PASTE_PASTE_H_
#define DALI_PIPELINE_OPERATORS_PASTE_PASTE_H_

#include <cstring>
#include <utility>
#include <vector>
#include <random>

#include "dali/common.h"
#include "dali/pipeline/operators/common.h"
#include "dali/error_handling.h"
#include "dali/pipeline/operators/operator.h"

namespace dali {

static const int MAX_C = 1024;

template <typename Backend>
class Paste : public Operator<Backend> {
 public:
  // 6 values: in_H, in_W, out_H, out_W, paste_y, paste_x
  static const int NUM_INDICES = 6;

  explicit inline Paste(const OpSpec &spec) :
    Operator<Backend>(spec),
    C_(spec.GetArgument<int>("n_channels")) {
    // Kind of arbitrary, we need to set some limit here
    // because we use static shared memory for storing
    // fill value array
    DALI_ENFORCE(C_ <= MAX_C,
      "n_channels of more than 1024 is not supported");
    std::vector<uint8> rgb;
    GetSingleOrRepeatedArg(spec, &rgb, "fill_value", C_);
    fill_value_.Copy(rgb, 0);

    input_ptrs_.Resize({batch_size_});
    output_ptrs_.Resize({batch_size_});
    in_out_dims_paste_yx_.Resize({batch_size_ * NUM_INDICES});
  }

  virtual inline ~Paste() = default;

 protected:
  void RunImpl(Workspace<Backend> *ws, const int idx) override;

  void SetupSharedSampleParams(Workspace<Backend> *ws) override {
    // No setup shared between input sets
  }

  void SetupSampleParams(Workspace<Backend> *ws, const int idx);

  void RunHelper(Workspace<Backend> *ws);

 private:
  inline Dims Prepare(const std::vector<Index> input_shape, const OpSpec& spec,
           ArgumentWorkspace *ws, int i, std::vector<int> *sample_dims_paste_yx) {
    DALI_ENFORCE(input_shape.size() == 3,
                 "Expects 3-dimensional image input.");

    const int H = input_shape[0];
    const int W = input_shape[1];
    C_ = input_shape[2];

    const float ratio = spec.GetArgument<float>("ratio", ws, i);
    DALI_ENFORCE(ratio >= 1.,
                 "ratio of less than 1 is not supported");

    const int new_H = static_cast<int>(ratio * H);
    const int new_W = static_cast<int>(ratio * W);

    const float paste_x_ = spec.GetArgument<float>("paste_x", ws, i);
    const float paste_y_ = spec.GetArgument<float>("paste_y", ws, i);
    DALI_ENFORCE(paste_x_ >= 0,
                 "paste_x of less than 0 is not supported");
    DALI_ENFORCE(paste_x_ <= 1,
                 "paste_x of more than 1 is not supported");
    DALI_ENFORCE(paste_y_ >= 0,
                 "paste_y of less than 0 is not supported");
    DALI_ENFORCE(paste_y_ <= 1,
                 "paste_y of more than 1 is not supported");
    const int paste_x = paste_x_ * (new_W - W);
    const int paste_y = paste_y_ * (new_H - H);

    *sample_dims_paste_yx = {H, W, new_H, new_W, paste_y, paste_x};
    return {new_H, new_W, C_};
  }

  // Op parameters
  int C_;
  Tensor<Backend> fill_value_;

  Tensor<CPUBackend> input_ptrs_, output_ptrs_, in_out_dims_paste_yx_;
  Tensor<GPUBackend> input_ptrs_gpu_, output_ptrs_gpu_, in_out_dims_paste_yx_gpu_;

  USE_OPERATOR_MEMBERS();
};

}  // namespace dali

#endif  // DALI_PIPELINE_OPERATORS_PASTE_PASTE_H_
