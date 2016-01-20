//
//  main.cpp
//  ClusteringSegmentation
//
//  Created by Mo DeJong on 12/30/15.
//  Copyright © 2015 helpurock. All rights reserved.
//

// clusteringsegmentation IMAGE TAGS_IMAGE
//
// This logic reads input pixels from an image and segments the image into different connected
// areas based on growing area of alike pixels. A set of pixels is determined to be alike
// if the pixels are near to each other in terms of 3D space via a fast clustering method.
// The TAGS_IMAGE output file is written with alike pixels being defined as having the same
// tag color.

#include <opencv2/opencv.hpp>

#include "ClusteringSegmentation.hpp"

#include "Superpixel.h"
#include "SuperpixelEdge.h"
#include "SuperpixelImage.h"

#include "OpenCVUtil.h"
#include "Util.h"

#include "quant_util.h"
#include "DivQuantHeader.h"

#include "MergeSuperpixelImage.h"

#include "srm.h"

#include "peakdetect.h"

#include "Util.h"

using namespace cv;
using namespace std;

bool clusteringCombine(Mat &inputImg, Mat &resultImg);

int main(int argc, const char** argv) {
  const char *inputImgFilename = NULL;
  const char *outputTagsImgFilename = NULL;

  if (argc == 2) {
    inputImgFilename = argv[1];
    // Default to "outtags.png"
    outputTagsImgFilename = "outtags.png";
    
    // In the special case where the inputImgFilename is fully qualified, cd to the directory
    // indicated by the path. This is useful so that just a fully qualified image path can
    // be passed as the first argument without having to explicitly set the process working dir
    // since Xcode tends to get that detail wrong when invoking profiling tools.
    
    bool containsSlash = false;
    int lastSlashOffset = -1;
    
    for ( char *ptr = (char*)inputImgFilename; *ptr != '\0' ; ptr++ ) {
      if (*ptr == '/') {
        containsSlash = true;
        lastSlashOffset = int(ptr - (char*)inputImgFilename);
      }
    }
    
    if (containsSlash) {
      char *dirname = strdup((char*)inputImgFilename);
      assert(lastSlashOffset >= 0);
      dirname[lastSlashOffset] = '\0';
      
      inputImgFilename = inputImgFilename + lastSlashOffset + 1;
      
      cout << "cd \"" << dirname << "\"" << endl;
      chdir(dirname);
      
      free(dirname);
    }
  } else if (argc != 3) {
    cerr << "usage : " << argv[0] << " IMAGE ?TAGS_IMAGE?" << endl;
    exit(1);
  } else if (argc == 3) {
    inputImgFilename = argv[1];
    outputTagsImgFilename = argv[2];
  }

  cout << "read \"" << inputImgFilename << "\"" << endl;
  
  Mat inputImg = imread(inputImgFilename, CV_LOAD_IMAGE_COLOR);
  if( inputImg.empty() ) {
    cerr << "could not read \"" << inputImgFilename << "\" as image data" << endl;
    exit(1);
  }
  
  assert(inputImg.channels() == 3);
  
  Mat resultImg;
  
  bool worked = clusteringCombine(inputImg, resultImg);
  if (!worked) {
    cerr << "seeds combine failed " << endl;
    exit(1);
  }
  
  imwrite(outputTagsImgFilename, resultImg);
  
  cout << "wrote " << outputTagsImgFilename << endl;
  
  exit(0);
}

// Main method that implements the cluster combine logic

