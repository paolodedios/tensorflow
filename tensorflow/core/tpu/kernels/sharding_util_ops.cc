/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#define EIGEN_USE_THREADS

#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"
#include "tensorflow/core/framework/kernel_def.pb.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/op_requires.h"
#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/resource_mgr.h"
#include "tensorflow/core/framework/resource_var.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_types.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/framework/types.pb.h"
#include "tensorflow/core/kernels/pad_op.h"
#include "tensorflow/core/kernels/slice_op.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/platform/refcount.h"
#include "tensorflow/core/platform/types.h"

namespace tensorflow {
namespace {

constexpr absl::string_view kTensorName = "'input' tensor";
constexpr absl::string_view kResourceName = "'resource' variable tensor";

// Converts flatten index to start indices (subscript scaled with slice shape)
// for determining where to start a slice in the input tensor.
template <int Rank>
Eigen::DSizes<Eigen::DenseIndex, Rank> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, Rank>& slice_shape,
    const int index) {
  return Eigen::DSizes<Eigen::DenseIndex, Rank>();
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 1> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 1>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 1> subscript;
  subscript[0] = index * slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 2> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 2>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 2> subscript;
  subscript[1] = (index % num_splits[1]) * slice_shape[1];
  subscript[0] = (index / num_splits[1]) * slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 3> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 3>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 3> subscript;
  subscript[2] = (index % num_splits[2]) * slice_shape[2];
  subscript[1] = ((index / num_splits[2]) % num_splits[1]) * slice_shape[1];
  subscript[0] = (index / (num_splits[2] * num_splits[1])) * slice_shape[2];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 4> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 4>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 4> subscript;
  subscript[3] = (index % num_splits[3]) * slice_shape[3];
  subscript[2] = ((index / num_splits[3]) % num_splits[2]) * slice_shape[2];
  subscript[1] = ((index / (num_splits[3] * num_splits[2])) % num_splits[1]) *
                 slice_shape[1];
  subscript[0] = (index / (num_splits[3] * num_splits[2] * num_splits[1])) *
                 slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 5> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 5>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 5> subscript;
  subscript[4] = (index % num_splits[4]) * slice_shape[4];
  subscript[3] = ((index / num_splits[4]) % num_splits[3]) * slice_shape[3];
  subscript[2] = ((index / (num_splits[4] * num_splits[3])) % num_splits[2]) *
                 slice_shape[2];
  subscript[1] = ((index / (num_splits[4] * num_splits[3] * num_splits[2])) %
                  num_splits[1]) *
                 slice_shape[1];
  subscript[0] = (index / (num_splits[4] * num_splits[3] * num_splits[2] *
                           num_splits[1])) *
                 slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 6> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 6>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 6> subscript;
  subscript[5] = (index % num_splits[5]) * slice_shape[5];
  subscript[4] = ((index / num_splits[5]) % num_splits[4]) * slice_shape[4];
  subscript[3] = ((index / (num_splits[5] * num_splits[4])) % num_splits[3]) *
                 slice_shape[3];
  subscript[2] = ((index / (num_splits[5] * num_splits[4] * num_splits[3])) %
                  num_splits[2]) *
                 slice_shape[2];
  subscript[1] = ((index / (num_splits[5] * num_splits[4] * num_splits[3] *
                            num_splits[2])) %
                  num_splits[1]) *
                 slice_shape[1];
  subscript[0] = (index / (num_splits[5] * num_splits[4] * num_splits[3] *
                           num_splits[2] * num_splits[1])) *
                 slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 7> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 7>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 7> subscript;
  subscript[6] = (index % num_splits[6]) * slice_shape[6];
  subscript[5] = ((index / num_splits[6]) % num_splits[5]) * slice_shape[5];
  subscript[4] = ((index / (num_splits[6] * num_splits[5])) % num_splits[4]) *
                 slice_shape[4];
  subscript[3] = ((index / (num_splits[6] * num_splits[5] * num_splits[4])) %
                  num_splits[3]) *
                 slice_shape[3];
  subscript[2] = ((index / (num_splits[6] * num_splits[5] * num_splits[4] *
                            num_splits[3])) %
                  num_splits[2]) *
                 slice_shape[2];
  subscript[1] = ((index / (num_splits[6] * num_splits[5] * num_splits[4] *
                            num_splits[3] * num_splits[2])) %
                  num_splits[1]) *
                 slice_shape[1];
  subscript[0] = (index / (num_splits[6] * num_splits[5] * num_splits[4] *
                           num_splits[3] * num_splits[2] * num_splits[1])) *
                 slice_shape[0];
  return subscript;
}

template <>
Eigen::DSizes<Eigen::DenseIndex, 8> GetSliceIndices(
    absl::Span<const int32> num_splits,
    const Eigen::DSizes<Eigen::DenseIndex, 8>& slice_shape, const int index) {
  Eigen::DSizes<Eigen::DenseIndex, 8> subscript;
  subscript[7] = (index % num_splits[7]) * slice_shape[7];
  subscript[6] = ((index / num_splits[7]) % num_splits[6]) * slice_shape[6];
  subscript[5] = ((index / (num_splits[7] * num_splits[6])) % num_splits[5]) *
                 slice_shape[5];
  subscript[4] = ((index / (num_splits[7] * num_splits[6] * num_splits[5])) %
                  num_splits[4]) *
                 slice_shape[4];
  subscript[3] = ((index / (num_splits[7] * num_splits[6] * num_splits[5] *
                            num_splits[4])) %
                  num_splits[3]) *
                 slice_shape[3];
  subscript[2] = ((index / (num_splits[7] * num_splits[6] * num_splits[5] *
                            num_splits[4] * num_splits[3])) %
                  num_splits[2]) *
                 slice_shape[2];
  subscript[1] = ((index / (num_splits[7] * num_splits[6] * num_splits[5] *
                            num_splits[4] * num_splits[3] * num_splits[2])) %
                  num_splits[1]) *
                 slice_shape[1];
  subscript[0] =
      (index / (num_splits[7] * num_splits[6] * num_splits[5] * num_splits[4] *
                num_splits[3] * num_splits[2] * num_splits[1])) *
      slice_shape[0];
  return subscript;
}

template <typename Device, typename T, bool Resource>
class XlaSplitNDBaseOp : public OpKernel {
 public:
  explicit XlaSplitNDBaseOp(OpKernelConstruction* ctx) : OpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("num_splits", &num_splits_));

