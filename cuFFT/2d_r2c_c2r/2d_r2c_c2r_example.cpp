/*
 * Copyright 2020 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO LICENSEE:
 *
 * This source code and/or documentation ("Licensed Deliverables") are
 * subject to NVIDIA intellectual property rights under U.S. and
 * international Copyright laws.
 *
 * These Licensed Deliverables contained herein is PROPRIETARY and
 * CONFIDENTIAL to NVIDIA and is being provided under the terms and
 * conditions of a form of NVIDIA software license agreement by and
 * between NVIDIA and Licensee ("License Agreement") or electronically
 * accepted by Licensee.  Notwithstanding any terms or conditions to
 * the contrary in the License Agreement, reproduction or disclosure
 * of the Licensed Deliverables to any third party without the express
 * written consent of NVIDIA is prohibited.
 *
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE
 * LICENSE AGREEMENT, NVIDIA MAKES NO REPRESENTATION ABOUT THE
 * SUITABILITY OF THESE LICENSED DELIVERABLES FOR ANY PURPOSE.  IT IS
 * PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.
 * NVIDIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THESE LICENSED
 * DELIVERABLES, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE
 * LICENSE AGREEMENT, IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY
 * SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THESE LICENSED DELIVERABLES.
 *
 * U.S. Government End Users.  These Licensed Deliverables are a
 * "commercial item" as that term is defined at 48 C.F.R. 2.101 (OCT
 * 1995), consisting of "commercial computer software" and "commercial
 * computer software documentation" as such terms are used in 48
 * C.F.R. 12.212 (SEPT 1995) and is provided to the U.S. Government
 * only as a commercial end item.  Consistent with 48 C.F.R.12.212 and
 * 48 C.F.R. 227.7202-1 through 227.7202-4 (JUNE 1995), all
 * U.S. Government End Users acquire the Licensed Deliverables with
 * only those rights set forth herein.
 *
 * Any use of the Licensed Deliverables in individual and commercial
 * software must include, in the user documentation and internal
 * comments to the code, the above Disclaimer and U.S. Government End
 * Users Notice.
 */

#include <array>
#include <complex>
#include <iostream>
#include <vector>
#include <cufft.h>
#include "cufft_utils.h"

using dim_t = std::array<int, 2>;

__global__
void scaling_kernel_2(cufftComplex* data, int width, int height, int r_thresh, float scale) {
    int row = blockIdx.y*blockDim.y + threadIdx.y;
	int col = blockIdx.x*blockDim.x + threadIdx.x;
    int w = min(width, r_thresh);
    int h = min(height, r_thresh);
	if(col < w && row < h ){
        data[row*width + col].x *= scale;
        data[row*width + col].y *= scale;
    }
    //else if( height - row <= r_thresh && width-col <= r_thresh ){
      //   if( height > row && width-col <= r_thresh ){*/
    else if( height - row <= r_thresh && width > col){
        data[row*width + col].x *= scale;
        data[row*width + col].y *= scale;
    }
}



__global__
void scaling_kernel_1(cufftReal* data, int width, int height, float scale) {
    // const int tid = threadIdx.x + blockIdx.x * blockDim.x;
    // const int stride = blockDim.x * gridDim.x;
    // for (auto i = tid; i<element_count; i+= stride) {
    //     data[tid] *= scale;
    // }

    int row = blockIdx.y*blockDim.y + threadIdx.y;
	int col = blockIdx.x*blockDim.x + threadIdx.x;

	if(col < width && row < height){
        data[row*width + col] *= scale;
    }


}