bool clusteringCombine(Mat &inputImg, Mat &resultImg)
{
  const bool debug = true;
  const bool debugWriteIntermediateFiles = true;
  
  // Alloc object on stack
  SuperpixelImage spImage;
  //
  // Ref to object allocated on heap
//  Ptr<SuperpixelImage> spImagePtr = new SuperpixelImage();
//  SuperpixelImage &spImage = *spImagePtr;
  
  // Generate a "tags" input that contains 1 tag for each 4x4 block of input, so that
  // large regions of the exact same fill color can be detected and processed early.
  
  Mat tagsImg = inputImg.clone();
  tagsImg = (Scalar) 0;
  
  const bool debugOutput = false;
  
  const int superpixelDim = 4;
  int blockWidth = inputImg.cols / superpixelDim;
  if ((inputImg.cols % superpixelDim) != 0) {
    blockWidth++;
  }
  int blockHeight = inputImg.rows / superpixelDim;
  if ((inputImg.rows % superpixelDim) != 0) {
    blockHeight++;
  }
  
  assert((blockWidth * superpixelDim) >= inputImg.cols);
  assert((blockHeight * superpixelDim) >= inputImg.rows);
  
  for(int y = 0; y < inputImg.rows; y++) {
    int yStep = y >> 2;
    
    for(int x = 0; x < inputImg.cols; x++) {
      int xStep = x >> 2;

      uint32_t tag = (yStep * blockWidth) + xStep;
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "for (%4d,%4d) tag is %d\n", x, y, tag);
        cout << buffer;
      }
      
      Vec3b vec = PixelToVec3b(tag);
      
      tagsImg.at<Vec3b>(y, x) = vec;
    }
    
    if (debugOutput) {
    cout << endl;
    }
  }

  
  bool worked = SuperpixelImage::parse(tagsImg, spImage);
  
  if (!worked) {
    return false;
  }
  
  // Dump image that shows the input superpixels written with a colortable
  
  resultImg = inputImg.clone();
  resultImg = (Scalar) 0;
  
  sranddev();
  
  if (debugWriteIntermediateFiles) {
    generateStaticColortable(inputImg, spImage);
  }
  
  if (debugWriteIntermediateFiles) {
    writeTagsWithStaticColortable(spImage, resultImg);
    imwrite("tags_init.png", resultImg);
  }
  
  cout << "started with " << spImage.superpixels.size() << " superpixels" << endl;
  
  // Identical
  
  spImage.mergeIdenticalSuperpixels(inputImg);
  
  if ((
#if defined(DEBUG)
       1
#else
       0
#endif // DEBUG
       )) {
    auto vec = spImage.sortSuperpixelsBySize();
    assert(vec.size() > 0);
  }
  
  if (debugWriteIntermediateFiles) {
    writeTagsWithStaticColortable(spImage, resultImg);
    imwrite("tags_after_identical_merge.png", resultImg);
  }
  
  // Do initial invocation of quant logic with an N that depends on the number
  // of large identical regions.
  
  if ((1)) {
    const bool debugOutput = false;
    
    int numPixels = inputImg.rows * inputImg.cols;
    
    uint32_t *pixels = new uint32_t[numPixels];
    assert(pixels);
    uint32_t pi = 0;
    
    for(int y = 0; y < inputImg.rows; y++) {
      for(int x = 0; x < inputImg.cols; x++) {
        Vec3b vec = inputImg.at<Vec3b>(y, x);
        uint32_t pixel = Vec3BToUID(vec);
        
        if ((debugOutput)) {
          char buffer[1024];
          snprintf(buffer, sizeof(buffer), "for (%4d,%4d) pixel is %d\n", x, y, pixel);
          cout << buffer;
        }
        
        pixels[pi++] = pixel;
      }
      
      if (debugOutput) {
        cout << endl;
      }
    }
    
    uint32_t *inPixels = pixels;
    uint32_t *outPixels = new uint32_t[numPixels];
    assert(outPixels);
    
    // Determine a good N (number of clusters)
    
    vector<int32_t> largestSuperpixelResults;
    spImage.scanLargestSuperpixels(largestSuperpixelResults, 0);
    
    if (largestSuperpixelResults.size() > 0) {
      assert(largestSuperpixelResults.size() > 0);
      int32_t largestSuperpixelTag = largestSuperpixelResults[0];
      
      // Typically the largest superpixel is the background, so pop the first
      // element and then run the stddev logic again.
      
      largestSuperpixelResults = spImage.getSuperpixelsVec();
      
      for ( int offset = 0; offset < largestSuperpixelResults.size(); offset++ ) {
        if (largestSuperpixelResults[offset] == largestSuperpixelTag) {
          largestSuperpixelResults.erase(largestSuperpixelResults.begin() + offset);
          break;
        }
      }
      
      spImage.scanLargestSuperpixels(largestSuperpixelResults, (superpixelDim*superpixelDim)); // min is 16 pixels
    }
    
 //   int32_t largestSuperpixelTag = largestSuperpixelResults[0];
    //    vector<int32_t> sortedSuperpixels = spImage.sortSuperpixelsBySize();
    
    const int numClusters = 256;
//    int numClusters = 1 + (int)largestSuperpixelResults.size();
    
    cout << "numClusters detected as " << numClusters << endl;
    
    uint32_t *colortable = new uint32_t[numClusters];
    
    uint32_t numActualClusters = numClusters;
    
    int allPixelsUnique = 0;
    
    quant_recurse(numPixels, inPixels, outPixels, &numActualClusters, colortable, allPixelsUnique );
    
    // Write quant output where each original pixel is replaced with the closest
    // colortable entry.
    
    dumpQuantImage("quant_output.png", inputImg, outPixels);
    
    dumpQuantTableImage("quant_table.png", inputImg, colortable, numActualClusters);
    
    // Generate color sorted clusters
    
    {
      vector<uint32_t> clusterCenterPixels;
      
      for ( int i = 0; i < numActualClusters; i++) {
        uint32_t pixel = colortable[i];
        clusterCenterPixels.push_back(pixel);
      }
      
#if defined(DEBUG)
      if ((1)) {
        unordered_map<uint32_t, uint32_t> seen;
        
        for ( int i = 0; i < numActualClusters; i++ ) {
          uint32_t pixel;
          pixel = colortable[i];
          
          if (seen.count(pixel) > 0) {
          } else {
            // Note that only the first seen index is retained, this means that a repeated
            // pixel value is treated as a dup.
            
            seen[pixel] = i;
          }
        }
        
        int numQuantUnique = (int)seen.size();
        assert(numQuantUnique == numActualClusters);
      }
#endif // DEBUG

      if ((0)) {
        fprintf(stdout, "numClusters %5d : numActualClusters %5d \n", numClusters, numActualClusters);
        
        unordered_map<uint32_t, uint32_t> seen;
        
        for ( int i = 0; i < numActualClusters; i++ ) {
          uint32_t pixel;
          pixel = colortable[i];
          
          if (seen.count(pixel) > 0) {
            fprintf(stdout, "cmap[%3d] = 0x%08X (DUP of %d)\n", i, pixel, seen[pixel]);
          } else {
            fprintf(stdout, "cmap[%3d] = 0x%08X\n", i, pixel);
            
            // Note that only the first seen index is retained, this means that a repeated
            // pixel value is treated as a dup.
            
            seen[pixel] = i;
          }
        }
        
        fprintf(stdout, "cmap contains %3d unique entries\n", (int)seen.size());
        
        int numQuantUnique = (int)seen.size();
        
        assert(numQuantUnique == numActualClusters);
      }
      
      vector<uint32_t> sortedOffsets = generate_cluster_walk_on_center_dist(clusterCenterPixels);
      
      vector<uint32_t> colortableVec;
      
      // Once cluster centers have been sorted by 3D color cube distance, emit "centers.png"
      
      Mat sortedQtableOutputMat = Mat(numActualClusters, 1, CV_8UC3);
      sortedQtableOutputMat = (Scalar) 0;
      
      for (int i = 0; i < numActualClusters; i++) {
        int si = (int) sortedOffsets[i];
        uint32_t pixel = colortable[si];
        colortableVec.push_back(pixel);
        Vec3b vec = PixelToVec3b(pixel);
        sortedQtableOutputMat.at<Vec3b>(i, 0) = vec;
      }
      
      char *outQuantTableFilename = (char*)"quant_table_sorted.png";
      imwrite(outQuantTableFilename, sortedQtableOutputMat);
      cout << "wrote " << outQuantTableFilename << endl;
      
      // Map quant pixels to colortable offsets
      
      Mat quantMat = inputImg.clone();
      quantMat = (Scalar) 0;
      
      {
        int pi = 0;
        for(int y = 0; y < quantMat.rows; y++) {
          for(int x = 0; x < quantMat.cols; x++) {
            uint32_t pixel = outPixels[pi++];
            Vec3b vec = PixelToVec3b(pixel);
            quantMat.at<Vec3b>(y, x) = vec;
          }
        }
      }
      
      // Map the quant pixels to indexes into the colortable
      
      Mat sortedQuantIndexOutputMat = mapQuantPixelsToColortableIndexes(quantMat, colortableVec, true);
      
      char *outQuantFilename = (char*)"quant_sorted_offsets.png";
      imwrite(outQuantFilename, sortedQuantIndexOutputMat);
      cout << "wrote " << outQuantFilename << endl;
    }
    
    // Quant to known evenly spaced matrix
    
    {
      unordered_map<Coord, HistogramForBlock> blockMap;
      
      Mat blockMat =
      genHistogramsForBlocks(inputImg, blockMap, blockWidth, blockHeight, superpixelDim);
      
      Mat blockMaskMat(blockMat.rows, blockMat.cols, CV_8UC1);
      
      blockMaskMat = (Scalar) 0xFF;
      
      unordered_map<uint32_t, uint32_t> pixelToNumVotesMap;
      
      vote_for_identical_neighbors(pixelToNumVotesMap, blockMat, blockMaskMat);

      vector<uint32_t> sortedPixelKeys = sort_keys_by_count(pixelToNumVotesMap, true);
      
      for ( uint32_t pixel : sortedPixelKeys ) {
        uint32_t count = pixelToNumVotesMap[pixel];
        fprintf(stdout, "0x%08X (%8d) -> %5d\n", pixel, pixel, count);
      }
      
      fprintf(stdout, "done\n");
    }
    
    // Generate global quant to spaced subdivisions
    
    {
      vector<uint32_t> colors = getSubdividedColors();
      
      uint32_t numColors = (uint32_t) colors.size();
      uint32_t *colortable = new uint32_t[numColors];
      
      {
        int i = 0;
        for ( uint32_t color : colors ) {
          colortable[i++] = color;
        }
      }
      
      if ((1)) {
        Mat pixelsTableMat(1, numColors, CV_8UC3);
        
        for (int i = 0; i < numColors; i++) {
          uint32_t pixel = colortable[i];
          
          if ((1)) {
            fprintf(stdout, "colortable[%4d] = 0x%08X\n", i, pixel);
          }
          
          Vec3b vec = PixelToVec3b(pixel);
          pixelsTableMat.at<Vec3b>(0, i) = vec;
        }
       
        char *filename = (char*)"quant_table_pixels.png";
        imwrite(filename, pixelsTableMat);
        cout << "wrote " << filename << endl;
      }
      
      map_colors_mps(pixels, numPixels, outPixels, colortable, numColors);
      
      // Write quant output where each original pixel is replaced with the closest
      // colortable entry.
      
      Mat quant8Mat = dumpQuantImage("quant_crayon_output.png", inputImg, outPixels);
      
      // Map quant output to indexes
      
      vector<uint32_t> colortableVec;
      
      for (int i = 0; i < numColors; i++) {
        uint32_t pixel = colortable[i];
        colortableVec.push_back(pixel);
      }
     
      Mat sortedQuantIndexOutputMat = mapQuantPixelsToColortableIndexes(quant8Mat, colortableVec, true);
      
      char *outQuantFilename = (char*)"quant_crayon_sorted_offsets.png";
      imwrite(outQuantFilename, sortedQuantIndexOutputMat);
      cout << "wrote " << outQuantFilename << endl;
      
      // Generate histogram from quant pixels
      
      unordered_map<uint32_t, uint32_t> pixelToCountTable;
      
      generatePixelHistogram(quant8Mat, pixelToCountTable);
      
      for ( auto it = begin(pixelToCountTable); it != end(pixelToCountTable); ++it) {
        uint32_t pixel = it->first;
        uint32_t count = it->second;
        
        printf("count table[0x%08X] = %6d\n", pixel, count);
      }

      printf("done\n");
    }
    
    // dealloc
    
    delete [] pixels;
    delete [] outPixels;
    delete [] colortable;
  }
  
  if ((0)) {
    // Attempt to merge based on a likeness predicate
    
    spImage.mergeSuperpixelsWithPredicate(inputImg);
    
    if (debugWriteIntermediateFiles) {
      writeTagsWithStaticColortable(spImage, resultImg);
      imwrite("tags_after_predicate_merge.png", resultImg);
    }
  }

  if ((0)) {
    // Attempt to merge regions that are very much alike
    // based on a histogram comparison. When the starting
    // point is identical regions then the regions near
    // identical regions are likely to be very alike
    // up until a hard edge.

    int mergeStep = 0;
    
    MergeSuperpixelImage::mergeBackprojectSuperpixels(spImage, inputImg, 1, mergeStep, BACKPROJECT_HIGH_FIVE8);
    
    if (debugWriteIntermediateFiles) {
      writeTagsWithStaticColortable(spImage, resultImg);
      imwrite("tags_after_histogram_merge.png", resultImg);
    }
  }
  
  if ((0)) {
  Mat minImg;
  writeTagsWithMinColortable(spImage, inputImg, minImg);
  imwrite("tags_min_color.png", minImg);
  cout << "wrote " << "tags_min_color.png" << endl;
  }
  
  if ((1)) {
    // SRM

//    double Q = 16.0;
//    double Q = 32.0;
//    double Q = 64.0; // Not too small
    double Q = 128.0; // keeps small circles together
//    double Q = 256.0;
//    double Q = 512.0;
    
    Mat srmTags = generateSRM(inputImg, Q);
    
    // Scan the tags generated by SRM and create superpixels of vario
    
    SuperpixelImage srmSpImage;
    
    bool worked = SuperpixelImage::parse(srmTags, srmSpImage);
    
    if (!worked) {
      return false;
    }
    
    if (debugWriteIntermediateFiles) {
      generateStaticColortable(inputImg, srmSpImage);
    }

    if (debugWriteIntermediateFiles) {
      Mat tmpResultImg = resultImg.clone();
      tmpResultImg = (Scalar) 0;
      writeTagsWithStaticColortable(srmSpImage, tmpResultImg);
      imwrite("srm_tags.png", tmpResultImg);
    }
    
    // Fill with UID+1
    
    srmSpImage.fillMatrixWithSuperpixelTags(srmTags);
    
    cout << "srm generated superpixels N = " << srmSpImage.superpixels.size() << endl;
    
    // Scan the largest superpixel regions in largest to smallest order and find
    // overlap between the SRM generated superpixels.
    
    vector<int32_t> srmSuperpixels = srmSpImage.sortSuperpixelsBySize();
    
    unordered_map<int32_t, set<int32_t> > srmSuperpixelToExactMap;
    
    Mat renderedTagsMat = resultImg.clone();
    renderedTagsMat = (Scalar) 0;
    
    spImage.fillMatrixWithSuperpixelTags(renderedTagsMat);

    for ( int32_t tag : srmSuperpixels ) {
      Superpixel *spPtr = srmSpImage.getSuperpixelPtr(tag);
      assert(spPtr);
    
      // Find all the superpixels that are all inside a larger superpixel
      // and then process the contained elements.
      
      // Find overlap between largest superpixels and the known all same superpixels
      
      set<int32_t> &otherTagsSet = srmSuperpixelToExactMap[tag];
      
      for ( Coord coord : spPtr->coords ) {
        Vec3b vec = renderedTagsMat.at<Vec3b>(coord.y, coord.x);
        uint32_t otherTag = Vec3BToUID(vec);
        
        if (otherTagsSet.find(otherTag) == otherTagsSet.end()) {
          if ((1)) {
            fprintf(stdout, "coord (%4d,%4d) = found tag 0x%08X aka %8d\n", coord.x, coord.y, otherTag, otherTag);
          }
          
          otherTagsSet.insert(otherTagsSet.end(), otherTag);
        }
        
        if ((0)) {
          fprintf(stdout, "coord (%4d,%4d) = 0x%08X aka %8d\n", coord.x, coord.y, otherTag, otherTag);
        }
        
        // Lookup a superpixel with this specific tag just to make sure it exists
#if defined(DEBUG)
        Superpixel *otherSpPtr = spImage.getSuperpixelPtr(otherTag);
        assert(otherSpPtr);
        assert(otherSpPtr->tag == otherTag);
#endif // DEBUG
      }
      
      cout << "for SRM superpixel " << tag << " : other tags ";
      for ( int32_t otherTag : otherTagsSet ) {
        cout << otherTag << " ";
      }
      cout << endl;
    } // end foreach srmSuperpixels
    
    
    // FIXME: very very small srm superpixels, like say 2x8 or a small long region might not need to be processed
    // since a larger region may expand and then cover the sliver at the end of a region. But, in that case
    // the expansion should mark a given superpixel as processed. See Yin/Yang for issue near top right
    
    // Loop over the otherTagsSet and find any tags that appear in
    // multiple regions.
    
    if ((1)) {
      vector<int32_t> tagsToRemove;
      
      set<int32_t> allSet;
      
      for ( int32_t tag : srmSuperpixels ) {
        set<int32_t> &otherTagsSet = srmSuperpixelToExactMap[tag];
        
        for ( int32_t otherTag : otherTagsSet ) {
          if (allSet.find(otherTag) != allSet.end()) {
            tagsToRemove.push_back(otherTag);
          }
          allSet.insert(allSet.end(), otherTag);
        }
      }
      
      for ( int32_t tag : srmSuperpixels ) {
        set<int32_t> &otherTagsSet = srmSuperpixelToExactMap[tag];
        
        for ( int32_t tag : tagsToRemove ) {
          if ( otherTagsSet.find(tag) != otherTagsSet.end() ) {
            otherTagsSet.erase(tag);
          }
        }
      }
      
      // Dump the removed regions as a mask
      
      if (debugWriteIntermediateFiles) {
        Mat tmpResultImg = resultImg.clone();
        tmpResultImg = (Scalar) 0;
        
        for ( int32_t tag : tagsToRemove ) {
          Superpixel *spPtr = spImage.getSuperpixelPtr(tag);
          assert(spPtr);
          
          Vec3b whitePixel(0xFF, 0xFF, 0xFF);
          
          for ( Coord c : spPtr->coords ) {
            tmpResultImg.at<Vec3b>(c.y, c.x) = whitePixel;
          }
        }
        
        std::stringstream fnameStream;
        fnameStream << "merge_removed_union" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, tmpResultImg);
        cout << "wrote " << fname << endl;
      }
    }
    
    // Foreach SRM superpixel, find the set of superpixels
    // in the identical tags that correspond to a union of
    // the pixels in the SRM region and the identical region.
    
    for ( int32_t tag : srmSuperpixels ) {
      set<int32_t> &otherTagsSet = srmSuperpixelToExactMap[tag];
      
      if ((1)) {
      cout << "srm superpixels " << tag << " corresponds to other tags : ";
      for ( int32_t otherTag : otherTagsSet ) {
        cout << otherTag << " ";
      }
      cout << endl;
      }
      
      // For the large SRM superpixel determine the set of superpixels
      // contains in the region by looking at the other tags image.
      
      Mat regionMat = Mat(resultImg.rows, resultImg.cols, CV_8UC1);

      regionMat = (Scalar) 0;
      
      int numCoords = 0;

      vector<Coord> unprocessedCoords;
      
      for ( int32_t otherTag : otherTagsSet ) {
        Superpixel *spPtr = spImage.getSuperpixelPtr(otherTag);
        assert(spPtr);
        
        if ((1)) {
          cout << "superpixel " << otherTag << " with N = " << spPtr->coords.size() << endl;
        }
        
        for ( Coord c : spPtr->coords ) {
          regionMat.at<uint8_t>(c.y, c.x) = 0xFF;
          // Slow bbox calculation simply records all the (X,Y) coords in all the
          // superpixels and then does a bbox using these coords. A faster method
          // would be to do a bbox on each superpixel and then save the upper left
          // and upper right coords only.
          unprocessedCoords.push_back(c);
          numCoords++;
        }
      }
      
      if (numCoords == 0) {
        cout << "zero unprocessed pixels for SRM superpixel " << tag << endl;
      } else {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_N_" << numCoords << "_tag_" << tag << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, regionMat);
        cout << "wrote " << fname << endl;
      }
      
      if ((false) && (numCoords != 0)) {
        // The same type of logic implemented as a morphological operation in terms of 4x4 blocks
        // represented as pixels.
        
        Mat morphBlockMat = Mat(blockHeight, blockWidth, CV_8U);
        morphBlockMat = (Scalar) 0;
        
        // Get the first coord for each block that is indicated as inside the SRM superpixel
        
        for ( int32_t otherTag : otherTagsSet ) {
          Superpixel *spPtr = spImage.getSuperpixelPtr(otherTag);
          assert(spPtr);
          
          if ((1)) {
            cout << "unprocessed superpixel " << otherTag << " with N = " << spPtr->coords.size() << endl;
          }
          
          for ( Coord c : spPtr->coords ) {
            // Convert (X,Y) to block (X,Y)
            
            int blockX = c.x / superpixelDim;
            int blockY = c.y / superpixelDim;
            
            if ((0)) {
              cout << "block with tag " << otherTag << " cooresponds to (X,Y) (" << c.x << "," << c.y << ")" << endl;
              cout << "maps to block (X,Y) (" << blockX << "," << blockY << ")" << endl;
            }
            
            // FIXME: optimize for case where (X,Y) is exactly the same as in the previous iteration and avoid
            // writing to the Mat in that case. This shift is cheap.
            
            morphBlockMat.at<uint8_t>(blockY, blockX) = 0xFF;
          }
        }
        
        Mat expandedBlockMat;
        
        for (int expandStep = 0; expandStep < 8; expandStep++ ) {
          if (expandStep == 0) {
            expandedBlockMat = morphBlockMat;
          } else {
            expandedBlockMat = expandWhiteInRegion(expandedBlockMat, 1, tag);
          }
          
          int nzc = countNonZero(expandedBlockMat);
          
          Mat morphBlockMat = Mat(blockHeight, blockWidth, CV_8U);
          
          if (nzc == (blockHeight * blockWidth)) {
            cout << "all pixels in Mat now white " << endl;
            break;
          }
          
          if ((1)) {
            std::stringstream fnameStream;
            fnameStream << "srm" << "_tag_" << tag << "_morph_block_" << expandStep << ".png";
            string fname = fnameStream.str();
            
            imwrite(fname, expandedBlockMat);
            cout << "wrote " << fname << endl;
          }
          
          // Map morph blocks back to rectangular ROI in original image and extract ROI
          
          vector<Point> locations;
          findNonZero(expandedBlockMat, locations);
          
          vector<Coord> minMaxCoords;
          
          for ( Point p : locations ) {
            int actualX = p.x * superpixelDim;
            int actualY = p.y * superpixelDim;
            
            Coord min(actualX, actualY);
            minMaxCoords.push_back(min);
            
            Coord max(actualX+superpixelDim-1, actualY+superpixelDim-1);
            minMaxCoords.push_back(max);
          }
          
          int32_t originX, originY, width, height;
          Superpixel::bbox(originX, originY, width, height, minMaxCoords);
          Rect expandedRoi(originX, originY, width, height);
          
          Mat roiInputMat = inputImg(expandedRoi);
          
          if ((1)) {
            std::stringstream fnameStream;
            fnameStream << "srm" << "_tag_" << tag << "_morph_block_input_" << expandStep << ".png";
            string fname = fnameStream.str();
            
            imwrite(fname, roiInputMat);
            cout << "wrote " << fname << endl;
          }
          
        } // for expandStep
        
      } // end if numCoords
      
    } // end foreach srmSuperpixels

    // Merge manager will iterate over the superpixels found by
    // doing a union of the SRM regions and the superpixels.
    
    if (debugWriteIntermediateFiles) {
      generateStaticColortable(inputImg, spImage);
    }
    
    if (debugWriteIntermediateFiles) {
      Mat tmpResultImg = resultImg.clone();
      tmpResultImg = (Scalar) 0;
      
      writeTagsWithStaticColortable(spImage, tmpResultImg);
      
      std::stringstream fnameStream;
      fnameStream << "merge_step_" << 0 << ".png";
      string fname = fnameStream.str();
      
      imwrite(fname, tmpResultImg);
      cout << "wrote " << fname << endl;
    }
    
    SRMMergeManager mergeManager(spImage, inputImg);
    
    for ( int32_t tag : srmSuperpixels ) {
      set<int32_t> &otherTagsSet = srmSuperpixelToExactMap[tag];
      
      if ((1)) {
        cout << "srm superpixels " << tag << " corresponds to other tags : ";
        for ( int32_t otherTag : otherTagsSet ) {
          cout << otherTag << " ";
        }
        cout << endl;
      }
      
      mergeManager.otherTagsSetPtr = &otherTagsSet;
      
      SuperpixelMergeManagerFunc<SRMMergeManager>(mergeManager);
    }
    
    // With the overall merge completed, generate a block Mat
    // for each large superpixel so that specific pixel values
    // for the area around the region can be queried.
    
    Mat maskMat(inputImg.rows, inputImg.cols, CV_8UC1);
    Mat mergeMat(inputImg.rows, inputImg.cols, CV_8UC3);
    
    auto spVec = spImage.sortSuperpixelsBySize();
    
    for ( int32_t tag : spVec ) {
      
      bool maskWritten =
      captureRegionMask(spImage, inputImg, srmTags, tag, blockWidth, blockHeight, superpixelDim, maskMat);
      
      if (maskWritten)
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_region_mask" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, maskMat);
        cout << "wrote " << fname << endl;
        cout << "";
      }
      
      if (maskWritten) {        
        vector<Point> locations;
        findNonZero(maskMat, locations);
        
        unordered_map<uint32_t,vector<Coord> > coordTable;
        
        for ( Point p : locations ) {
          int x = p.x;
          int y = p.y;
          
          Coord c(x, y);
          
          // Find spImage tag associated with this (x,y) coordinate
          
          Vec3b vec = renderedTagsMat.at<Vec3b>(y, x);
          uint32_t renderedTag = Vec3BToUID(vec);
          
          if (renderedTag == tag) {
            // The region that pixels will be merged into
          } else {
            vector<Coord> &mergeCoords = coordTable[renderedTag];
            mergeCoords.push_back(c);
          }
          
          //printf("coord (%5d, %5d) = 0x%08X\n", c.x, c.y, pixel);
        }
        
        if (debug) {
          for ( auto &pair : coordTable ) {
            uint32_t pixel = pair.first;
            vector<Coord> &vec = pair.second;
            
            printf("pixel->srmTag table[0x%08X] = num coords %d\n", pixel, (int)vec.size());
          }
          
          printf("\n");
        }
        
        // Merge by pulling the indicated coordinates out of the associated superpixels
        
        Superpixel *dstSpPtr = spImage.getSuperpixelPtr(tag);
        assert(dstSpPtr);
        
        for ( auto &pair : coordTable ) {
          uint32_t renderedTag = pair.first;
          vector<Coord> &vec = pair.second;
          
          Superpixel *srcSpPtr = spImage.getSuperpixelPtr(renderedTag);
          assert(srcSpPtr);
          
          const vector<Coord> &srcCoords = srcSpPtr->coords;
          
          vector<Coord> filteredVec;
          filteredVec.reserve(srcSpPtr->coords.size());
          
          unordered_map<Coord, uint32_t> toRemoveMap;
          
          //append_to_vector(dstSpPtr->coords, vec);
          
          for ( Coord c : vec ) {
            toRemoveMap[c] = 0;
          }
          
          if (debug) {
            int i = 0;
            for ( Coord c : vec ) {
              printf("vec[%5d] = (%5d,%5d)\n", i, c.x, c.y);
              i++;
            }
            
            printf("\n");
          }
          
          if (debug) {
            int i = 0;
            for ( auto &pair : toRemoveMap ) {
              Coord c = pair.first;
              printf("toRemoveMap[%5d] = (%5d,%5d)\n", i, c.x, c.y);
              i++;
            }
            
            printf("\n");
          }
          
          if (debug) {
            int i = 0;
            for ( Coord c : srcCoords ) {
              printf("pre filtered[%5d] = (%5d,%5d)\n", i, c.x, c.y);
              i++;
            }
            
            printf("\n");
          }
          
          for ( Coord c : srcCoords ) {
            if (toRemoveMap.count(c) > 0) {
              // Skip to remove
            } else {
              filteredVec.push_back(c);
            }
          }
          
          if (debug) {
            int i = 0;
            for ( Coord c : filteredVec ) {
              printf("filtered[%5d] = (%5d,%5d)\n", i, c.x, c.y);
              i++;
            }
            
            printf("\n");
          }
          
          assert(srcCoords.size() == (filteredVec.size() + toRemoveMap.size()));
          //dstSpPtr->coords = filteredVec;
          
          printf("pixel->srmTag table[0x%08X] = num coords %d\n", renderedTag, (int)vec.size());
          
          {
          int32_t tagToRender = tag; // dst tag
          //int32_t tagToRender = renderedTag; // src tag
          Vec3b renderedTagVec = Vec3BToUID(tagToRender);
          
          for ( Coord c : vec ) {
            mergeMat.at<Vec3b>(c.y, c.x) = renderedTagVec;
          }
          }
        } // foreach vec of coords
        
        if (debugWriteIntermediateFiles) {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_merge_region" << ".png";
          string fname = fnameStream.str();
          
          imwrite(fname, mergeMat);
          cout << "wrote " << fname << endl;
          cout << "" << endl;
        }
        
      } // foreach tag in sorted superpixels

    }
    
  }
  
  // Generate result image after region based merging
  
  if (debugWriteIntermediateFiles) {
    generateStaticColortable(inputImg, spImage);
    writeTagsWithStaticColortable(spImage, resultImg);
    imwrite("tags_after_region_merge.png", resultImg);
  }
  
  // Done
  
  cout << "ended with " << spImage.superpixels.size() << " superpixels" << endl;
  
  return true;
}

