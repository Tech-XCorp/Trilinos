/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER
*/

#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>

#include <string>
#include <stdexcept>

#include "KokkosSparse_CrsMatrix.hpp"
#include "KokkosKernels_IOUtils.hpp"
#include "KokkosBlas1_nrm2.hpp"
#include "KokkosSparse_spmv.hpp"
#include "KokkosSparse_gmres.hpp"
#include "KokkosSparse_MatrixPrec.hpp"

#include <gtest/gtest.h>

using namespace KokkosSparse;
using namespace KokkosSparse::Experimental;
using namespace KokkosKernels;
using namespace KokkosKernels::Experimental;

namespace Test {

template <class T>
struct TolMeta {
  static constexpr T value = 1e-8;
};

template <>
struct TolMeta<float> {
  static constexpr float value = 1e-5;  // Lower tolerance for floats
};

template <typename scalar_t, typename lno_t, typename size_type,
          typename device>
void run_test_gmres() {
  using exe_space = typename device::execution_space;
  using mem_space = typename device::memory_space;
  using sp_matrix_type =
      KokkosSparse::CrsMatrix<scalar_t, lno_t, device, void, size_type>;
  using KernelHandle = KokkosKernels::Experimental::KokkosKernelsHandle<
      size_type, lno_t, scalar_t, exe_space, mem_space, mem_space>;
  using float_t = typename Kokkos::ArithTraits<scalar_t>::mag_type;

  // Create a diagonally dominant sparse matrix to test:
  constexpr auto n             = 5000;
  constexpr auto m             = 15;
  constexpr auto tol           = TolMeta<float_t>::value;
  constexpr auto numRows       = n;
  constexpr auto numCols       = n;
  constexpr auto diagDominance = 1;
  constexpr bool verbose       = false;

  typename sp_matrix_type::non_const_size_type nnz = 10 * numRows;
  auto A = KokkosSparse::Impl::kk_generate_diagonally_dominant_sparse_matrix<
      sp_matrix_type>(numRows, numCols, nnz, 0, lno_t(0.01 * numRows),
                      diagDominance);

  // Make kernel handles
  KernelHandle kh;
  kh.create_gmres_handle(m, tol);
  auto gmres_handle = kh.get_gmres_handle();
  using GMRESHandle =
      typename std::remove_reference<decltype(*gmres_handle)>::type;
  using ViewVectorType = typename GMRESHandle::nnz_value_view_t;

  // Set initial vectors:
  ViewVectorType X("X", n);    // Solution and initial guess
  ViewVectorType Wj("Wj", n);  // For checking residuals at end.
  ViewVectorType B(Kokkos::view_alloc(Kokkos::WithoutInitializing, "B"),
                   n);  // right-hand side vec

  gmres_handle->set_verbose(verbose);

  // Test CGS2
  {
    // Make rhs ones so that results are repeatable:
    Kokkos::deep_copy(B, 1.0);

    gmres(&kh, A, B, X);

    // Double check residuals at end of solve:
    float_t nrmB = KokkosBlas::nrm2(B);
    KokkosSparse::spmv("N", 1.0, A, X, 0.0, Wj);  // wj = Ax
    KokkosBlas::axpy(-1.0, Wj, B);                // b = b-Ax.
    float_t endRes = KokkosBlas::nrm2(B) / nrmB;

    const auto conv_flag = gmres_handle->get_conv_flag_val();

    EXPECT_LT(endRes, gmres_handle->get_tol());
    EXPECT_EQ(conv_flag, GMRESHandle::Flag::Conv);
  }

  // Test MGS
  {
    gmres_handle->reset_handle(m, tol);
    gmres_handle->set_ortho(GMRESHandle::Ortho::MGS);
    gmres_handle->set_verbose(verbose);

    // reset X for next gmres call
    Kokkos::deep_copy(X, 0.0);

    gmres(&kh, A, B, X);

    // Double check residuals at end of solve:
    float_t nrmB = KokkosBlas::nrm2(B);
    KokkosSparse::spmv("N", 1.0, A, X, 0.0, Wj);  // wj = Ax
    KokkosBlas::axpy(-1.0, Wj, B);                // b = b-Ax.
    float_t endRes = KokkosBlas::nrm2(B) / nrmB;

    const auto conv_flag = gmres_handle->get_conv_flag_val();

    EXPECT_LT(endRes, gmres_handle->get_tol());
    EXPECT_EQ(conv_flag, GMRESHandle::Flag::Conv);
  }

  // Test GSS2 with simple preconditioner
  {
    gmres_handle->reset_handle(m, tol);
    gmres_handle->set_verbose(verbose);

    // Make precond
    auto myPrec = new KokkosSparse::Experimental::MatrixPrec<sp_matrix_type>(A);

    // reset X for next gmres call
    Kokkos::deep_copy(X, 0.0);

    gmres(&kh, A, B, X, myPrec);

    // Double check residuals at end of solve:
    float_t nrmB = KokkosBlas::nrm2(B);
    KokkosSparse::spmv("N", 1.0, A, X, 0.0, Wj);  // wj = Ax
    KokkosBlas::axpy(-1.0, Wj, B);                // b = b-Ax.
    float_t endRes = KokkosBlas::nrm2(B) / nrmB;

    const auto conv_flag = gmres_handle->get_conv_flag_val();

    EXPECT_LT(endRes, gmres_handle->get_tol());
    EXPECT_EQ(conv_flag, GMRESHandle::Flag::Conv);

    delete myPrec;
  }
}

}  // namespace Test

template <typename scalar_t, typename lno_t, typename size_type,
          typename device>
void test_gmres() {
  Test::run_test_gmres<scalar_t, lno_t, size_type, device>();
}

#define KOKKOSKERNELS_EXECUTE_TEST(SCALAR, ORDINAL, OFFSET, DEVICE)       \
  TEST_F(TestCategory,                                                    \
         sparse##_##gmres##_##SCALAR##_##ORDINAL##_##OFFSET##_##DEVICE) { \
    test_gmres<SCALAR, ORDINAL, OFFSET, DEVICE>();                        \
  }

#include <Test_Common_Test_All_Type_Combos.hpp>

#undef KOKKOSKERNELS_EXECUTE_TEST
