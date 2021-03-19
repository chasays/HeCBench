/*
* Portions Copyright (c) 1993-2015 NVIDIA Corporation.  All rights reserved.
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
* Portions Copyright (c) 2009 Mike Giles, Oxford University.  All rights reserved.
* Portions Copyright (c) 2008 Frances Y. Kuo and Stephen Joe.  All rights reserved.
*
* Sobol Quasi-random Number Generator example
*
* Based on CUDA code submitted by Mike Giles, Oxford University, United Kingdom
* http://people.maths.ox.ac.uk/~gilesm/
*
* and C code developed by Stephen Joe, University of Waikato, New Zealand
* and Frances Kuo, University of New South Wales, Australia
* http://web.maths.unsw.edu.au/~fkuo/sobol/
*
* For theoretical background see:
*
* P. Bratley and B.L. Fox.
* Implementing Sobol's quasirandom sequence generator
* http://portal.acm.org/citation.cfm?id=42288
* ACM Trans. on Math. Software, 14(1):88-100, 1988
*
* S. Joe and F. Kuo.
* Remark on algorithm 659: implementing Sobol's quasirandom sequence generator.
* http://portal.acm.org/citation.cfm?id=641879
* ACM Trans. on Math. Software, 29(1):49-57, 2003
*
*/

#define DPCT_USM_LEVEL_NONE
#include <CL/sycl.hpp>
#include <dpct/dpct.hpp>
#include "sobol.h"
#include "sobol_gpu.h"

#define k_2powneg32 2.3283064E-10F


int _ffs(const int x) {
  for (int i = 0; i < 32; i++)
    if ((x >> i) & 1) return (i+1);
  return 0;
};

void sobolGPU_kernel(unsigned n_vectors, unsigned n_dimensions, unsigned *d_directions, float *d_output,
                     sycl::nd_item<3> item_ct1, unsigned int *v)
{
    // Handle to thread block group
    //cg::thread_block cta = cg::this_thread_block();

    // Offset into the correct dimension as specified by the
    // block y coordinate
    d_directions = d_directions + n_directions * item_ct1.get_group(1);
    d_output = d_output + n_vectors * item_ct1.get_group(1);

    // Copy the direction numbers for this dimension into shared
    // memory - there are only 32 direction numbers so only the
    // first 32 (n_directions) threads need participate.
    if (item_ct1.get_local_id(2) < n_directions)
    {
        v[item_ct1.get_local_id(2)] = d_directions[item_ct1.get_local_id(2)];
    }

    //cg::sync(cta);
    item_ct1.barrier();

    // Set initial index (i.e. which vector this thread is
    // computing first) and stride (i.e. step to the next vector
    // for this thread)
    int i0 = item_ct1.get_local_id(2) +
             item_ct1.get_group(2) * item_ct1.get_local_range().get(2);
    int stride = item_ct1.get_group_range(2) * item_ct1.get_local_range().get(2);

    // Get the gray code of the index
    // c.f. Numerical Recipes in C, chapter 20
    // http://www.nrbook.com/a/bookcpdf/c20-2.pdf
    unsigned int g = i0 ^ (i0 >> 1);

    // Initialisation for first point x[i0]
    // In the Bratley and Fox paper this is equation (*), where
    // we are computing the value for x[n] without knowing the
    // value of x[n-1].
    unsigned int X = 0;
    unsigned int mask;

    for (unsigned int k = 0 ; k < _ffs(stride) - 1 ; k++)
    {
        // We want X ^= g_k * v[k], where g_k is one or zero.
        // We do this by setting a mask with all bits equal to
        // g_k. In reality we keep shifting g so that g_k is the
        // LSB of g. This way we avoid multiplication.
        mask = - (g & 1);
        X ^= mask & v[k];
        g = g >> 1;
    }

    if (i0 < n_vectors)
    {
        d_output[i0] = (float)X * k_2powneg32;
    }

    // Now do rest of points, using the stride
    // Here we want to generate x[i] from x[i-stride] where we
    // don't have any of the x in between, therefore we have to
    // revisit the equation (**), this is easiest with an example
    // so assume stride is 16.
    // From x[n] to x[n+16] there will be:
    //   8 changes in the first bit
    //   4 changes in the second bit
    //   2 changes in the third bit
    //   1 change in the fourth
    //   1 change in one of the remaining bits
    //
    // What this means is that in the equation:
    //   x[n+1] = x[n] ^ v[p]
    //   x[n+2] = x[n+1] ^ v[q] = x[n] ^ v[p] ^ v[q]
    //   ...
    // We will apply xor with v[1] eight times, v[2] four times,
    // v[3] twice, v[4] once and one other direction number once.
    // Since two xors cancel out, we can skip even applications
    // and just apply xor with v[4] (i.e. log2(16)) and with
    // the current applicable direction number.
    // Note that all these indices count from 1, so we need to
    // subtract 1 from them all to account for C arrays counting
    // from zero.
    unsigned int v_log2stridem1 = v[_ffs(stride) - 2];
    unsigned int v_stridemask = stride - 1;

    for (unsigned int i = i0 + stride ; i < n_vectors ; i += stride)
    {
        // x[i] = x[i-stride] ^ v[b] ^ v[c]
        //  where b is log2(stride) minus 1 for C array indexing
        //  where c is the index of the rightmost zero bit in i,
        //  not including the bottom log2(stride) bits, minus 1
        //  for C array indexing
        // In the Bratley and Fox paper this is equation (**)
        X ^= v_log2stridem1 ^ v[_ffs(~((i - stride) | v_stridemask)) - 1];
        d_output[i] = (float)X * k_2powneg32;
    }
}

