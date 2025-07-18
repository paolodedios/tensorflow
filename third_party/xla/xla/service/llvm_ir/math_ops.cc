/* Copyright 2018 The OpenXLA Authors.

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

#include "xla/service/llvm_ir/math_ops.h"

#include <array>

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "xla/service/llvm_ir/llvm_util.h"

namespace xla {
namespace llvm_ir {

llvm::Value* EmitFastTanh(llvm::IRBuilderBase* b, llvm::Value* input,
                          bool with_fma) {
  llvm::Type* type = input->getType();
  const float plus_clamp =
      with_fma ? 7.99881172180175781f : 7.90531110763549805f;
  const float minus_clamp = -plus_clamp;
  // Inputs in the range [plus_clamp, 9.0] may cause the output
  // of EmitFastTanh to be greater than 1, so we set the input to be at most
  // 'plus_clamp'. We choose 'plus_clamp' in a way that the
  // tanh approximation on that input is exactly 1.0. Similarly for
  // 'minus_clamp', where the tanh approximation will return exactly
  // -1.0.
  // Taken from tanh(Eigen/src/Core/MathFunctionsImpl.h).

  // For small values of x, we can approximate tanh(x)=x. For extremely small
  // values of x (|x| < 1e-37), the other approximation evaluates tanh(x) = 0.
  const auto kCanUseApprox = 0.0004;
  auto abs_x =
      llvm_ir::EmitCallToIntrinsic(llvm::Intrinsic::fabs, {input}, {type}, b);
  auto use_aprox =
      b->CreateFCmpOLT(abs_x, llvm::ConstantFP::get(type, kCanUseApprox));

  // To simplify the code base until it's an issue, don't have a slow min/max in
  // this approximation.
  llvm::Value* input_clamped = llvm_ir::EmitFloatMin(
      llvm_ir::EmitFloatMax(input, llvm::ConstantFP::get(type, minus_clamp), b,
                            /*enable_fast_min_max=*/true),
      llvm::ConstantFP::get(type, plus_clamp), b, /*enable_fast_min_max=*/true);

  static constexpr std::array<float, 7> numerator_coeffs{
      -2.76076847742355e-16f, 2.00018790482477e-13f, -8.60467152213735e-11f,
      5.12229709037114e-08f,  1.48572235717979e-05f, 6.37261928875436e-04f,
      4.89352455891786e-03f};

  static constexpr std::array<float, 4> denominator_coeffs{
      1.19825839466702e-06f, 1.18534705686654e-04f, 2.26843463243900e-03f,
      4.89352518554385e-03f};

  llvm::Value* input_squared = b->CreateFMul(input_clamped, input_clamped);
  llvm::Value* numerator = llvm::ConstantFP::get(type, numerator_coeffs[0]);
  for (int i = 1; i < numerator_coeffs.size(); i++) {
    numerator = b->CreateFAdd(b->CreateFMul(input_squared, numerator),
                              llvm::ConstantFP::get(type, numerator_coeffs[i]));
  }

  numerator = b->CreateFMul(input_clamped, numerator);

  llvm::Value* denominator = llvm::ConstantFP::get(type, denominator_coeffs[0]);
  for (int i = 1; i < denominator_coeffs.size(); i++) {
    denominator =
        b->CreateFAdd(b->CreateFMul(input_squared, denominator),
                      llvm::ConstantFP::get(type, denominator_coeffs[i]));
  }

  return b->CreateSelect(use_aprox, input,
                         b->CreateFDiv(numerator, denominator));
}

llvm::Value* EmitFastTanhF64(llvm::IRBuilderBase* b, llvm::Value* input,
                             bool with_fma) {
  llvm::Type* type = input->getType();

  // Clamp the inputs to the range [-c, c]. Everything outside this range will
  // output -1.0 or 1.0. The value c is chosen as the smallest floating point
  // argument such that the approximation is exactly 1.
  // Taken from `ptanh_double` in
  // Eigen/src/Core/arch/Default/GenericPacketMathFunctions.h.
  constexpr bool fast_min_max = true;
  const double clamp = with_fma ? 17.6610191624600077 : 17.714196154005176;
  llvm::Value* plus_clamp = llvm::ConstantFP::get(type, clamp);
  llvm::Value* minus_clamp = llvm::ConstantFP::get(type, -clamp);
  llvm::Value* x = llvm_ir::EmitFloatMin(
      llvm_ir::EmitFloatMax(input, minus_clamp, b, fast_min_max), plus_clamp, b,
      fast_min_max);

  // {alpha_19, alpha_17, ..., alpha_1}
  static constexpr std::array<double, 10> numerator_coeffs{
      2.6158007860482230e-23, 7.6534862268749319e-19,
      3.1309488231386680e-15, 4.2303918148209176e-12,
      2.4618379131293676e-09, 6.8644367682497074e-07,
      9.3839087674268880e-05, 5.9809711724441161e-03,
      1.5184719640284322e-01, 1.0};

  // {beta_18, beta_16, ..., beta_0}
  static constexpr std::array<double, 10> denominator_coeffs{
      6.463747022670968018e-21, 5.782506856739003571e-17,
      1.293019623712687916e-13, 1.123643448069621992e-10,
      4.492975677839633985e-08, 8.785185266237658698e-06,
      8.295161192716231542e-04, 3.437448108450402717e-02,
      4.851805297361760360e-01, 1.0};

  llvm::Value* x2 = b->CreateFMul(x, x);  // x^2.

  // Compute numerator polynomial.
  llvm::Value* numerator = llvm::ConstantFP::get(type, numerator_coeffs[0]);
  for (int i = 1; i < numerator_coeffs.size(); ++i) {
    llvm::Value* alpha = llvm::ConstantFP::get(type, numerator_coeffs[i]);
    numerator = b->CreateFAdd(b->CreateFMul(x2, numerator), alpha);
  }
  // Multiply by `x` for the odd terms.
  numerator = b->CreateFMul(x, numerator);

  // Compute denominator polynomial.
  llvm::Value* denominator = llvm::ConstantFP::get(type, denominator_coeffs[0]);
  for (int i = 1; i < denominator_coeffs.size(); i++) {
    llvm::Value* beta = llvm::ConstantFP::get(type, denominator_coeffs[i]);
    denominator = b->CreateFAdd(b->CreateFMul(x2, denominator), beta);
  }

  // Divide the numerator by the denominator.
  return b->CreateFDiv(numerator, denominator);
}

}  // namespace llvm_ir
}  // namespace xla