    int num_dims_to_split = 0;
    for (int i = 0, e = num_splits_.size(); i < e; ++i) {
      const auto& split = num_splits_[i];
      OP_REQUIRES(
          ctx, split > 0,
          errors::InvalidArgument("'num_splits' at index ", i,
                                  " must be positive, but got ", split, "."));
      if (split > 1) {
        ++num_dims_to_split;
      }
      num_slices_ *= split;
    }

    int n;
    OP_REQUIRES_OK(ctx, ctx->GetAttr("N", &n));
    OP_REQUIRES(
        ctx, n == num_slices_,
        errors::InvalidArgument("'N' must match number of slices ", num_slices_,
                                " from 'num_splits', but got ", n, "."));

    OP_REQUIRES_OK(ctx, ctx->GetAttr("paddings", &paddings_));
    const int expected_rank = num_splits_.size();
    if (!paddings_.empty()) {
      OP_REQUIRES(ctx, paddings_.size() == expected_rank,
                  errors::InvalidArgument(
                      "'paddings' length must match 'num_splits' length ",
                      expected_rank, ", but got ", paddings_.size(), "."));

      for (int dim = 0; dim < expected_rank; ++dim) {
        OP_REQUIRES(ctx, paddings_[dim] >= 0,
                    errors::InvalidArgument(
                        "'padding' must be all non-negative, but got ",
                        paddings_[dim], " at index ", dim, "."));
        if (paddings_[dim] > 0) {
          has_paddings_ = true;
        }
      }
    } else {
      paddings_.assign(expected_rank, 0);
    }
  }

 protected:
  void ComputeInternal(OpKernelContext* ctx, const Tensor* input) {
    absl::string_view input_name = Resource ? kResourceName : kTensorName;
    const int rank = input->shape().dims();

    OP_REQUIRES(ctx, rank > 0 && rank <= 8,
                errors::InvalidArgument(
                    input_name, " must have rank in range (0, 8], but got ",
                    rank, "."));
    OP_REQUIRES(
        ctx, rank == num_splits_.size(),
        errors::InvalidArgument(
            input_name, " rank must be the same as 'num_splits' length ",
            num_splits_.size(), ", but got rank ", rank, "."));

    for (int dim = 0; dim < rank; ++dim) {
      OP_REQUIRES(
          ctx,
          (input->shape().dim_size(dim) + paddings_[dim]) % num_splits_[dim] ==
              0,
          errors::InvalidArgument(input_name, " shape dimension ", dim, " (",
                                  input->shape().dim_size(dim),
                                  ") with padding ", paddings_[dim],
                                  " must be evenly divisible by 'num_splits' ",
                                  num_splits_[dim], "."));
    }

    if (has_paddings_) {
      if (rank == 1) {
        SliceAndPad<1>(ctx, input);
      } else if (rank == 2) {
        SliceAndPad<2>(ctx, input);
      } else if (rank == 3) {
        SliceAndPad<3>(ctx, input);
      } else if (rank == 4) {
        SliceAndPad<4>(ctx, input);
      } else if (rank == 5) {
        SliceAndPad<5>(ctx, input);
      } else if (rank == 6) {
        SliceAndPad<6>(ctx, input);
      } else if (rank == 7) {
        SliceAndPad<7>(ctx, input);
      } else if (rank == 8) {
        SliceAndPad<8>(ctx, input);
      }
      return;
    }

    if (rank == 1) {
      Slice<1>(ctx, input);
    } else if (rank == 2) {
      Slice<2>(ctx, input);
    } else if (rank == 3) {
      Slice<3>(ctx, input);
    } else if (rank == 4) {
      Slice<4>(ctx, input);
    } else if (rank == 5) {
      Slice<5>(ctx, input);
    } else if (rank == 6) {
      Slice<6>(ctx, input);
    } else if (rank == 7) {
      Slice<7>(ctx, input);
    } else if (rank == 8) {
      Slice<8>(ctx, input);
    }
  }

 private:
  template <int Rank>
  void SliceAndPad(OpKernelContext* ctx, const Tensor* input) {
    const auto& shape = input->shape().dim_sizes();
    const Device& device = ctx->eigen_device<Device>();
    functor::Pad<Device, T, int64, Rank> pad_functor;
    if (num_slices_ == 1) {
      Eigen::array<Eigen::IndexPair<int64>, Rank> tensor_paddings;
      TensorShape output_shape;
      for (int i = 0; i < Rank; ++i) {
        tensor_paddings[i] = {0, paddings_[i]};
        output_shape.AddDim(shape[i] + paddings_[i]);
      }
      Tensor* output = nullptr;
      OP_REQUIRES_OK(ctx,
                     ctx->allocate_output(/*index=*/0, output_shape, &output));
      pad_functor(device, output->tensor<T, Rank>(), input->tensor<T, Rank>(),
                  tensor_paddings, T());
      return;
    }

    // Slice shape with optional padding.
    TensorShape output_slice_shape;
    for (int dim = 0; dim < Rank; ++dim) {
      output_slice_shape.AddDim((shape[dim] + paddings_[dim]) /
                                num_splits_[dim]);
    }
    const Eigen::DSizes<Eigen::DenseIndex, Rank> base_slice_shape =
        output_slice_shape.AsEigenDSizes<Rank>();
    functor::Slice<Device, T, Rank> slice_functor;

    for (int i = 0; i < num_slices_; ++i) {
      Tensor* output_slice = nullptr;
      OP_REQUIRES_OK(ctx, ctx->allocate_output(
                              /*index=*/i, output_slice_shape, &output_slice));

      bool pad = false;
      TensorShape non_padded_slice_shape;
      Eigen::array<Eigen::IndexPair<int64>, Rank> slice_paddings;
      Eigen::DSizes<Eigen::DenseIndex, Rank> slice_indices =
          GetSliceIndices<Rank>(num_splits_, base_slice_shape, i);

      // Calculate paddings necessary for shard instead of padding input and
      // slicing subsequently to reduce temporary memory allocation.
      for (int dim = 0; dim < Rank; ++dim) {
        const int64 dim_size = shape[dim];
        if (slice_indices[dim] >= dim_size) {
          // Complete padding.
          slice_indices[dim] = dim_size;
          non_padded_slice_shape.AddDim(0);
          slice_paddings[dim] = {0, base_slice_shape[dim]};
          pad = true;
        } else if (slice_indices[dim] + base_slice_shape[dim] >= dim_size) {
          // Partial padding.
          non_padded_slice_shape.AddDim(dim_size - slice_indices[dim]);
          slice_paddings[dim] = {
              0, base_slice_shape[dim] - non_padded_slice_shape.dim_size(dim)};
          pad = true;
        } else {
          non_padded_slice_shape.AddDim(base_slice_shape[dim]);
        }
      }

      if (pad) {
        Tensor no_padding_slice;
        OP_REQUIRES_OK(
            ctx, ctx->allocate_temp(input->dtype(), non_padded_slice_shape,
                                    &no_padding_slice));
        slice_functor(device, no_padding_slice.tensor<T, Rank>(),
                      input->tensor<T, Rank>(), slice_indices,
                      non_padded_slice_shape.AsEigenDSizes<Rank>());
        pad_functor(
            device, output_slice->tensor<T, Rank>(),
            const_cast<const Tensor&>(no_padding_slice).tensor<T, Rank>(),
            slice_paddings, T());
      } else {
        slice_functor(device, output_slice->tensor<T, Rank>(),
                      input->tensor<T, Rank>(), slice_indices,
                      base_slice_shape);
      }
    }
  }

  template <int Rank>
  void Slice(OpKernelContext* ctx, const Tensor* input) {
    if (num_slices_ == 1) {
      ctx->set_output(0, *input);
      return;
    }

    const auto& shape = input->shape().dim_sizes();
    const Device& device = ctx->eigen_device<Device>();

    TensorShape output_slice_shape;
    for (int dim = 0; dim < Rank; ++dim) {
      output_slice_shape.AddDim(shape[dim] / num_splits_[dim]);
    }
    const Eigen::DSizes<Eigen::DenseIndex, Rank> base_slice_shape =
        output_slice_shape.AsEigenDSizes<Rank>();
    functor::Slice<Device, T, Rank> slice_functor;

    for (int i = 0; i < num_slices_; ++i) {
      Tensor* output_slice = nullptr;
      OP_REQUIRES_OK(ctx, ctx->allocate_output(
                              /*index=*/i, output_slice_shape, &output_slice));
      Eigen::DSizes<Eigen::DenseIndex, Rank> slice_indices =
          GetSliceIndices<Rank>(num_splits_, base_slice_shape, i);
      slice_functor(device, output_slice->tensor<T, Rank>(),
                    input->tensor<T, Rank>(), slice_indices, base_slice_shape);
    }
  }

  std::vector<int32> num_splits_;
  int num_slices_ = 1;
  std::vector<int32> paddings_;
  bool has_paddings_ = false;
};