void sobolGPU(int n_vectors, int n_dimensions, unsigned int *d_directions, float *d_output)
{
    const int threadsperblock = 64;

    // Set up the execution configuration
    sycl::range<3> dimGrid(1, 1, 1);
    sycl::range<3> dimBlock(1, 1, 1);

    // This implementation of the generator outputs all the draws for
    // one dimension in a contiguous region of memory, followed by the
    // next dimension and so on.
    // Therefore all threads within a block will be processing different
    // vectors from the same dimension. As a result we want the total
    // number of blocks to be a multiple of the number of dimensions.
    dimGrid[1] = n_dimensions;

    // If the number of dimensions is large then we will set the number
    // of blocks to equal the number of dimensions (i.e. dimGrid.x = 1)
    // but if the number of dimensions is small, then we'll partition 
    // the vectors across blocks (as well as threads).
    if (n_dimensions < (4 * 24))
    {
        dimGrid[2] = 4 * 24;
    }
    else
    {
        dimGrid[2] = 1;
    }

    // Cap the dimGrid.x if the number of vectors is small
    if (dimGrid[2] > (unsigned int)(n_vectors / threadsperblock))
    {
        dimGrid[2] = (n_vectors + threadsperblock - 1) / threadsperblock;
    }

    // Round up to a power of two, required for the algorithm so that
    // stride is a power of two.
    unsigned int targetDimGridX = dimGrid[2];

    for (dimGrid[2] = 1; dimGrid[2] < targetDimGridX; dimGrid[2] *= 2);
    // Fix the number of threads
    dimBlock[2] = threadsperblock;

    // Execute GPU kernel
    for (int i = 0; i < 100; i++)
      /*
      DPCT1049:0: The workgroup size passed to the SYCL kernel may exceed the
      limit. To get the device limit, query info::device::max_work_group_size.
      Adjust the workgroup size if needed.
      */
   {
      std::pair<dpct::buffer_t, size_t> d_directions_buf_ct2 =
          dpct::get_buffer_and_offset(d_directions);
      size_t d_directions_offset_ct2 = d_directions_buf_ct2.second;
      std::pair<dpct::buffer_t, size_t> d_output_buf_ct3 =
          dpct::get_buffer_and_offset(d_output);
      size_t d_output_offset_ct3 = d_output_buf_ct3.second;
      dpct::get_default_queue().submit([&](sycl::handler &cgh) {
         sycl::accessor<unsigned int, 1, sycl::access::mode::read_write,
                        sycl::access::target::local>
             v_acc_ct1(sycl::range<1>(32 /*n_directions*/), cgh);
         auto d_directions_acc_ct2 =
             d_directions_buf_ct2.first
                 .get_access<sycl::access::mode::read_write>(cgh);
         auto d_output_acc_ct3 =
             d_output_buf_ct3.first.get_access<sycl::access::mode::read_write>(
                 cgh);

         cgh.parallel_for(sycl::nd_range<3>(dimGrid * dimBlock, dimBlock),
                          [=](sycl::nd_item<3> item_ct1) {
                             unsigned int *d_directions_ct2 =
                                 (unsigned int *)(&d_directions_acc_ct2[0] +
                                                  d_directions_offset_ct2);
                             float *d_output_ct3 =
                                 (float *)(&d_output_acc_ct3[0] +
                                           d_output_offset_ct3);
                             sobolGPU_kernel(n_vectors, n_dimensions,
                                             d_directions_ct2, d_output_ct3,
                                             item_ct1, v_acc_ct1.get_pointer());
                          });
      });
   }
}
