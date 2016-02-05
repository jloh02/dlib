// Copyright (C) 2015  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_DNN_CPU_cPP_
#define DLIB_DNN_CPU_cPP_

// This file contains CPU implementations of the GPU based functions in cuda_dlib.h

#include "cpu_dlib.h"

namespace dlib
{
    namespace cpu 
    {

    // -----------------------------------------------------------------------------------

        void multiply (
            tensor& dest,
            const tensor& src1,
            const tensor& src2
        )
        {
            DLIB_CASSERT(dest.k() == src1.k() && src1.k() == src2.k() &&
                dest.nr() == src1.nr() && src1.nr() == src2.nr() &&
                dest.nc() == src1.nc() && src1.nc() == src2.nc() ,"");
            const long MD = std::max(std::max(dest.num_samples(),src1.num_samples()),src2.num_samples());
            DLIB_CASSERT((dest.num_samples()==1 || dest.num_samples()==MD) &&
                (src1.num_samples()==1 || src1.num_samples()==MD) &&
                (src2.num_samples()==1 || src2.num_samples()==MD) ,"");

            if (dest.size() == 0)
                return;

            const size_t max_size = std::max(std::max(dest.size(),src1.size()),src2.size());
            const auto d = dest.host();
            const auto s1 = src1.host();
            const auto s2 = src2.host();
            if (dest.size() == src1.size() && src1.size() == src2.size())
            {
                for (size_t i = 0; i < src1.size(); ++i)
                    d[i] = s1[i]*s2[i];
            }
            else if (dest.num_samples() == 1)
            {
                for (size_t i = 0; i < dest.size(); ++i)
                    d[i] = 0;
                for (size_t i = 0; i < max_size; ++i)
                    d[i%dest.size()] += s1[i%src1.size()]*s2[i%src2.size()];
            }
            else
            {
                for (size_t i = 0; i < max_size; ++i)
                    d[i] = s1[i%src1.size()]*s2[i%src2.size()];
            }
        }

        void multiply_conv (
            tensor& dest,
            const tensor& src1,
            const tensor& src2
        )
        {
            auto d = dest.host();
            auto s1 = src1.host();
            auto s2 = src2.host();
            if (have_same_dimensions(dest,src1))
            {
                DLIB_CASSERT(src2.num_samples() == 1 && src2.nr() == 1 && src2.nc() == 1 && src2.k() == src1.k(),"");

                for (long n = 0; n < dest.num_samples(); ++n)
                {
                    for (long k = 0; k < dest.k(); ++k)
                    {
                        for (long r = 0; r < dest.nr(); ++r)
                        {
                            for (long c = 0; c < dest.nc(); ++c)
                            {
                                *d++ = (*s1++)*s2[k];
                            }
                        }
                    }
                }
            }
            else
            {
                DLIB_CASSERT(have_same_dimensions(src1,src2),"");
                DLIB_CASSERT(dest.num_samples() == 1 && dest.nr() == 1 && dest.nc() == 1 && dest.k() == src1.k(),"");

                for (long k = 0; k < src1.k(); ++k)
                    d[k] = 0;

                for (long n = 0; n < src1.num_samples(); ++n)
                {
                    for (long k = 0; k < src1.k(); ++k)
                    {
                        for (long r = 0; r < src1.nr(); ++r)
                        {
                            for (long c = 0; c < src1.nc(); ++c)
                            {
                                d[k] += (*s1++)*(*s2++);
                            }
                        }
                    }
                }
            }
        }

        void add(
            float beta,
            tensor& dest,
            float alpha,
            const tensor& src
        )
        {
            DLIB_CASSERT(
                  (have_same_dimensions(src, dest) ||
                  (src.num_samples()==1 && src.k()==dest.k() && src.nr()==1 && src.nc()==1) ||
                  (src.num_samples()==1 && src.k()==dest.k() && src.nr()==dest.nr() && src.nc()==dest.nc()) ||
                  (src.num_samples()==1 && src.k()==1 && src.nr()==dest.nr() && src.nc()==dest.nc())) &&
                  is_same_object(src,dest) == false , 
                    "\n\t dest.num_samples(): " << dest.num_samples()
                    <<"\n\t dest.k():           " << dest.k()
                    <<"\n\t dest.nr():          " << dest.nr()
                    <<"\n\t dest.nc():          " << dest.nc()
                    <<"\n\t src.num_samples():  " << src.num_samples()
                    <<"\n\t src.k():            " << src.k()
                    <<"\n\t src.nr():           " << src.nr()
                    <<"\n\t src.nc():           " << src.nc()
                    );


            if (beta == 0 && alpha == 0)
            {
                dest = 0;
                return;
            }

            auto d = dest.host();
            auto s = src.host();
            for (long n = 0; n < dest.num_samples(); ++n)
            {
                const auto sn = src.num_samples()==1 ? 0:n;
                for (long k = 0; k < dest.k(); ++k)
                {
                    const auto sk = src.k()==1 ? 0:k;
                    for (long r = 0; r < dest.nr(); ++r)
                    {
                        const auto sr = src.nr()==1 ? 0:r;
                        for (long c = 0; c < dest.nc(); ++c)
                        {
                            const auto sc = src.nc()==1 ? 0:c;

                            const auto s_idx = ((sn*src.k() + sk)*src.nr() + sr)*src.nc() + sc;
                            *d = beta*(*d) + alpha*s[s_idx];
                            ++d;
                        }
                    }
                }
            }
        }

    // ----------------------------------------------------------------------------------------