int main(int argc, char *argv[]) {
    cufftHandle planc2r, planr2c;
    cudaStream_t stream = NULL;

    int nx = 5;
    int ny = 5;
    dim_t fft_size = {nx, ny};
    unsigned int batch_size = 1;

    using scalar_type = float;
    using input_type = scalar_type;
    using output_type = std::complex<scalar_type>;

    std::vector<input_type> input_real(batch_size * nx * ny);
    std::vector<output_type> output_complex(batch_size * nx * (ny/2 + 1));

    for (int i = 0; i < input_real.size(); i++) {
        input_real[i] = (scalar_type)i+1.0f;
    }

    std::printf("Input array:\n");
    for (auto &i : input_real) {
        std::printf("%f \n", i);
    }
    std::printf("=====\n");

    cufftReal *d_data = nullptr;
    cufftComplex *d_data_2 = nullptr;

    CUFFT_CALL(cufftCreate(&planc2r));
    CUFFT_CALL(cufftCreate(&planr2c));
    // inembed/onembed being nullptr indicates contiguous data for each batch, then the stride and dist settings are ignored
    CUFFT_CALL(cufftPlanMany(&planc2r, fft_size.size(), fft_size.data(), 
                             nullptr, 1, 0, // *inembed, istride, idist 
                             nullptr, 1, 0, // *onembed, ostride, odist
                             CUFFT_C2R, batch_size));
    CUFFT_CALL(cufftPlanMany(&planr2c, fft_size.size(), fft_size.data(), 
                             nullptr, 1, 0, // *inembed, istride, idist
                             nullptr, 1, 0, // *onembed, ostride, odist
                             CUFFT_R2C, batch_size));
    CUDA_RT_CALL(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    CUFFT_CALL(cufftSetStream(planc2r, stream));
    CUFFT_CALL(cufftSetStream(planr2c, stream));

    // Create device arrays
    // For in-place r2c/c2r transforms, make sure the device array is always allocated to the size of complex array 
    CUDA_RT_CALL(
        cudaMalloc(reinterpret_cast<void **>(&d_data), sizeof(input_type) * input_real.size()));
    CUDA_RT_CALL(
        cudaMalloc(reinterpret_cast<void **>(&d_data_2), sizeof(output_type) * output_complex.size()));    

    CUDA_RT_CALL(cudaMemcpyAsync(d_data, (input_real.data()), sizeof(input_type) * input_real.size(),
                                 cudaMemcpyHostToDevice, stream));

    // C2R
    CUFFT_CALL(cufftExecR2C(planr2c, d_data, d_data_2));

    CUDA_RT_CALL(cudaMemcpyAsync(output_complex.data(), d_data_2, sizeof(output_type) * output_complex.size(),
                                 cudaMemcpyDeviceToHost, stream));

    CUDA_RT_CALL(cudaStreamSynchronize(stream));

    std::printf("Output transforms before scaling:\n");
   
    for (auto &i : output_complex) {
        std::printf("%f + %fj\n", i.real(), i.imag());
    }
    std::printf("=====\n");

    dim3 dimGrid(ceil(nx/16.0), ceil((ny/2+1)/16.0), batch_size); 
    dim3 dimBlock(16, 16, 1);
    scaling_kernel_2<<<dimGrid, dimBlock, 0, stream>>>(d_data_2, nx, ny/2+1, 1, 0.7);


    CUDA_RT_CALL(cudaMemcpyAsync(output_complex.data(), d_data_2, sizeof(output_type) * output_complex.size(),
                                 cudaMemcpyDeviceToHost, stream));

    CUDA_RT_CALL(cudaStreamSynchronize(stream));

    std::printf("Output transforms after scaling:\n");
   
    for (auto &i : output_complex) {
        std::printf("%f + %fj\n", i.real(), i.imag());
    }
    std::printf("=====\n");

    // Normalize the data
    // scaling_kernel<<<1, 128, 0, stream>>>(d_data, input_complex.size(), 1.f/(nx * ny));

    // C2R 
    CUFFT_CALL(cufftExecC2R(planc2r, d_data_2, d_data));

    dim3 dimGrid1(ceil(nx/16.0), ceil(ny/16.0), batch_size); 
    dim3 dimBlock1 (16, 16, 1);
    scaling_kernel_1<<<dimGrid1, dimBlock1, 0, stream>>>(d_data, nx, ny, 1.f/(nx * ny));

    CUDA_RT_CALL(cudaMemcpyAsync(input_real.data(), d_data, sizeof(input_type) * input_real.size(),
                                 cudaMemcpyDeviceToHost, stream));

    std::printf("Output array after R2C and C2R:\n");
    for (auto i = 0; i < input_real.size(); i++) {
        std::printf("%f\n", input_real[i]);
    }
    std::printf("=====\n");


    /* free resources */
    CUDA_RT_CALL(cudaFree(d_data));
    CUDA_RT_CALL(cudaFree(d_data_2));

    CUFFT_CALL(cufftDestroy(planc2r));
    CUFFT_CALL(cufftDestroy(planr2c));

    CUDA_RT_CALL(cudaStreamDestroy(stream));

    CUDA_RT_CALL(cudaDeviceReset());

    return EXIT_SUCCESS;
}