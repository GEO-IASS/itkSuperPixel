/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef itkSLICImageFilter_h
#define itkSLICImageFilter_h

#include "itkImageToImageFilter.h"
#include "itkVariableLengthVector.h"

#include "itkShrinkImageFilter.h"

#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkImageScanlineIterator.h"

#include "itkMath.h"

namespace itk
{

template< typename TInputImage, typename TOutputImage >
class SLICImageFilter:
    public ImageToImageFilter< TInputImage, TOutputImage >
{
public:
  /** Standard class typedefs. */
  typedef SLICImageFilter                                 Self;
  typedef ImageToImageFilter< TInputImage, TOutputImage > Superclass;
  typedef SmartPointer< Self >                            Pointer;
  typedef SmartPointer< const Self >                      ConstPointer;

  /** Method for creation through the object factory. */
  itkNewMacro(Self);

  /** Run-time type information (and related methods). */
  itkTypeMacro(ExtractImageFilter, ImageToImageFilter);

  /** Image type information. */
  typedef TInputImage  InputImageType;
  typedef typename InputImageType::PixelType InputPixelType;
  typedef TOutputImage OutputImageType;
  itkStaticConstMacro(ImageDimension, unsigned int,
                      TInputImage::ImageDimension);
  typedef double DistanceImagePixelType;
  typedef Image<DistanceImagePixelType, ImageDimension> DistanceImageType;

  typedef typename InputImageType::IndexType IndexType;
  typedef typename InputImageType::PointType PointType;
  // assume variable length vector right now
  typedef VariableLengthVector<double> ClusterType;


  typedef FixedArray< unsigned int, ImageDimension > SuperGridSizeType;

  itkSetMacro( SpatialProximityWeight, double );
  itkGetConstMacro( SpatialProximityWeight, double );

  itkSetMacro( MaximumNumberOfIterations, unsigned int );
  itkGetConstMacro( MaximumNumberOfIterations, unsigned int );

  itkSetMacro(SuperGridSize, SuperGridSizeType);
  void SetSuperGridSize(unsigned int factor)
    {
      unsigned int i;
      for (i = 0; i < ImageDimension; ++i)
        {
        if (factor != m_SuperGridSize[i])
          {
          break;
          }
        }
      if ( i < ImageDimension )
        {
        this->Modified();
        m_SuperGridSize.Fill(factor);
        }
    }
  void SetSuperGridSize(unsigned int i, unsigned int factor)
    {
      if (m_SuperGridSize[i] == factor)
        {
        return;
        }

      this->Modified();
      m_SuperGridSize[i] = factor;
    }


protected:
  SLICImageFilter()
    {
      m_SuperGridSize.Fill(50);
      m_MaximumNumberOfIterations = (ImageDimension > 2) ? 5 : 10;
      m_SpatialProximityWeight = 10.0;
    }
  ~SLICImageFilter() {}

  void PrintSelf(std::ostream & os, Indent indent) const ITK_OVERRIDE
    {
      Superclass::PrintSelf(os, indent);
    }


  /** Generate full output and require full input */
  void EnlargeOutputRequestedRegion(DataObject *output)
    {
      Superclass::EnlargeOutputRequestedRegion(output);
      output->SetRequestedRegionToLargestPossibleRegion();
    }



  void GenerateData() ITK_OVERRIDE
    {
      itkDebugMacro("Starting GenerateData");
      this->AllocateOutputs();

      const InputImageType *inputImage = this->GetInput();

      OutputImageType *outputImage = this->GetOutput();


      typedef typename InputImageType::PixelType InputPixelType;


      itkDebugMacro("Shinking Starting")
      typename InputImageType::Pointer shrunkImage;
      {
      // todo disconnect input from pipeline
      typedef itk::ShrinkImageFilter<InputImageType, InputImageType> ShrinkImageFilterType;
      typename ShrinkImageFilterType::Pointer shrinker = ShrinkImageFilterType::New();
      shrinker->SetInput(inputImage);
      shrinker->SetShrinkFactors(m_SuperGridSize);
      shrinker->UpdateLargestPossibleRegion();

      shrunkImage = shrinker->GetOutput();
      }
      itkDebugMacro("Shinking Completed")

      const typename InputImageType::RegionType region = inputImage->GetBufferedRegion();
      const unsigned int numberOfComponents = inputImage->GetNumberOfComponentsPerPixel();
      const unsigned int numberOfClusterComponents = numberOfComponents+ImageDimension;

      const size_t numberOfClusters = shrunkImage->GetBufferedRegion().GetNumberOfPixels();

      std::vector<ClusterType> clusters;
      clusters.reserve(numberOfClusters);

      typedef ImageScanlineConstIterator< InputImageType > InputConstIteratorType;
      typedef ImageScanlineIterator< DistanceImageType >   DistanceIteratorType;
      typedef ImageScanlineIterator< OutputImageType >     OutputIteratorType;

      InputConstIteratorType it(shrunkImage, shrunkImage->GetLargestPossibleRegion());

      // Initialize cluster centers
      while(!it.IsAtEnd())
          {
          const size_t         ln =  shrunkImage->GetLargestPossibleRegion().GetSize(0);
          for (unsigned x = 0; x < ln; ++x)
            {
            ClusterType cluster( numberOfClusterComponents );
            for(unsigned int i = 0; i < numberOfComponents; ++i)
              {
              cluster[i] = it.Get()[i];
              }
            const IndexType & idx = it.GetIndex();
            typename InputImageType::PointType pt;
            shrunkImage->TransformIndexToPhysicalPoint(idx, pt);
            for(unsigned int i = 0; i < ImageDimension; ++i)
              {
              cluster[numberOfComponents+i] = pt[i];
              }
            clusters.push_back(cluster);
            ++it;
            }
          it.NextLine();
          }
      itkDebugMacro("Initial Clustering Completed");

      shrunkImage = ITK_NULLPTR;

      // TODO: Move cluster center to lowest gradient position in a 3x
      // neighborhood
      typename DistanceImageType::Pointer distanceImage = DistanceImageType::New();
      distanceImage->CopyInformation(outputImage);
      distanceImage->SetBufferedRegion( region );
      distanceImage->Allocate();


      typename InputImageType::SizeType searchRadius;
      for (unsigned int i = 0; i < ImageDimension; ++i)
        {
        searchRadius[i] = m_SuperGridSize[i];
        m_DistanceScales[i] = m_SpatialProximityWeight/m_SuperGridSize[i];
        }



      // deep copy to ensure memory is allocated
      std::vector<ClusterType> oldClusters(clusters.begin(), clusters.end());

      itkDebugMacro("Entering Main Loop");
      for(unsigned int loopCnt = 0;  loopCnt<m_MaximumNumberOfIterations; ++loopCnt)
        {
        itkDebugMacro("Iteration :" << loopCnt);
        distanceImage->FillBuffer(NumericTraits<DistanceImagePixelType>::max());


        for (size_t i = 0; i < clusters.size(); ++i)
          {
          const ClusterType &cluster = clusters[i];
          typename InputImageType::RegionType localRegion;
          typename InputImageType::PointType pt;
          IndexType idx;

          for (unsigned int d = 0; d < ImageDimension; ++d)
            {
            pt[d] = cluster[numberOfComponents+d];
            }
          //std::cout << "Cluster " << i << "@" << pt <<": " << cluster << std::endl;
          inputImage->TransformPhysicalPointToIndex(pt, idx);

          localRegion.SetIndex(idx);
          localRegion.GetModifiableSize().Fill(1u);
          localRegion.PadByRadius(searchRadius);
          if (!localRegion.Crop(region))
            {
            continue;
            }


          const size_t         ln =  localRegion.GetSize(0);

          InputConstIteratorType inputIter(inputImage, localRegion);
          DistanceIteratorType     distanceIter(distanceImage, localRegion);


          while ( !inputIter.IsAtEnd() )
            {
            for( size_t x = 0; x < ln; ++x )
              {
              const IndexType &idx = inputIter.GetIndex();

              inputImage->TransformIndexToPhysicalPoint(idx, pt);
              const double distance = this->Distance(clusters[i],
                                                     inputIter.Get(),
                                                     pt);
              if (distance < distanceIter.Get() )
                {
                distanceIter.Set(distance);
                outputImage->SetPixel(idx, i);
                }

              ++distanceIter;
              ++inputIter;
              }
            inputIter.NextLine();
            distanceIter.NextLine();
            }

          // for neighborhood iterator size S
          }

        // clear
        swap(clusters, oldClusters);
        for (size_t i = 0; i < clusters.size(); ++i)
          {
          ClusterType &cluster = clusters[i];
          cluster.Fill(0.0);
          }

        std::vector<size_t> clusterCount(clusters.size(), 0);

        itkDebugMacro("Estimating Centers");
        // calculate new centers
        OutputIteratorType itOut = OutputIteratorType(outputImage, region);
        InputConstIteratorType itIn = InputConstIteratorType(inputImage, region);
        while(!itOut.IsAtEnd())
          {
          const size_t         ln =  region.GetSize(0);
          for (unsigned x = 0; x < ln; ++x)
            {
            const IndexType &idx = itOut.GetIndex();
            const InputPixelType &v = itIn.Get();
            const typename OutputImageType::PixelType l = itOut.Get();

            ClusterType &cluster = clusters[l];
            ++clusterCount[l];

            for(unsigned int i = 0; i < numberOfComponents; ++i)
              {
              cluster[i] += v[i];
              }

            typename InputImageType::PointType pt;
            inputImage->TransformIndexToPhysicalPoint(idx, pt);
            for(unsigned int i = 0; i < ImageDimension; ++i)
              {
              cluster[numberOfComponents+i] += pt[i];
              }

            ++itIn;
            ++itOut;
            }
          itIn.NextLine();
          itOut.NextLine();
          }

        // average, l1
        double l1Residual = 0.0;
        for (size_t i = 0; i < clusters.size(); ++i)
          {
          ClusterType &cluster = clusters[i];
          cluster /= clusterCount[i];

          const ClusterType &oldCluster = oldClusters[i];
          for(unsigned j = 0; j < numberOfClusterComponents; ++j)
            {
            l1Residual += std::abs(cluster[j]-oldCluster[j]);
            }
          }
        std::cout << "L1 residual: " << l1Residual << std::endl;
        // while error <= threshold
        }
    }

  double Distance(const ClusterType &cluster, const InputPixelType &v, const PointType &pt)
    {
      const unsigned int s = cluster.GetSize();
      double d1 = 0.0;
      double d2 = 0.0;
      unsigned int i = 0;
      for (; i<s-ImageDimension; ++i)
        {
        const double d = (cluster[i] - v[i]);
        d1 += d*d;
        }
      //d1 = std::sqrt(d1);

      for (unsigned int j = 0; j < ImageDimension; ++j)
        {
        const double d = (cluster[i] - pt[j]) * m_DistanceScales[j];
        d2 += d*d;
        ++i;
        }
      //d2 = std::sqrt(d2);
      return d1+d2;
    }

private:
  SLICImageFilter(const Self &);    //purposely not implemented
  void operator=(const Self &);     //purposely not implemented

  SuperGridSizeType m_SuperGridSize;
  unsigned int m_MaximumNumberOfIterations;
  FixedArray<double,ImageDimension> m_DistanceScales;
  double m_SpatialProximityWeight;
};
} // end namespace itk

#endif //itkSLICImageFilter_h