template <typename Device, typename T>
class XlaSplitNDOp : public XlaSplitNDBaseOp<Device, T, false> {
 public:
  explicit XlaSplitNDOp(OpKernelConstruction* ctx)
      : XlaSplitNDBaseOp<Device, T, false>(ctx) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& input = ctx->input(0);
    this->ComputeInternal(ctx, &input);
  }
};

template <typename Device, typename T>
class ReadVariableXlaSplitNDOp : public XlaSplitNDBaseOp<Device, T, true> {
 public:
  explicit ReadVariableXlaSplitNDOp(OpKernelConstruction* ctx)
      : XlaSplitNDBaseOp<Device, T, true>(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("T", &dtype_));
  }

  void Compute(OpKernelContext* ctx) override {
    core::RefCountPtr<Var> variable;
    const ResourceHandle& handle = HandleFromInput(ctx, 0);
    const auto status = LookupResource(ctx, handle, &variable);
    OP_REQUIRES(
        ctx, status.ok(),
        errors::InvalidArgument("'resource' variable handle ('", handle.name(),
                                "') container ('", handle.container(),
                                "') cannot be found."));

    tf_shared_lock ml(*variable->mu());
    const Tensor* input = variable->tensor();
    OP_REQUIRES(ctx, input->dtype() == dtype_,
                errors::InvalidArgument(
                    "'T' must match 'resource' variable handle ('",
                    handle.name(), "') container ('", handle.container(),
                    "') dtype ", DataTypeString(input->dtype()), ", but got ",
                    DataTypeString(dtype_), "."));

    this->ComputeInternal(ctx, input);
  }

 private:
  DataType dtype_;
};

}  // anonymous namespace

