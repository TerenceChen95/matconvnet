// @file im2row_cpu.cpp
// @brief Stack image patches as matrix rows (CPU)
// @author Andrea Vedaldi

/*
Copyright (C) 2014-16 Andrea Vedaldi.
All rights reserved.

This file is part of the VLFeat library and is made available under
the terms of the BSD license (see the COPYING file).
*/

#include "im2row.hpp"
#include <string.h>

using namespace vl ;
using namespace vl::impl ;

/* ---------------------------------------------------------------- */
/*                                                  Heper functions */
/* ---------------------------------------------------------------- */

template<typename T>
static inline T floor_divide(T a, T b) {
  if (a >= 0) return a/b;
  else return (a - b + 1)/b;
}

template<typename T>
static inline T ceil_divide(T a, T b) {
  if (a >= 0) return (a + b - 1)/b ;
  else return a/b ;
}

template<typename T>
static inline T static_max(T a, T b) {
  return (a>=b) ? a:b ;
}

template<typename T>
static inline T static_min(T a, T b) {
  return (a<=b) ? a:b ;
}

namespace vl { namespace impl {


  template<typename type>
  struct im2row<vl::VLDT_CPU, type>
  {

    /* ------------------------------------------------------------ */
    /*                                                      forward */
    /* ------------------------------------------------------------ */

    // Todo: make all Int.
    static vl::ErrorCode
    forward(Context & context,
            type* stacked,
            type const* data,
            Int width,
            Int height,
            Int depth,
            Int windowWidth,
            Int windowHeight,
            Int strideX,
            Int strideY,
            Int padLeft,
            Int padRight,
            Int padTop,
            Int padBottom,
            Int dilateX,
            Int dilateY)
    {
      Int windowExtentX = (windowWidth - 1)*dilateX + 1 ;
      Int windowExtentY = (windowHeight - 1)*dilateY + 1 ;
      Int numPatchesX = (width + padLeft + padRight - windowExtentX)/strideX + 1 ;
      Int numPatchesY = (height + padTop + padBottom - windowExtentY)/strideY + 1 ;
      Int numRows = windowWidth * windowHeight * depth ;

      /*
       Fill a row of the patch matrix. Since patches are stored
       along the columns of the matrix, scanning a row menas visiting all
       the patches. Different rows corresponds to a different
       offset within each patch.

       In this manner, as we fill a row
       we tend to access spatially adiacent elements
       in the input image, particulary for small strides.
       */
      for (Int row = 0; row < numRows ; ++row) {
        /*
         Get the patch offset corresponding to this row of the stacked
         image.
         */
        Int u = row ;
        Int v = u / windowWidth ;
        Int z = v / windowHeight ;
        u %= windowWidth ;
        v %= windowHeight ;

        /*
         Filling this row requires visiting the pixels in the input tensor
         `data` that appear at the given offset (u,v) in the output patches.
         For the patch at (x,y), the pixel coordinates (x_data,y_data) in the
         `data` tensor are:

         x_data(x) = x * strideX + u * dilateX - padLeft,  0 <= x < numPatchesX,
         y_data(y) = y * strideY + v * dilateY - padTop,   0 <= y < numPatchesY,
         z_data(z) = z.

         Now we visit all patches (x,y) in lexicographical order to fill
         successive output pixels. Patches around the boundary may peek outside
         the `data` tensor, which is padded with zero. We calcualte these
         borders here and fill them with zeros in the output.
         
         In particular, patch x peeks within the input tensor `data`
         if x is in the range [x0,x1] given by:

         x_data(x) >= 0
         <=> x >= (padLeft - u * dilateX) / stride
         <=> x >= ceil((padLeft - u * dilateX) / stride) = x0
         
         x_data(x) <= width-1
         <=> x <= (width-1 + padLeft - u * dilateX) / stride
         <=> x <= floor((width-1 + padLeft - u * dilateX) / stride)
         <=> x <  floor((width-1 + padLeft - u * dilateX) / stride) + 1 = x1

         and the same for y. Note that, while usually x0 <= x1, there are
         special cases for which x1 < x0. This is accounted for in the loops
         below.
         */

        Int x0 = static_min(numPatchesX, ceil_divide(padLeft - u * dilateX, strideX)) ;
        Int y0 = static_min(numPatchesY, ceil_divide(padTop - v * dilateY, strideY)) ;
        Int x1 = static_min(numPatchesX, floor_divide(width + padLeft - u * dilateX - 1, strideX) + 1) ;
        Int y1 = static_min(numPatchesY, floor_divide(height + padTop - v * dilateY - 1, strideY) + 1) ;
        Int x ;
        Int y ;

        for (y = 0 ; y < y0 ; ++y) {
          for (x = 0 ; x < numPatchesX ; ++x) {
            *stacked++ = 0 ;
          }
        }
        for ( ; y < y1 ; ++y) {
          for (x = 0 ; x < x0 ; ++x) {
            *stacked++ = 0 ;
          }
          auto y_data = y * strideY + v * dilateY - padTop ;
          auto x_data = x * strideX + u * dilateX - padLeft ;
          type const * b = data + (z * height + y_data) * width + x_data ;
          for ( ; x < x1 ; ++x) {
            *stacked++ = *b ;
            b += strideX ;
          }
          for ( ; x < numPatchesX ; ++x) {
            *stacked++ = 0 ;
          }
        }
        for ( ; y < numPatchesY ; ++y) {
          for (x = 0 ; x < numPatchesX ; ++x) {
            *stacked++ = 0 ;
          }
        }
      }
      return vl::VLE_Success ;
    }