        void add (
            tensor& dest,
            const tensor& src1,
            const tensor& src2
        )
        {
            auto d = dest.host();
            auto s1 = src1.host();
            auto s2 = src2.host();

            // Do the simple and fast version if everything has the same dimensions
            if (have_same_dimensions(dest, src1) &&
                have_same_dimensions(dest, src2))
            {
                for (size_t i = 0; i < dest.size(); ++i)
                    d[i] = s1[i] + s2[i];
                return;
            }

            // Otherwise, do the more complex version with bounds checking.
            for (long n = 0; n < dest.num_samples(); ++n)
            {
                for (long k = 0; k < dest.k(); ++k)
                {
                    for (long r = 0; r < dest.nr(); ++r)
                    {
                        for (long c = 0; c < dest.nc(); ++c)
                        {
                            float v1 = 0;
                            float v2 = 0;

                            // if this index is inside src1
                            if (n < src1.num_samples() && 
                                k < src1.k() && 
                                r < src1.nr() && 
                                c < src1.nc() )
                            {
                                const auto s_idx = ((n*src1.k() + k)*src1.nr() + r)*src1.nc() + c;
                                v1 = s1[s_idx];
                            }

                            // if this index is inside src2
                            if (n < src2.num_samples() && 
                                k < src2.k() && 
                                r < src2.nr() && 
                                c < src2.nc() )
                            {
                                const auto s_idx = ((n*src2.k() + k)*src2.nr() + r)*src2.nc() + c;
                                v2 = s2[s_idx];
                            }

                            *d = v1 + v2;
                            ++d;
                        }
                    }
                }
            }
        }

    // ----------------------------------------------------------------------------------------

        void assign_bias_gradient (
            tensor& grad,
            const tensor& gradient_input
        )
        {
            DLIB_CASSERT(
                  grad.num_samples() == 1 &&
                  gradient_input.k() == grad.k() &&
                  gradient_input.nr() == grad.nr() &&
                  gradient_input.nc() == grad.nc() &&
                  gradient_input.size() > 0,"");

            auto out = grad.host();
            auto in = gradient_input.host();

            for (size_t i = 0; i < grad.size(); ++i)
                out[i] = *in++;

            for (long i = 1; i < gradient_input.num_samples(); ++i)
            {
                for (size_t i = 0; i < grad.size(); ++i)
                    out[i] += *in++;
            }
        }

    // ------------------------------------------------------------------------------------

        void assign_conv_bias_gradient (
            tensor& grad,
            const tensor& gradient_input
        )
        {
            DLIB_CASSERT(
                  grad.num_samples() == 1 &&
                  grad.k()  >= 1 &&
                  grad.nr() == 1 &&
                  grad.nc() == 1 &&
                  gradient_input.k() == grad.k() &&
                  gradient_input.size() > 0 && 
                  is_same_object(grad,gradient_input) == false
                  ,"");

            auto g = grad.host();
            auto gi = gradient_input.host();

            for (long k = 0; k < gradient_input.k(); ++k)
                g[k] = 0;

            for (long n = 0; n < gradient_input.num_samples(); ++n)
            {
                for (long k = 0; k < gradient_input.k(); ++k)
                {
                    for (long r = 0; r < gradient_input.nr(); ++r)
                    {
                        for (long c = 0; c < gradient_input.nc(); ++c)
                        {
                            g[k] += (*gi++);
                        }
                    }
                }
            }
        }

    // -----------------------------------------------------------------------------------

        void affine_transform(
            tensor& dest,
            const tensor& src,
            const float A,
            const float B
        )
        {
            DLIB_CASSERT(dest.size()==src.size(),"");
            const auto d = dest.host();
            const auto s = src.host();
            for (size_t i = 0; i < src.size(); ++i)
                d[i] = A*s[i] + B;
        }

        void affine_transform(
            tensor& dest,
            const tensor& src1,
            const tensor& src2,
            const float A,
            const float B,
            const float C
        )
        {
            DLIB_CASSERT(dest.size()==src1.size(),"");
            DLIB_CASSERT(dest.size()==src2.size(),"");
            const auto d = dest.host();
            const auto s1 = src1.host();
            const auto s2 = src2.host();
            for (size_t i = 0; i < src1.size(); ++i)
                d[i] = A*s1[i] + B*s2[i] + C;
        }

        void affine_transform(
            tensor& dest,
            const tensor& src1,
            const tensor& src2,
            const tensor& src3,
            const float A,
            const float B,
            const float C,
            const float D
        )
        {
            DLIB_CASSERT(dest.size()==src1.size(),"");
            DLIB_CASSERT(dest.size()==src2.size(),"");
            DLIB_CASSERT(dest.size()==src3.size(),"");
            const auto d = dest.host();
            const auto s1 = src1.host();
            const auto s2 = src2.host();
            const auto s3 = src3.host();
            for (size_t i = 0; i < src1.size(); ++i)
                d[i] = A*s1[i] + B*s2[i] + C*s3[i] + D;
        }

    // -----------------------------------------------------------------------------------

        void affine_transform(
            tensor& dest,
            const tensor& src,
            const tensor& A,
            const tensor& B
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest,src),"");
            DLIB_CASSERT(
                  ((A.num_samples()==1 && B.num_samples()==1) ||
                  (A.num_samples()==src.num_samples() && B.num_samples()==src.num_samples())) &&
                  A.nr()==B.nr() && B.nr()==src.nr() &&
                  A.nc()==B.nc() && B.nc()==src.nc() &&
                  A.k() ==B.k()  && B.k()==src.k(),"");