#define REGISTER_XLA_SPLIT_ND(type)                                    \
  REGISTER_KERNEL_BUILDER(                                             \
      Name("XlaSplitND").Device(DEVICE_CPU).TypeConstraint<type>("T"), \
      XlaSplitNDOp<Eigen::ThreadPoolDevice, type>)

TF_CALL_POD_TYPES(REGISTER_XLA_SPLIT_ND);
TF_CALL_QUANTIZED_TYPES(REGISTER_XLA_SPLIT_ND);
#undef REGISTER_XLA_SPLIT_ND

#define REGISTER_READ_VARIABLE_XLA_SPLIT_ND(type) \
  REGISTER_KERNEL_BUILDER(                        \
      Name("ReadVariableXlaSplitND")              \
          .Device(DEVICE_CPU)                     \
          .TypeConstraint<type>("T"),             \
      ReadVariableXlaSplitNDOp<Eigen::ThreadPoolDevice, type>)

TF_CALL_POD_TYPES(REGISTER_READ_VARIABLE_XLA_SPLIT_ND);
TF_CALL_QUANTIZED_TYPES(REGISTER_READ_VARIABLE_XLA_SPLIT_ND);
#undef REGISTER_READ_VARIABLE_XLA_SPLIT_ND

}  // namespace tensorflow