    /* ------------------------------------------------------------ */
    /*                                                     backward */
    /* ------------------------------------------------------------ */

    static vl::ErrorCode
    backward(Context & context,
             type* data,
             type const* stacked,
             Int width,
             Int height,
             Int depth,
             Int windowWidth,
             Int windowHeight,
             Int strideX,
             Int strideY,
             Int padLeft,
             Int padRight,
             Int padTop,
             Int padBottom,
             Int dilateX,
             Int dilateY)
    {
      Int windowExtentX = (windowWidth - 1)*dilateX + 1 ;
      Int windowExtentY = (windowHeight - 1)*dilateY + 1 ;
      Int numPatchesX = (width + padLeft + padRight - windowExtentX)/strideX + 1 ;
      Int numPatchesY = (height + padTop + padBottom - windowExtentY)/strideY + 1 ;
      Int numRows = windowWidth * windowHeight * depth ;

      memset(data, 0, sizeof(type) * size_t(width * height * depth)) ;

      /*
       Do the converse of im2col, still scanning rows of the stacked image.
       See comments of im2col for an explanation of the algorithm.
       */
      for (Int row = 0; row < numRows ; ++row) {
        Int u = row ;
        Int v = u / windowWidth ;
        Int z = v / windowHeight ;
        u %= windowWidth ;
        v %= windowHeight ;

        Int x0 = static_min(numPatchesX, ceil_divide(padLeft - u * dilateX, strideX)) ;
        Int y0 = static_min(numPatchesY, ceil_divide(padTop - v * dilateY, strideY)) ;
        Int x1 = static_min(numPatchesX, floor_divide(width + padLeft - u * dilateX - 1, strideX) + 1) ;
        Int y1 = static_min(numPatchesY, floor_divide(height + padTop - v * dilateY - 1, strideY) + 1) ;

        Int y = static_max(0L, y0) ;
        stacked += numPatchesX * static_max(0L, y) ;
        for ( ; y < y1 ; ++y) {
          Int x = static_max(0L, x0) ;
          Int y_data = y * strideY + v * dilateY - padTop ;
          Int x_data = x * strideX + u * dilateX - padLeft ;
          type * b = data + (z * height + y_data) * width + x_data ;
          stacked += x ;
          for ( ; x < x1 ; ++x) {
            *b += *stacked++ ;
            b += strideX ;
          }
          stacked += numPatchesX - x ;
        }
        stacked += numPatchesX * (numPatchesY - y) ;
      }
      return vl::VLE_Success ;
    }
  } ;

} }

// Instantiations
template struct vl::impl::im2row<vl::VLDT_CPU, float> ;

#ifdef ENABLE_DOUBLE
template struct vl::impl::im2row<vl::VLDT_CPU, double> ;
#endif