            auto d = dest.host();
            auto s = src.host();
            const auto a = A.host();
            const auto b = B.host();
            if (A.num_samples() == 1)
            {
                const long num = src.size()/src.num_samples();
                for (size_t i = 0; i < src.num_samples(); ++i)
                {
                    for (long j = 0; j < num; ++j)
                    {
                        *d = a[j]*(*s) + b[j];
                        d++;
                        s++;
                    }
                }
            }
            else
            {
                for (size_t i = 0; i < src.size(); ++i)
                    d[i] = a[i]*s[i] + b[i];
            }
        }

    // -----------------------------------------------------------------------------------

        void affine_transform_conv(
            tensor& dest,
            const tensor& src,
            const tensor& A,
            const tensor& B
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest,src),"");
            DLIB_CASSERT(have_same_dimensions(A,B),"");
            DLIB_CASSERT(A.num_samples() == 1 &&
                         A.nr() == 1 &&
                         A.nc() == 1 &&
                         A.k() == src.k(), "");

            auto d = dest.host();
            auto s = src.host();
            const auto a = A.host();
            const auto b = B.host();
            for (long n = 0; n < dest.num_samples(); ++n)
            {
                for (long k = 0; k < dest.k(); ++k)
                {
                    for (long r = 0; r < dest.nr(); ++r)
                    {
                        for (long c = 0; c < dest.nc(); ++c)
                        {
                            *d++ = a[k]*(*s++) + b[k];
                        }
                    }
                }
            }
        }

    // -----------------------------------------------------------------------------------

        void batch_normalize_inference (
            resizable_tensor& dest,
            const tensor& src,
            const tensor& gamma, 
            const tensor& beta,
            const tensor& running_means,
            const tensor& running_invstds
        )
        {
            DLIB_CASSERT(
                gamma.num_samples() == 1 && 
                gamma.nr() == src.nr() &&
                gamma.nc() == src.nc() &&
                gamma.k()  == src.k() &&
                have_same_dimensions(gamma, beta) &&
                have_same_dimensions(gamma, running_means) &&
                have_same_dimensions(gamma, running_invstds), 
                "\ngamma.num_samples(): " << gamma.num_samples() << 
                "\ngamma.k():  " << gamma.k() << 
                "\ngamma.nr(): " << gamma.nr() << 
                "\ngamma.nc(): " << gamma.nc() << 
                "\nbeta.num_samples(): " << beta.num_samples() << 
                "\nbeta.k():   " << beta.k() << 
                "\nbeta.nr():  " << beta.nr() << 
                "\nbeta.nc():  " << beta.nc() << 
                "\nrunning_means.num_samples(): " << running_means.num_samples() << 
                "\nrunning_means.k():   " << running_means.k() << 
                "\nrunning_means.nr():  " << running_means.nr() << 
                "\nrunning_means.nc():  " << running_means.nc() << 
                "\nrunning_invstds.num_samples(): " << running_invstds.num_samples() << 
                "\nrunning_invstds.k():   " << running_invstds.k() << 
                "\nrunning_invstds.nr():  " << running_invstds.nr() << 
                "\nrunning_invstds.nc():  " << running_invstds.nc() << 
                "\nsrc.k():   " << src.k() << 
                "\nsrc.nr():  " << src.nr() << 
                "\nsrc.nc():  " << src.nc() 
            );
            dest.copy_size(src);

            auto d = dest.host();
            auto s = src.host();
            auto g = gamma.host();
            auto b = beta.host();
            auto m = running_means.host();
            auto i = running_invstds.host();

            const long num = src.k()*src.nr()*src.nc();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < num; ++k)
                {
                    *d = g[k]*(*s - m[k])*i[k] + b[k];
                    ++d;
                    ++s;
                }
            }
        }

        void batch_normalize (
            resizable_tensor& dest,
            resizable_tensor& means,
            resizable_tensor& invstds,
            const double averaging_factor,
            resizable_tensor& running_means,
            resizable_tensor& running_invstds,
            const tensor& src,
            const tensor& gamma, 
            const tensor& beta 
        )
        {
            DLIB_CASSERT(0 <= averaging_factor && averaging_factor <= 1, "averaging_factor: " << averaging_factor);
            DLIB_CASSERT(averaging_factor==1 || have_same_dimensions(running_means,means),"");
            DLIB_CASSERT(averaging_factor==1 || have_same_dimensions(running_invstds,invstds),"");
            DLIB_CASSERT(
                src.num_samples() > 1 &&
                gamma.num_samples() == 1 && 
                beta.num_samples() == 1 && 
                gamma.nr() == beta.nr() && beta.nr() == src.nr() &&
                gamma.nc() == beta.nc() && beta.nc() == src.nc() &&
                gamma.k()  == beta.k()  && beta.k() == src.k(), 
                "\ngamma.num_samples(): " << gamma.num_samples() << 
                "\ngamma.k():  " << gamma.k() << 
                "\ngamma.nr(): " << gamma.nr() << 
                "\ngamma.nc(): " << gamma.nc() << 
                "\nbeta.num_samples(): " << beta.num_samples() << 
                "\nbeta.k():   " << beta.k() << 
                "\nbeta.nr():  " << beta.nr() << 
                "\nbeta.nc():  " << beta.nc() << 
                "\nsrc.k():   " << src.k() << 
                "\nsrc.nr():  " << src.nr() << 
                "\nsrc.nc():  " << src.nc() 
            );

            dest.copy_size(src);
            means.set_size(1, src.k(), src.nr(), src.nc());
            invstds.set_size(1, src.k(), src.nr(), src.nc());

            // first compute means and invstds
            means = 0;
            invstds = 0;
            const auto p_invstds = invstds.host();
            const auto p_means = means.host();
            auto p_src = src.host();
            const long num = src.k()*src.nr()*src.nc();
            // compute means, and sum of squares
            for (long i = 0; i < num; ++i)
            {
                for (long n = 0; n < src.num_samples(); ++n)
                {
                    float val = p_src[n*num+i];
                    p_means[i] += val;
                    p_invstds[i] += val*val;
                }
            }
            means /= src.num_samples();
            invstds /= src.num_samples();
            // copy data back to host
            invstds.host(); means.host();

            // compute variances 
            for (long i = 0; i < num; ++i)
            {
                auto actual_var = p_invstds[i] - p_means[i]*p_means[i];
                p_invstds[i] = 1.0f/std::sqrt(actual_var+BATCH_NORM_EPS);
            }

            p_src = src.host();
            auto p_dest = dest.host();
            const auto p_gamma = gamma.host();   
            const auto p_beta = beta.host();   
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long i = 0; i < num; ++i)
                {
                    *p_dest = (*p_src - p_means[i])*p_invstds[i];
                    *p_dest = (*p_dest)*p_gamma[i] + p_beta[i];
                    ++p_src;
                    ++p_dest;
                }
            }

            // now keep track of the running means and invstds
            running_means.copy_size(means);
            running_invstds.copy_size(invstds);
            if (averaging_factor != 1)
            {
                running_means = (1-averaging_factor)*mat(running_means) + averaging_factor*mat(means);
                running_invstds = (1-averaging_factor)*mat(running_invstds) + averaging_factor*mat(invstds);
            }
            else
            {
                running_means = means;
                running_invstds = invstds;
            }
        }

        void batch_normalize_gradient (
            const tensor& gradient_input,
            const tensor& means,
            const tensor& invstds,
            const tensor& src,
            const tensor& gamma,
            tensor& src_grad,
            tensor& gamma_grad, 
            tensor& beta_grad 
        )
        {

            const long num = src.k()*src.nr()*src.nc();
            DLIB_CASSERT(src.num_samples() > 1, "");
            DLIB_CASSERT(num == means.size(),"");
            DLIB_CASSERT(num == invstds.size(),"");
            DLIB_CASSERT(num == gamma.size(),"");
            DLIB_CASSERT(num == gamma_grad.size(),"");
            DLIB_CASSERT(num == beta_grad.size(),"");
            DLIB_CASSERT(have_same_dimensions(gradient_input, src),"");
            DLIB_CASSERT(have_same_dimensions(gradient_input, src_grad),"");

            beta_grad = 0;
            gamma_grad = 0;
            auto p_grad = gradient_input.host();
            auto p_src = src.host();
            const auto p_gamma = gamma.host();   
            const auto p_gamma_grad = gamma_grad.host();   
            const auto p_beta_grad = beta_grad.host();   
            const auto p_invstds = invstds.host();
            const auto p_means = means.host();

            resizable_tensor dvars, dmeans;
            dvars.copy_size(invstds);
            dmeans.copy_size(means);
            dvars = 0;
            dmeans = 0;
            const auto p_dvars = dvars.host();
            const auto p_dmeans = dmeans.host();

            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long i = 0; i < num; ++i)
                {
                    const float x_hat = (*p_src - p_means[i])*p_invstds[i];
                    p_beta_grad[i] += *p_grad;
                    p_gamma_grad[i] += (*p_grad)*x_hat;

                    const float dx = *p_grad * p_gamma[i];

                    p_dvars[i] += dx*(*p_src - p_means[i])*-0.5*std::pow(p_invstds[i], 3.0f);

                    ++p_grad;
                    ++p_src;
                }
            }

            const float invnum = 1.0f/src.num_samples();
            p_grad = gradient_input.host();
            p_src = src.host();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long i = 0; i < num; ++i)
                {
                    const float dx = *p_grad * p_gamma[i];

                    p_dmeans[i] += dx*-p_invstds[i] + p_dvars[i] * -2*(*p_src - p_means[i])*invnum;

                    ++p_grad;
                    ++p_src;
                }
            }
            p_grad = gradient_input.host();
            p_src = src.host();
            auto p_src_grad = src_grad.host();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long i = 0; i < num; ++i)
                {
                    const float dx = *p_grad * p_gamma[i];

                    *p_src_grad += dx*p_invstds[i] + 
                        p_dvars[i] *2*(*p_src - p_means[i])*invnum + 
                        p_dmeans[i]*invnum;


                    ++p_grad;
                    ++p_src;
                    ++p_src_grad;
                }
            }
        }

    // ----------------------------------------------------------------------------------------

        void batch_normalize_conv_inference (
            resizable_tensor& dest,
            const tensor& src,
            const tensor& gamma, 
            const tensor& beta,
            const tensor& running_means,
            const tensor& running_invstds
        )
        {
            DLIB_CASSERT(
                gamma.num_samples() == 1 && 
                gamma.nr() == 1 &&
                gamma.nc() == 1 &&
                gamma.k()  == src.k() &&
                have_same_dimensions(gamma, beta) &&
                have_same_dimensions(gamma, running_means) &&
                have_same_dimensions(gamma, running_invstds), 
                "\ngamma.num_samples(): " << gamma.num_samples() << 
                "\ngamma.k():  " << gamma.k() << 
                "\ngamma.nr(): " << gamma.nr() << 
                "\ngamma.nc(): " << gamma.nc() << 
                "\nbeta.num_samples(): " << beta.num_samples() << 
                "\nbeta.k():   " << beta.k() << 
                "\nbeta.nr():  " << beta.nr() << 
                "\nbeta.nc():  " << beta.nc() << 
                "\nrunning_means.num_samples(): " << running_means.num_samples() << 
                "\nrunning_means.k():   " << running_means.k() << 
                "\nrunning_means.nr():  " << running_means.nr() << 
                "\nrunning_means.nc():  " << running_means.nc() << 
                "\nrunning_invstds.num_samples(): " << running_invstds.num_samples() << 
                "\nrunning_invstds.k():   " << running_invstds.k() << 
                "\nrunning_invstds.nr():  " << running_invstds.nr() << 
                "\nrunning_invstds.nc():  " << running_invstds.nc() << 
                "\nsrc.k():   " << src.k() << 
                "\nsrc.nr():  " << src.nr() << 
                "\nsrc.nc():  " << src.nc() 
            );
            dest.copy_size(src);

            auto d = dest.host();
            auto s = src.host();
            auto g = gamma.host();
            auto b = beta.host();
            auto m = running_means.host();
            auto i = running_invstds.host();

            const long num = src.nr()*src.nc();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    for (long j = 0; j < num; ++j)
                    {
                        *d = g[k]*(*s - m[k])*i[k] + b[k];
                        ++d;
                        ++s;
                    }
                }
            }
        }

        void batch_normalize_conv (
            resizable_tensor& dest,
            resizable_tensor& means,
            resizable_tensor& invstds,
            const double averaging_factor,
            resizable_tensor& running_means,
            resizable_tensor& running_invstds,
            const tensor& src,
            const tensor& gamma, 
            const tensor& beta 
        )
        {
            DLIB_CASSERT(0 <= averaging_factor && averaging_factor <= 1, "averaging_factor: " << averaging_factor);
            DLIB_CASSERT(averaging_factor==1 || have_same_dimensions(running_means,means),"");
            DLIB_CASSERT(averaging_factor==1 || have_same_dimensions(running_invstds,invstds),"");
            DLIB_CASSERT(
                src.num_samples() > 1 &&
                gamma.num_samples() == 1 && 
                beta.num_samples() == 1 && 
                gamma.nr() == 1 && 
                beta.nr() == 1 && 
                gamma.nc() == 1 && 
                beta.nc() == 1 && 
                gamma.k()  == beta.k()  && beta.k() == src.k(), 
                "\ngamma.num_samples(): " << gamma.num_samples() << 
                "\ngamma.k():  " << gamma.k() << 
                "\ngamma.nr(): " << gamma.nr() << 
                "\ngamma.nc(): " << gamma.nc() << 
                "\nbeta.num_samples(): " << beta.num_samples() << 
                "\nbeta.k():   " << beta.k() << 
                "\nbeta.nr():  " << beta.nr() << 
                "\nbeta.nc():  " << beta.nc() << 
                "\nsrc.k():   " << src.k() << 
                "\nsrc.nr():  " << src.nr() << 
                "\nsrc.nc():  " << src.nc() 
            );

            dest.copy_size(src);
            means.set_size(1, src.k());
            invstds.set_size(1, src.k());

            // first compute means and invstds
            means = 0;
            invstds = 0;
            const auto p_invstds = invstds.host();
            const auto p_means = means.host();
            const auto p_gamma = gamma.host();   
            const auto p_beta = beta.host();   
            auto p_src = src.host();
            const long num = src.nr()*src.nc();
            // compute means, and sum of squares
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    for (long i = 0; i < num; ++i)
                    {
                        p_means[k] += *p_src;
                        p_invstds[k] += (*p_src)*(*p_src);
                        ++p_src;
                    }
                }
            }
            means /= src.num_samples()*num;
            invstds /= src.num_samples()*num;
            // copy data back to host
            invstds.host(); means.host();

            p_src = src.host();
            // compute variances 
            for (long k = 0; k < src.k(); ++k)
            {
                float actual_var = p_invstds[k] - p_means[k]*p_means[k];
                p_invstds[k] = 1.0f/std::sqrt(actual_var + BATCH_NORM_EPS);
            }

            p_src = src.host();
            auto p_dest = dest.host();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    for (long i = 0; i < num; ++i)
                    {
                        *p_dest = (*p_src - p_means[k])*p_invstds[k];
                        *p_dest = (*p_dest)*p_gamma[k] + p_beta[k];
                        ++p_src;
                        ++p_dest;
                    }
                }
            }

            // now keep track of the running means and invstds
            running_means.copy_size(means);
            running_invstds.copy_size(invstds);
            if (averaging_factor != 1)
            {
                running_means = (1-averaging_factor)*mat(running_means) + averaging_factor*mat(means);
                running_invstds = (1-averaging_factor)*mat(running_invstds) + averaging_factor*mat(invstds);
            }
            else
            {
                running_means = means;
                running_invstds = invstds;
            }
        }

        void batch_normalize_conv_gradient(
            const tensor& gradient_input,
            const tensor& means,
            const tensor& invstds,
            const tensor& src,
            const tensor& gamma,
            tensor& src_grad,
            tensor& gamma_grad, 
            tensor& beta_grad 
        )
        {

            const long num = src.nr()*src.nc();
            DLIB_CASSERT(src.num_samples() > 1, "");
            DLIB_CASSERT(src.k() == means.size(),"");
            DLIB_CASSERT(src.k() == invstds.size(),"");
            DLIB_CASSERT(src.k() == gamma.size(),"");
            DLIB_CASSERT(src.k() == gamma_grad.size(),"");
            DLIB_CASSERT(src.k() == beta_grad.size(),"");
            DLIB_CASSERT(have_same_dimensions(gradient_input, src),"");
            DLIB_CASSERT(have_same_dimensions(gradient_input, src_grad),"");

            beta_grad = 0;
            gamma_grad = 0;

            auto p_grad = gradient_input.host();
            auto p_src = src.host();
            const auto p_gamma = gamma.host();   
            const auto p_gamma_grad = gamma_grad.host();   
            const auto p_beta_grad = beta_grad.host();   
            const auto p_invstds = invstds.host();
            const auto p_means = means.host();

            resizable_tensor dvars, dmeans;
            dvars.copy_size(invstds);
            dmeans.copy_size(means);
            dvars = 0;
            dmeans = 0;
            const auto p_dvars = dvars.host();
            const auto p_dmeans = dmeans.host();

            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    const float invstd_pow = -0.5*std::pow(p_invstds[k], 3.0f);
                    for (long i = 0; i < num; ++i)
                    {
                        const float x_hat = (*p_src - p_means[k])*p_invstds[k];
                        p_beta_grad[k] += *p_grad;
                        p_gamma_grad[k] += (*p_grad)*x_hat;

                        const float dx = *p_grad * p_gamma[k];

                        p_dvars[k] += dx*(*p_src - p_means[k])*invstd_pow;

                        ++p_grad;
                        ++p_src;
                    }
                }
            }

            p_grad = gradient_input.host();
            p_src = src.host();
            const float invnum = 1.0f/(src.num_samples()*num);
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    for (long i = 0; i < num; ++i)
                    {
                        const float dx = *p_grad * p_gamma[k];

                        p_dmeans[k] += -dx*p_invstds[k] + p_dvars[k] * -2*(*p_src - p_means[k])*invnum;

                        ++p_grad;
                        ++p_src;
                    }
                }
            }
            p_grad = gradient_input.host();
            p_src = src.host();
            auto p_src_grad = src_grad.host();
            for (long n = 0; n < src.num_samples(); ++n)
            {
                for (long k = 0; k < src.k(); ++k)
                {
                    for (long i = 0; i < num; ++i)
                    {
                        const float dx = *p_grad * p_gamma[k];

                        *p_src_grad += dx*p_invstds[k] + 
                            p_dvars[k]*2*(*p_src - p_means[k])*invnum + 
                            p_dmeans[k]*invnum;


                        ++p_grad;
                        ++p_src;
                        ++p_src_grad;
                    }
                }
            }
        }

    // -----------------------------------------------------------------------------------

        void threshold (
            tensor& data,
            float thresh
        )
        {
            const auto d = data.host();
            for (size_t i = 0; i < data.size(); ++i)
                d[i] = d[i]>thresh ? 1:0;
        }

        void dot (
            const tensor& a,
            const tensor& b,
            tensor& result,
            size_t idx
        )
        {
            DLIB_CASSERT(a.size() == b.size(), "");
            DLIB_CASSERT(idx < result.size(), "");

            const auto aa = a.host();
            const auto bb = b.host();
            auto r = result.host();
            for (size_t i = 0; i < a.size(); ++i)
                r[idx] += aa[i]*bb[i];
        }

    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------

        void softmax (
            tensor& dest,
            const tensor& src
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest,src),"");
            const auto d = dest.host();
            const auto s = src.host();

            const long num = src.nr()*src.nc();
            // Note that we subtract out the max values in each channel before applying
            // exp() to avoid numeric overflow in the subsequent computations.  Doing this
            // doesn't change the resulting output, it just makes it more numerically
            // stable.
            for (long n = 0; n < src.num_samples(); ++n)
            {
                auto ss = s + num*src.k()*n;
                auto dd = d + num*src.k()*n;
                for (long i = 0; i < num; ++i)
                {
                    float max_val = -std::numeric_limits<float>::infinity();
                    for (long k = 0; k < src.k(); ++k)
                        max_val = std::max(max_val, ss[k*num]);

                    for (long k = 0; k < src.k(); ++k)
                        dd[k*num] = std::exp(ss[k*num]-max_val);

                    ++ss;
                    ++dd;
                }
            }

            // Now normalize each channel so they sum to 1.
            for (long n = 0; n < src.num_samples(); ++n)
            {
                const auto ss = s + num*src.k()*n;
                const auto dd = d + num*src.k()*n;
                for (long r = 0; r < src.nr(); ++r)
                {
                    for (long c = 0; c < src.nc(); ++c)
                    {
                        const auto sss = ss+r*src.nc()+c;
                        const auto ddd = dd+r*src.nc()+c;

                        float temp = 0;
                        for (long k = 0; k < src.k(); ++k)
                            temp += ddd[k*num];
                        for (long k = 0; k < src.k(); ++k)
                            ddd[k*num] /= temp;
                    }
                }
            }
        }

        void softmax_gradient (
            tensor& grad,
            const tensor& dest,
            const tensor& gradient_input
        )
        {
            DLIB_CASSERT(have_same_dimensions(grad,dest),"");
            DLIB_CASSERT(have_same_dimensions(grad,gradient_input),"");
            const auto d = dest.host();
            const auto g = grad.host();
            const auto in = gradient_input.host();

            const long num = grad.nr()*grad.nc();

            for (long n = 0; n < grad.num_samples(); ++n)
            {
                const auto d2 = d + num*grad.k()*n;
                const auto g2 = g + num*grad.k()*n;
                const auto in2 = in + num*grad.k()*n;
                for (long r = 0; r < grad.nr(); ++r)
                {
                    for (long c = 0; c < grad.nc(); ++c)
                    {
                        const auto d3 = d2+r*grad.nc()+c;
                        const auto g3 = g2+r*grad.nc()+c;
                        const auto in3 = in2+r*grad.nc()+c;

                        float temp = 0;
                        for (long k = 0; k < grad.k(); ++k)
                            temp += -d3[k*num]*in3[k*num];
                        for (long k = 0; k < grad.k(); ++k)
                            g3[k*num] = d3[k*num]*(temp+in3[k*num]);
                    }
                }
            }
        }

    // ------------------------------------------------------------------------------------

        void sigmoid (
            tensor& dest,
            const tensor& src
        )
        {
            const auto d = dest.host();
            const auto s = src.host();
            for (size_t i = 0; i < src.size(); ++i)
                d[i] = 1/(1+std::exp(-s[i]));
        }

        void sigmoid_gradient (
            tensor& grad,
            const tensor& dest,
            const tensor& gradient_input
        )
        {
            const auto g = grad.host();
            const auto d = dest.host();
            const auto in = gradient_input.host();
            for (size_t i = 0; i < dest.size(); ++i)
                g[i] = in[i]*d[i]*(1-d[i]);
        }

    // ------------------------------------------------------------------------------------

        void relu (
            tensor& dest,
            const tensor& src
        )
        {
            dest = lowerbound(mat(src), 0);
        }

        void relu_gradient (
            tensor& grad,
            const tensor& dest,
            const tensor& gradient_input
        )
        {
            const float* gi = gradient_input.host();
            const float* in = dest.host();
            float* out = grad.host();
            for (size_t i = 0; i < dest.size(); ++i)
            {
                if (in[i] > 0)
                    out[i] = gi[i];
                else
                    out[i] = 0;
            }
        }

    // ------------------------------------------------------------------------------------

        void tanh (
            tensor& dest,
            const tensor& src
        )
        {
            const auto d = dest.host();
            const auto s = src.host();
            for (size_t i = 0; i < src.size(); ++i)
                d[i] = std::tanh(s[i]);
        }

        void tanh_gradient (
            tensor& grad,
            const tensor& dest,
            const tensor& gradient_input
        )
        {
            const auto g = grad.host();
            const auto d = dest.host();
            const auto in = gradient_input.host();
            for (size_t i = 0; i < dest.size(); ++i)
                g[i] = in[i]*(1-d[i]*d[i]);
        }

    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------

        pooling::pooling (
        ) : window_height(0),window_width(0),stride_y(0),stride_x(0),do_max_pooling(true)
        {
        }

        void pooling::
        clear(
        )
        {
            window_height = 0;
            window_width = 0;
            stride_y = 0;
            stride_x = 0;
        }

        void pooling::
        setup_max_pooling(
            int window_height_,
            int window_width_,
            int stride_y_,
            int stride_x_
        )
        {
            DLIB_CASSERT(window_width_ > 0,"");
            DLIB_CASSERT(window_height_ > 0,"");
            DLIB_CASSERT(stride_y_ > 0,"");
            DLIB_CASSERT(stride_x_ > 0,"");

            window_height = window_height_;
            window_width = window_width_;
            stride_y = stride_y_;
            stride_x = stride_x_;
            do_max_pooling = true;
        }

        void pooling::
        setup_avg_pooling(
            int window_height_,
            int window_width_,
            int stride_y_,
            int stride_x_
        )
        {
            DLIB_CASSERT(window_width_ > 0,"");
            DLIB_CASSERT(window_height_ > 0,"");
            DLIB_CASSERT(stride_y_ > 0,"");
            DLIB_CASSERT(stride_x_ > 0,"");

            window_height = window_height_;
            window_width = window_width_;
            stride_y = stride_y_;
            stride_x = stride_x_;
            do_max_pooling = false;
        }

        void pooling::
        operator() (
            resizable_tensor& dest,
            const tensor& src
        )
        {
            DLIB_CASSERT(window_width > 0,"");
            DLIB_CASSERT(window_height > 0,"");
            DLIB_CASSERT(stride_y > 0,"");
            DLIB_CASSERT(stride_x > 0,"");

            dest.set_size(
                 src.num_samples(),
                 src.k(),
                 1+(src.nr()-window_height%2)/stride_y,
                 1+(src.nc()-window_width%2)/stride_x
                );

            if (src.size() == 0)
            {
                dest = 0;
                return;
            }


            auto d = dest.host();
            auto s = src.host();
            if (does_max_pooling())
            {
                for (long n = 0; n < dest.num_samples(); ++n)
                {
                    for (long k = 0; k < dest.k(); ++k)
                    {
                        auto simg = image_plane(src,n,k);
                        auto dimg = d + (n*dest.k() + k)*dest.nr()*dest.nc();

                        for (long r = 0; r < dest.nr(); ++r)
                        {
                            for (long c = 0; c < dest.nc(); ++c)
                            {
                                auto win = centered_rect(c*stride_x,
                                    r*stride_y,
                                    window_width,
                                    window_height);
                                dimg[r*dest.nc() + c] = max(subm_clipped(simg,win));
                            }
                        }
                    }
                }
            }
            else
            {
                for (long n = 0; n < dest.num_samples(); ++n)
                {
                    for (long k = 0; k < dest.k(); ++k)
                    {
                        auto simg = image_plane(src,n,k);
                        auto dimg = d + (n*dest.k() + k)*dest.nr()*dest.nc();

                        for (long r = 0; r < dest.nr(); ++r)
                        {
                            for (long c = 0; c < dest.nc(); ++c)
                            {
                                auto win = centered_rect(c*stride_x,
                                    r*stride_y,
                                    window_width,
                                    window_height);
                                dimg[r*dest.nc() + c] = mean(subm_clipped(simg,win));
                            }
                        }
                    }
                }
            }

        }

        void pooling::get_gradient(
            const tensor& gradient_input, 
            const tensor& dest,
            const tensor& src,
            tensor& grad 
        )
        {
            DLIB_CASSERT(have_same_dimensions(gradient_input,dest),"");
            DLIB_CASSERT(have_same_dimensions(src,grad),"");


            if (src.size() == 0)
            {
                return;
            }


            auto gi = gradient_input.host();
            auto g = grad.host();
            auto s = src.host();
            if (does_max_pooling())
            {
                for (long n = 0; n < dest.num_samples(); ++n)
                {
                    for (long k = 0; k < dest.k(); ++k)
                    {
                        auto simg = image_plane(src,n,k);
                        auto gimg = g + (n*grad.k() + k)*grad.nr()*grad.nc();
                        auto giimg = gi + (n*dest.k() + k)*dest.nr()*dest.nc();
                        auto imgbox = get_rect(simg);

                        for (long r = 0; r < dest.nr(); ++r)
                        {
                            for (long c = 0; c < dest.nc(); ++c)
                            {
                                auto win = centered_rect(c*stride_x,
                                    r*stride_y,
                                    window_width,
                                    window_height).intersect(imgbox);
                                auto p = max_point(subm(simg,win))+win.tl_corner();
                                gimg[p.y()*grad.nc()+p.x()] += giimg[r*dest.nc()+c];
                            }
                        }
                    }
                }
            }
            else
            {
                for (long n = 0; n < dest.num_samples(); ++n)
                {
                    for (long k = 0; k < dest.k(); ++k)
                    {
                        auto simg = image_plane(src,n,k);
                        auto gimg = g + (n*grad.k() + k)*grad.nr()*grad.nc();
                        auto giimg = gi + (n*dest.k() + k)*dest.nr()*dest.nc();
                        auto imgbox = get_rect(simg);

                        for (long r = 0; r < dest.nr(); ++r)
                        {
                            for (long c = 0; c < dest.nc(); ++c)
                            {
                                auto win = centered_rect(c*stride_x,
                                    r*stride_y,
                                    window_width,
                                    window_height).intersect(imgbox);
                                const float delta = giimg[r*dest.nc()+c]/win.area();
                                for (long y = win.top(); y <= win.bottom(); ++y)
                                {
                                    for (long x = win.left(); x <= win.right(); ++x)
                                    {
                                        gimg[y*grad.nc()+x] += delta;
                                    }
                                }
                            }
                        }
                    }
                }
            }

        }

    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------

        void img2col(
            matrix<float>& output,
            const tensor& data,
            long n,
            long filter_nr,
            long filter_nc,
            long stride_y,
            long stride_x
        )
        {
            const auto d = data.host() + data.k()*data.nr()*data.nc()*n;
            const rectangle boundary = get_rect(data);

            const long out_nr = 1+(data.nr()-filter_nr%2)/stride_y;
            const long out_nc = 1+(data.nc()-filter_nc%2)/stride_x;

            output.set_size(out_nr*out_nc, 
                            data.k()*filter_nr*filter_nc);
            DLIB_CASSERT(output.size() != 0,"");
            float* t = &output(0,0);

            // now fill in the Toeplitz output matrix for the n-th sample in data.  
            size_t cnt = 0;
            for (long r = -(1-filter_nr%2); r < data.nr(); r+=stride_y)
            {
                for (long c = -(1-filter_nc%2); c < data.nc(); c+=stride_x)
                {
                    for (long k = 0; k < data.k(); ++k)
                    {
                        for (long y = 0; y < filter_nr; ++y)
                        {
                            for (long x = 0; x < filter_nc; ++x)
                            {
                                DLIB_CASSERT(cnt < output.size(),"");
                                long xx = c-x+filter_nc/2;
                                long yy = r-y+filter_nr/2;
                                if (boundary.contains(xx,yy))
                                    *t = d[(k*data.nr() + yy)*data.nc() + xx];
                                else
                                    *t = 0;
                                ++t;
                                ++cnt;
                            }
                        }
                    }
                }
            }
        }

        void col2img(
            const matrix<float>& output,
            tensor& data,
            long n,
            long filter_nr,
            long filter_nc,
            long stride_y,
            long stride_x
        )
        {
            const auto d = data.host() + data.k()*data.nr()*data.nc()*n;
            const rectangle boundary = get_rect(data);

            DLIB_CASSERT(output.size() != 0,"");
            const float* t = &output(0,0);

            // now fill in the Toeplitz output matrix for the n-th sample in data.  
            for (long r = -(1-filter_nr%2); r < data.nr(); r+=stride_y)
            {
                for (long c = -(1-filter_nc%2); c < data.nc(); c+=stride_x)
                {
                    for (long k = 0; k < data.k(); ++k)
                    {
                        for (long y = 0; y < filter_nr; ++y)
                        {
                            for (long x = 0; x < filter_nc; ++x)
                            {
                                long xx = c-x+filter_nc/2;
                                long yy = r-y+filter_nr/2;
                                if (boundary.contains(xx,yy))
                                    d[(k*data.nr() + yy)*data.nc() + xx] += *t;
                                ++t;
                            }
                        }
                    }
                }
            }
        }

        void tensor_conv::operator() (
            resizable_tensor& output,
            const tensor& data,
            const tensor& filters,
            int stride_y,
            int stride_x
        )
        {
            DLIB_CASSERT(is_same_object(output,data) == false,"");
            DLIB_CASSERT(is_same_object(output,filters) == false,"");
            DLIB_CASSERT(filters.k() == data.k(),"");
            DLIB_CASSERT(stride_y > 0 && stride_x > 0,"");

            output.set_size(data.num_samples(),
                            filters.num_samples(),
                            1+(data.nr()-filters.nr()%2)/stride_y,
                            1+(data.nc()-filters.nc()%2)/stride_x);

            matrix<float> temp;
            for (long n = 0; n < data.num_samples(); ++n)
            {
                img2col(temp, data, n, filters.nr(), filters.nc(), stride_y, stride_x);
                output.set_sample(n, mat(filters)*trans(temp));
            }

            last_stride_y = stride_y;
            last_stride_x = stride_x;
        }

    // ------------------------------------------------------------------------------------

        void tensor_conv::
        get_gradient_for_data (
            const tensor& gradient_input, 
            const tensor& filters,
            tensor& data_gradient
        )
        {
            matrix<float> temp;
            for (long n = 0; n < gradient_input.num_samples(); ++n)
            {
                auto gi = mat(gradient_input.host()+gradient_input.k()*gradient_input.nr()*gradient_input.nc()*n,
                              gradient_input.k(),
                              gradient_input.nr()*gradient_input.nc());
                                    

                temp = trans(gi)*mat(filters);
                col2img(temp, data_gradient, n, filters.nr(), filters.nc(), last_stride_y, last_stride_x);
            }
        }

    // ------------------------------------------------------------------------------------

        void tensor_conv::
        get_gradient_for_filters (
            const tensor& gradient_input, 
            const tensor& data,
            tensor& filters_gradient
        )
        {
            matrix<float> temp;
            for (long n = 0; n < gradient_input.num_samples(); ++n)
            {
                auto gi = mat(gradient_input.host()+gradient_input.k()*gradient_input.nr()*gradient_input.nc()*n,
                              gradient_input.k(),
                              gradient_input.nr()*gradient_input.nc());


                img2col(temp, data, n, filters_gradient.nr(), filters_gradient.nc(), last_stride_y, last_stride_x);
                if (n == 0)
                    filters_gradient = gi*temp;
                else
                    filters_gradient += gi*temp;
            }
        }

    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------------

    } 
}


#endif // DLIB_DNN_CPU_cPP_

