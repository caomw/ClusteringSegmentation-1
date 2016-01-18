//
//  ClusteringSegmentation.cpp
//  ClusteringSegmentation
//
//  Created by Mo DeJong on 1/17/16.
//  Copyright © 2016 helpurock. All rights reserved.
//

#include "ClusteringSegmentation.hpp"

#include <opencv2/opencv.hpp>

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

// Given an input image and a pixel buffer that is of the same dimensions
// write the buffer of pixels out as an image in a file.

Mat dumpQuantImage(string filename, const Mat &inputImg, uint32_t *pixels) {
  Mat quantOutputMat = inputImg.clone();
  quantOutputMat = (Scalar) 0;
  
  const bool debugOutput = false;
  
  int pi = 0;
  for (int y = 0; y < quantOutputMat.rows; y++) {
    for (int x = 0; x < quantOutputMat.cols; x++) {
      uint32_t pixel = pixels[pi++];
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "for (%4d,%4d) pixel is 0x%08X\n", x, y, pixel);
        cout << buffer;
      }
      
      Vec3b vec = PixelToVec3b(pixel);
      
      quantOutputMat.at<Vec3b>(y, x) = vec;
    }
  }
  
  imwrite(filename, quantOutputMat);
  cout << "wrote " << filename << endl;
  return quantOutputMat;
}

// Dump N x 1 image that contains pixels

void dumpQuantTableImage(string filename, const Mat &inputImg, uint32_t *colortable, uint32_t numColortableEntries)
{
  // Write image that contains one color in each row in a N x 1 image
  
  Mat qtableOutputMat = Mat(numColortableEntries, 1, CV_8UC3);
  qtableOutputMat = (Scalar) 0;
  
  vector<uint32_t> clusterCenterPixels;
  
  for ( int i = 0; i < numColortableEntries; i++) {
    uint32_t pixel = colortable[i];
    clusterCenterPixels.push_back(pixel);
  }
  
#if defined(DEBUG)
  if ((1)) {
    fprintf(stdout, "numClusters %5d\n", numColortableEntries);
    
    unordered_map<uint32_t, uint32_t> seen;
    
    for ( int i = 0; i < numColortableEntries; i++ ) {
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
    
    assert(numQuantUnique == numColortableEntries);
  }
#endif // DEBUG
  
  vector<uint32_t> sortedOffsets = generate_cluster_walk_on_center_dist(clusterCenterPixels);
  
  for (int i = 0; i < numColortableEntries; i++) {
    int si = (int) sortedOffsets[i];
    uint32_t pixel = colortable[si];
    Vec3b vec = PixelToVec3b(pixel);
    qtableOutputMat.at<Vec3b>(i, 0) = vec;
  }
  
  imwrite(filename, qtableOutputMat);
  cout << "wrote " << filename << endl;
  return;
}

// Generate a tags Mat from the original input pixels based on SRM algo.

Mat generateSRM(const Mat &inputImg, double Q)
{
  // SRM
  
  const bool debugOutput = false;
  const bool debugDumpImage = false;
  
  int numPixels = inputImg.rows * inputImg.cols;
  
  assert(inputImg.channels() == 3);
  
  const int channels = 3;
  
  uint8_t *in = new uint8_t[numPixels * channels]();
  uint8_t *out = new uint8_t[numPixels * channels]();
  
  int i = 0;
  for(int y = 0; y < inputImg.rows; y++) {
    for(int x = 0; x < inputImg.cols; x++) {
      Vec3b vec = inputImg.at<Vec3b>(y, x);
      
      uint8_t B = vec[0];
      uint8_t G = vec[1];
      uint8_t R = vec[2];
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "for IN (%4d,%4d) pixel is 0x00%02X%02X%02X -> offset %d\n", x, y, R, G, B, i);
        cout << buffer;
      }
      
      in[(i*3)+0] = B;
      in[(i*3)+1] = G;
      in[(i*3)+2] = R;
      i += 1;
    }
  }
  
  //double Q = 512.0;
  //double Q = 255.0;
  
  SRM(Q, inputImg.cols, inputImg.rows, channels, in, out, 0);
  
  //uint32_t *outPixels = new uint32_t[numPixels]();
  
  Mat outImg = inputImg.clone();
  outImg = (Scalar) 0;
  
  bool foundWhitePixel = false;
  uint32_t largestNonWhitePixel = 0x0;
  
  i = 0;
  for(int y = 0; y < outImg.rows; y++) {
    for(int x = 0; x < outImg.cols; x++) {
      uint32_t B = out[(i*3)+0];
      uint32_t G = out[(i*3)+1];
      uint32_t R = out[(i*3)+2];
      
      //uint32_t pixel = (R << 16) | (G << 8) | B;
      //outPixels[i] = pixel;
      i += 1;
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "for OUT (%4d,%4d) pixel is 0x00%02X%02X%02X -> offset %d\n", x, y, R, G, B, i);
        cout << buffer;
      }
      
      Vec3b vec;
      
      vec[0] = B;
      vec[1] = G;
      vec[2] = R;
      
      outImg.at<Vec3b>(y, x) = vec;
      
      if (B == 0xFF && G == 0xFF && R == 0xFF) {
        foundWhitePixel = true;
      } else {
        uint32_t pixel = (R << 16) | (G << 8) | (B);
        if (pixel > largestNonWhitePixel) {
          largestNonWhitePixel = pixel;
        }
      }
    }
  }
  
  if (foundWhitePixel) {
    // SRM output must not include the special case of color 0xFFFFFFFF since the
    // implicit +1 during paring would overflow the int value. Simply find an unused
    // near white color and use that instead.
    
    uint32_t nonWhitePixel = 0x00FFFFFF;
    
    nonWhitePixel -= 1;
    
    while (1) {
      if (nonWhitePixel != largestNonWhitePixel) {
        break;
      }
    }
    
    // nonWhitePixel now contains an unused pixel value
    
    Vec3b nonWhitePixelVec = PixelToVec3b(nonWhitePixel);
    
    if ((debugOutput)) {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "rewrite white pixel 0x%08X as 0x%08X\n", 0x00FFFFFF, nonWhitePixel);
      cout << buffer;
    }
    
    for(int y = 0; y < outImg.rows; y++) {
      for(int x = 0; x < outImg.cols; x++) {
        Vec3b vec = outImg.at<Vec3b>(y, x);
        uint32_t pixel = Vec3BToUID(vec);
        if (pixel == 0x00FFFFFF) {
          vec = nonWhitePixelVec;
          outImg.at<Vec3b>(y, x) = vec;
        }
      }
    }
  }
  
  if (debugDumpImage) {
    string filename = "srm.png";
    imwrite(filename, outImg);
    cout << "wrote " << filename << endl;
  }
  
  //  if (debugDumpImage) {
  //    dumpQuantImage("srm.png", inputImg, outPixels);
  //  }
  
  //  delete [] outPixels;
  delete [] in;
  delete [] out;
  
  return outImg;
}

// Generate a histogram for each block of 4x4 pixels in the input image.
// This logic maps input pixels to an even quant division of the color cube
// so that comparison based on the pixel frequency is easy on a region
// by region basis.

Mat genHistogramsForBlocks(const Mat &inputImg,
                           unordered_map<Coord, HistogramForBlock> &blockMap,
                           int blockWidth,
                           int blockHeight,
                           int superpixelDim)
{
  const bool debugOutput = false;
  const bool dumpOutputImages = false;
  
  uint32_t width = inputImg.cols;
  uint32_t height = inputImg.rows;
  
  uint32_t numPixels = width * height;
  uint32_t *inPixels = new uint32_t[numPixels];
  uint32_t *outPixels = new uint32_t[numPixels]();
  
  int pi = 0;
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      Vec3b vec = inputImg.at<Vec3b>(y, x);
      uint32_t pixel = Vec3BToUID(vec);
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "for (%4d,%4d) pixel is 0x%08X\n", x, y, pixel);
        cout << buffer;
      }
      
      inPixels[pi++] = pixel;
    }
  }
  
  vector<uint32_t> quantColors = getSubdividedColors();
  uint32_t numColors = (uint32_t) quantColors.size();
  uint32_t *colortable = new uint32_t[numColors];
  
  {
    int i = 0;
    for ( uint32_t color : quantColors ) {
      colortable[i++] = color;
    }
  }
  
  map_colors_mps(inPixels, numPixels, outPixels, colortable, numColors);
  
  if (dumpOutputImages) {
    Mat quantMat = dumpQuantImage("block_quant_full_output.png", inputImg, outPixels);
  }
  
  // Allocate Mat where a single quant value is selected for each block. Iterate over
  // each block and query the coordinates associated with a specific block.
  
  Mat blockMat = Mat(blockHeight, blockWidth, CV_8UC3);
  blockMat = (Scalar) 0;
  
  pi = 0;
  for(int by = 0; by < blockMat.rows; by++) {
    for(int bx = 0; bx < blockMat.cols; bx++) {
      Coord blockC(bx, by);
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "block (%4d,%4d)", bx, by);
        cout << buffer << endl;
      }
      
      int actualX = blockC.x * superpixelDim;
      int actualY = blockC.y * superpixelDim;
      
      Coord min(actualX, actualY);
      Coord max(actualX+superpixelDim-1, actualY+superpixelDim-1);
      
      if ((debugOutput)) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "block min (%4d,%4d) max (%4d,%4d)", min.x, min.y, max.x, max.y);
        cout << buffer << endl;
      }
      
      vector<uint32_t> pixelsThisBlock;
      
      bool isAllSamePixel = true;
      bool isFirstPixelSet = false;
      uint32_t firstPixel = 0x0;
      
      for (int y = actualY; y <= max.y; y++) {
        for (int x = actualX; x <= max.x; x++) {
          if ((debugOutput) && false) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "(%4d,%4d)", x, y);
            cout << buffer << endl;
          }
          
          if (x > width-1) {
            continue;
          }
          if (y > height-1) {
            continue;
          }
          
          Coord c(x, y);
          uint32_t pi = (y * width) + x;
          uint32_t quantPixel = outPixels[pi];
          
          if (!isFirstPixelSet) {
            // First pixel in block
            isFirstPixelSet = true;
            firstPixel = quantPixel;
            
            if (debugOutput) {
              cout << "detected first pixel in block at " << x << "," << y << endl;
            }
          }
          
          if ((debugOutput)) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "for (%4d,%4d) offset is %d pixel is 0x%08X\n", x, y, pi, quantPixel);
            cout << buffer;
          }
          
          pixelsThisBlock.push_back(quantPixel);
          
          if (isAllSamePixel) {
            if (quantPixel != firstPixel) {
              isAllSamePixel = false;
            }
          }
        }
      }
      
      if (debugOutput) {
        cout << "isAllSamePixel " << isAllSamePixel << " isFirstPixelSet " << isFirstPixelSet << " num pixelsThisBlock " << pixelsThisBlock.size() << endl;
      }
      
      assert(isFirstPixelSet && pixelsThisBlock.size() > 0);
      
      // Examine each quant pixel value in pixelsThisBlock and determine which quant pixel best
      // represents this block. Note that coord is the upper left coord in the block.
      
      HistogramForBlock &hfb = blockMap[blockC];
      
      unordered_map<uint32_t, uint32_t> &pixelToCountTable = hfb.pixelToCountTable;
      
      uint32_t maxPixel = 0x0;
      
      if (isAllSamePixel) {
        // Optimized common case where all pixels are exactly the same value
        
        maxPixel = pixelsThisBlock[0];
        
        pixelToCountTable[maxPixel] = (uint32_t)pixelsThisBlock.size();
        
        if ((debugOutput)) {
          char buffer[1024];
          snprintf(buffer, sizeof(buffer), "all pixels in block optimized case for pixel 0x%08X\n", maxPixel);
          cout << buffer;
        }
      } else {
        
        for ( uint32_t qp : pixelsThisBlock ) {
          if ((debugOutput)) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "histogram pixel 0x%08X\n", qp);
            cout << buffer;
          }
          
          pixelToCountTable[qp] += 1;
        }
        
        int maxCount = 0;
        
        for ( auto it = begin(pixelToCountTable); it != end(pixelToCountTable); ++it) {
          uint32_t pixel = it->first;
          uint32_t count = it->second;
          
          if ((debugOutput)) {
            printf("count table[0x%08X] = %6d\n", pixel, count);
          }
          
          if (count > maxCount) {
            maxCount = count;
            maxPixel = pixel;
          }
        }
        
        // FIXME: if these are anywhere close, then do a stddev and choose one that is way
        // larger than the others. But if really close then choose no specific pixel.
        
        if ((debugOutput)) {
          printf("maxCount %5d : maxPixel 0x%08X\n", maxCount, maxPixel);
          printf("done\n");
        }
      }
      
      hfb.regionQuantPixel = maxPixel;
      
      Vec3b vec = PixelToVec3b(maxPixel);
      blockMat.at<Vec3b>(by, bx) = vec;
    }
  }
  
  if (dumpOutputImages) {
    char *filename = (char*) "block_quant_output.png";
    imwrite(filename, blockMat);
    cout << "wrote " << filename << endl;
  }
  
  delete [] colortable;
  delete [] inPixels;
  delete [] outPixels;
  
  return blockMat;
}

// Given a tag indicating a superpixel generate a mask that captures the region in terms of
// exact pixels. This method returns a Mat that indicate a boolean region mask where 0xFF
// means that the pixel is inside the indicated region.

bool
captureRegionMask(SuperpixelImage &spImage,
                  const Mat & inputImg,
                  int32_t tag,
                  int blockWidth,
                  int blockHeight,
                  int superpixelDim,
                  Mat &outBlockMask)
{
  const bool debug = false;
  const bool debugDumpImages = true;
  
  if (debug) {
    cout << "captureRegionMask" << endl;
  }
  
  assert(outBlockMask.rows == inputImg.rows);
  assert(outBlockMask.cols == inputImg.cols);
  assert(outBlockMask.channels() == 1);
  
  auto &coords = spImage.getSuperpixelPtr(tag)->coords;
  
  if (coords.size() <= (superpixelDim*superpixelDim)) {
    // A region contained in only a single block, don't process by itself
    
    if (debug) {
      cout << "captureRegionMask : region indicated by tag " << tag << " is too small to process" << endl;
    }
    
    return false;
  }

  // Init mask after possible early return
  
  outBlockMask = (Scalar) 0;
  
  Mat expandedBlockMat = expandBlockRegion(tag, coords, 2, blockWidth, blockHeight, superpixelDim);
  
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
    
    if (max.x > inputImg.cols-1) {
      max.x = inputImg.cols-1;
    }
    if (max.y > inputImg.rows-1) {
      max.y = inputImg.rows-1;
    }
    
    minMaxCoords.push_back(max);
  }
  
  int32_t originX, originY, width, height;
  Superpixel::bbox(originX, originY, width, height, minMaxCoords);
  Rect expandedRoi(originX, originY, width, height);
  
  if (debugDumpImages) {
    Mat roiInputMat = inputImg(expandedRoi);
    
    std::stringstream fnameStream;
    fnameStream << "srm" << "_tag_" << tag << "_morph_block_input" << ".png";
    string fname = fnameStream.str();
    
    imwrite(fname, roiInputMat);
    cout << "wrote " << fname << endl;
  }
  
  if (debugDumpImages) {
    int width = inputImg.cols;
    int height = inputImg.rows;
    
    Mat tmpExpandedBlockMat(height, width, CV_8U);
    
    tmpExpandedBlockMat = (Scalar) 0;
    
    for ( Point p : locations ) {
      int actualX = p.x * superpixelDim;
      int actualY = p.y * superpixelDim;
      
      Coord min(actualX, actualY);
      Coord max(actualX+superpixelDim-1, actualY+superpixelDim-1);
      
      for ( int y = min.y; y <= max.y; y++ ) {
        for ( int x = min.x; x <= max.x; x++ ) {
          
          if (x > width-1) {
            continue;
          }
          if (y > height-1) {
            continue;
          }
          
          tmpExpandedBlockMat.at<uint8_t>(y, x) = 0xFF;
        }
      }
    }
    
    std::stringstream fnameStream;
    fnameStream << "srm" << "_tag_" << tag << "_morph_block_bw" << ".png";
    string fname = fnameStream.str();
    
    imwrite(fname, tmpExpandedBlockMat);
    cout << "wrote " << fname << endl;
  }
  
  // Generate a collection of pixels from the blocks included in the
  // expanded mask.
  
  if ((1)) {
    int width = inputImg.cols;
    int height = inputImg.rows;
    
    vector<Coord> regionCoords;
    regionCoords.reserve(locations.size() * (superpixelDim * superpixelDim));
    
    for ( Point p : locations ) {
      int actualX = p.x * superpixelDim;
      int actualY = p.y * superpixelDim;
      
      Coord min(actualX, actualY);
      Coord max(actualX+superpixelDim-1, actualY+superpixelDim-1);
      
      for ( int y = min.y; y <= max.y; y++ ) {
        for ( int x = min.x; x <= max.x; x++ ) {
          Coord c(x, y);
          
          if (x > width-1) {
            continue;
          }
          if (y > height-1) {
            continue;
          }
          
          regionCoords.push_back(c);
        }
      }
    }
    
    Mat tmpResultImg = inputImg.clone();
    tmpResultImg = Scalar(0,0,0xFF);
    
    int numPixels = (int) regionCoords.size();
    
    uint32_t *inPixels = new uint32_t[numPixels];
    uint32_t *outPixels = new uint32_t[numPixels];
    
    for ( int i = 0; i < numPixels; i++ ) {
      Coord c = regionCoords[i];
      Vec3b vec = inputImg.at<Vec3b>(c.y, c.x);
      uint32_t pixel = Vec3BToUID(vec);
      inPixels[i] = pixel;
      tmpResultImg.at<Vec3b>(c.y, c.x) = vec;
    }
    
    {
      std::stringstream fnameStream;
      fnameStream << "srm" << "_tag_" << tag << "_morph_masked_input" << ".png";
      string fname = fnameStream.str();
      
      imwrite(fname, tmpResultImg);
      cout << "wrote " << fname << endl;
    }
    
    if ((1)) {
      // Use estimation based on quant to 8 colors to determine the N value for the
      // number of clusters to pass into the kmeans segmentation logic.
      
      vector<uint32_t> colors = getSubdividedColors();
      
      uint32_t numColors = (uint32_t) colors.size();
      uint32_t *colortable = new uint32_t[numColors];
      
      {
        int i = 0;
        for ( uint32_t color : colors ) {
          colortable[i++] = color;
        }
      }
      
      map_colors_mps(inPixels, numPixels, outPixels, colortable, numColors);
      
      // Count each quant pixel in outPixels
      
      Mat countMat(1, numPixels, CV_8UC3);
      
      for (int i = 0; i < numPixels; i++) {
        uint32_t pixel = outPixels[i];
        Vec3b vec = PixelToVec3b(pixel);
        countMat.at<Vec3b>(0, i) = vec;
      }
      
      unordered_map<uint32_t, uint32_t> pixelToCountTable;
      
      generatePixelHistogram(countMat, pixelToCountTable);
      
      for ( auto it = begin(pixelToCountTable); it != end(pixelToCountTable); ++it) {
        uint32_t pixel = it->first;
        uint32_t count = it->second;
        
        printf("count table[0x%08X] = %6d\n", pixel, count);
      }
      
      // Dump quant output, each pixel is replaced by color in colortable
      
      tmpResultImg = Scalar(0,0,0xFF);
      
      for ( int i = 0; i < numPixels; i++ ) {
        Coord c = regionCoords[i];
        uint32_t pixel = outPixels[i];
        Vec3b vec = PixelToVec3b(pixel);
        tmpResultImg.at<Vec3b>(c.y, c.x) = vec;
      }
      
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_quant_output" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, tmpResultImg);
        cout << "wrote " << fname << endl;
      }
      
      // Map quant pixels to colortable offsets
      
      vector<uint32_t> colortableVec;
      
      for (int i = 0; i < numColors; i++) {
        uint32_t pixel = colortable[i];
        colortableVec.push_back(pixel);
      }
      
      // Add phony entry for Red (the mask color)
      colortableVec.push_back(0x00FF0000);
      
      Mat quantOffsetsMat = mapQuantPixelsToColortableIndexes(tmpResultImg, colortableVec, true);
      
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_quant_offsets" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, quantOffsetsMat);
        cout << "wrote " << fname << endl;
      }
      
      delete [] colortable;
    }
    
    
    // Estimate the number of clusters to use in a quant operation by
    // mapping the input pixels through an even quant table and then
    // convert to blocks that represent the quant regions. This logic
    // counts quant pixels that are next to other quant pixels such
    // that dense areas that quant to the same pixel are promoted to
    // a high count.
    
    // MOMO
    
    if (1) {
      
      unordered_map<Coord, HistogramForBlock> blockMap;
      
      Mat blockMat =
      genHistogramsForBlocks(inputImg, blockMap, blockWidth, blockHeight, superpixelDim);
      
      // Generate mask Mat that is the same dimensions as blockMat but contains just one
      // byte for each pixel and acts as a mask. The white pixels indicate the blocks
      // that are included in the mask.
      
      Mat blockMaskMat(blockMat.rows, blockMat.cols, CV_8U);
      blockMaskMat= (Scalar) 0;
      
      for ( Point p : locations ) {
        int blockX = p.x;
        int blockY = p.y;
        blockMaskMat.at<uint8_t>(blockY, blockX) = 0xFF;
      }
      
      if (debugDumpImages) {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_block_mask" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, blockMaskMat);
        cout << "wrote " << fname << endl;
      }
      
      // Count neighbors that share a quant pixel value after conversion to blocks
      
      unordered_map<uint32_t, uint32_t> pixelToNumVotesMap;
      
      vote_for_identical_neighbors(pixelToNumVotesMap, blockMat, blockMaskMat);
      
      vector<uint32_t> sortedPixelKeys = sort_keys_by_count(pixelToNumVotesMap, true);
      
      for ( uint32_t pixel : sortedPixelKeys ) {
        uint32_t count = pixelToNumVotesMap[pixel];
        fprintf(stdout, "0x%08X (%8d) -> %5d\n", pixel, pixel, count);
      }
      
      fprintf(stdout, "done\n");
      
      // Instead of a stddev type of approach, use grap peak logic to examine the counts
      // and select the peaks in the distrobution.
      
      vector<uint32_t> sortedOffsets = generate_cluster_walk_on_center_dist(sortedPixelKeys);
      
      // Once cluster centers have been sorted by 3D color cube distance, emit "centers.png"
      
      int numPoints = (int) sortedOffsets.size();
      
      Mat sortedQtableOutputMat = Mat(numPoints, 1, CV_8UC3);
      sortedQtableOutputMat = (Scalar) 0;
      
      vector<uint32_t> sortedColortable;
      
      for (int i = 0; i < numPoints; i++) {
        int si = (int) sortedOffsets[i];
        uint32_t pixel = sortedPixelKeys[si];
        Vec3b vec = PixelToVec3b(pixel);
        sortedQtableOutputMat.at<Vec3b>(i, 0) = vec;
        
        sortedColortable.push_back(pixel);
      }
      
      for ( uint32_t pixel : sortedColortable ) {
        uint32_t count = pixelToNumVotesMap[pixel];
        fprintf(stdout, "0x%08X (%8d) -> %5d\n", pixel, pixel, count);
      }
      
      fprintf(stdout, "done\n");
      
      // Dump sorted pixel data as a CSV file, with int value and hex rep of int value for readability
      
      std::stringstream fnameStream;
      fnameStream << "srm" << "_tag_" << tag << "_quant_table_sorted" << ".csv";
      string fname = fnameStream.str();
      
      FILE *fout = fopen(fname.c_str(), "w+");
      
      for ( uint32_t pixel : sortedColortable ) {
        uint32_t count = pixelToNumVotesMap[pixel];
        uint32_t pixelNoAlpha = pixel & 0x00FFFFFF;
        fprintf(fout, "%d,0x%08X,%d\n", pixelNoAlpha, pixelNoAlpha, count);
      }
      
      fclose(fout);
      cout << "wrote " << fname << endl;
      
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_block_mask_sorted" << ".png";
        string filename = fnameStream.str();
        
        char *outQuantTableFilename = (char*) filename.c_str();
        imwrite(outQuantTableFilename, sortedQtableOutputMat);
        cout << "wrote " << outQuantTableFilename << endl;
      }
      
      // Use peak detection logic to examine the 1D histogram in sorted order so as to find the
      // peaks in the distribution.
      
      int N = 0;
      vector<uint32_t> peakPixels;
      
      {
        // FIXME: dynamically allocate buffers to fit input size ?
        
        double*     data[2];
        //              double      row[2];
        
#define MAX_PEAK    256
        
        int         emi_peaks[MAX_PEAK];
        int         absorp_peaks[MAX_PEAK];
        
        int         emi_count = 0;
        int         absorp_count = 0;
        
        double      delta = 1e-6;
        int         emission_first = 0;
        
        int numDataPoints = (int) sortedColortable.size();
        
        assert(numDataPoints <= 256);
        
        data[0] = (double*) malloc(sizeof(double) * MAX_PEAK);
        data[1] = (double*) malloc(sizeof(double) * MAX_PEAK);
        
        memset(data[0], 0, sizeof(double) * MAX_PEAK);
        memset(data[1], 0, sizeof(double) * MAX_PEAK);
        
        int i = 0;
        
        i += 1;
        
        for ( uint32_t pixel : sortedColortable ) {
          uint32_t count = pixelToNumVotesMap[pixel];
          uint32_t pixelNoAlpha = pixel & 0x00FFFFFF;
          
          data[0][i] = pixelNoAlpha;
          //data[0][i] = i;
          data[1][i] = count;
          
          if ((0)) {
            fprintf(stderr, "pixel %05d : 0x%08X = %d\n", i, pixelNoAlpha, count);
          }
          
          i += 1;
        }
        
        // +1 at the end of the samples
        i += 1;
        
        // Print the input data with zeros at the front and the back
        
        for ( int j = 0; j < i; j++ ) {
          uint32_t pixelNoAlpha = data[0][j];
          uint32_t count = data[1][j];
          
          if ((1)) {
            fprintf(stderr, "pixel %05d : 0x%08X = %d\n", j, pixelNoAlpha, count);
          }
        }
        
        if(detect_peak(data[1], i,
                       emi_peaks, &emi_count, MAX_PEAK,
                       absorp_peaks, &absorp_count, MAX_PEAK,
                       delta, emission_first))
        {
          fprintf(stderr, "There are too many peaks.\n");
          exit(1);
        }
        
        fprintf(stdout, "num emi_peaks %d\n", emi_count);
        fprintf(stdout, "num absorp_peaks %d\n", absorp_count);
        
        for(i = 0; i < emi_count; ++i) {
          int offset = emi_peaks[i];
          fprintf(stdout, "%5d : %5d,%5d\n", offset, (int)data[0][offset], (int)data[1][offset]);
          
          uint32_t pixel = (uint32_t) round(data[0][offset]);
          peakPixels.push_back(pixel);
        }
        
        puts("");
        
        for(i = 0; i < absorp_count; ++i) {
          int offset = absorp_peaks[i];
          fprintf(stdout, "%5d : %5d,%5d\n", offset, (int)data[0][offset],(int)data[1][offset]);
        }
        
        free(data[0]);
        free(data[1]);
        
        // FIXME: if there seems to be just 1 peak, then it is likely that the other
        // points are another color range. Just assume N = 2 in that case ?
        
        N = (int) peakPixels.size();
        
        N = N * 4;
      }
      
      /*
       
       // Estimate N
       
       // Choice of N for splitting the masked area. Need 1 for the surrounding area, possibly
       // more if background is more than 1 color. But, need to select the other "target" color
       // to split from the background by looking at the density of the colors in (X,Y) terms.
       // For example, a dense patch of green should be seen as +1 over a surrounding gradient
       // even if there are more colors in the gradient but they are spread out.
       
       float mean, stddev;
       
       vector<float> floatSizes;
       
       for ( uint32_t pixel : sortedPixelKeys ) {
       uint32_t count = pixelToNumVotesMap[pixel];
       
       floatSizes.push_back(count);
       }
       
       sample_mean(floatSizes, &mean);
       sample_mean_delta_squared_div(floatSizes, mean, &stddev);
       
       if (1) {
       char buffer[1024];
       
       snprintf(buffer, sizeof(buffer), "mean %0.4f stddev %0.4f", mean, stddev);
       cout << (char*)buffer << endl;
       
       snprintf(buffer, sizeof(buffer), "1 stddev %0.4f", (mean + (stddev * 0.5f * 1.0f)));
       cout << (char*)buffer << endl;
       
       snprintf(buffer, sizeof(buffer), "2 stddev %0.4f", (mean + (stddev * 0.5f * 2.0f)));
       cout << (char*)buffer << endl;
       
       snprintf(buffer, sizeof(buffer), "3 stddev %0.4f", (mean + (stddev * 0.5f * 3.0f)));
       cout << (char*)buffer << endl;
       
       snprintf(buffer, sizeof(buffer), "-1 stddev %0.4f", (mean - (stddev * 0.5f * 1.0f)));
       cout << (char*)buffer << endl;
       
       snprintf(buffer, sizeof(buffer), "-2 stddev %0.4f", (mean - (stddev * 0.5f * 2.0f)));
       cout << (char*)buffer << endl;
       }
       
       // 1 for the background
       // 1 for the most common color group
       
       int N = 1;
       
       // Anything larger than 1 standard dev is likely to be a cluster of alike pixels
       
       float upOneStddev = (mean + (stddev * 0.5f * 1.0f));
       
       int countAbove = 0;
       
       for ( float floatSize : floatSizes ) {
       if (floatSize >= upOneStddev) {
       countAbove += 1;
       }
       }
       
       N += countAbove;
       //N = N * 2;
       N = N * 4;
       
       //          fprintf(stdout, "N = %5d\n", N);
       
       */
      
      // Generate quant based on the input
      
      const int numClusters = N;
      
      cout << "numClusters detected as " << numClusters << endl;
      
      uint32_t *colortable = new uint32_t[numClusters];
      
      uint32_t numActualClusters = numClusters;
      
      int allPixelsUnique = 0;
      
      quant_recurse(numPixels, inPixels, outPixels, &numActualClusters, colortable, allPixelsUnique );
      
      // Write quant output where each original pixel is replaced with the closest
      // colortable entry.
      
      tmpResultImg = Scalar(0,0,0xFF);
      
      for ( int i = 0; i < numPixels; i++ ) {
        Coord c = regionCoords[i];
        uint32_t pixel = outPixels[i];
        Vec3b vec = PixelToVec3b(pixel);
        tmpResultImg.at<Vec3b>(c.y, c.x) = vec;
      }
      
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_quant_output" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, tmpResultImg);
        cout << "wrote " << fname << endl;
      }
      
      // table
      
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_quant_table" << ".png";
        string fname = fnameStream.str();
        
        dumpQuantTableImage(fname, inputImg, colortable, numActualClusters);
      }
      
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
        
        if ((1)) {
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
        
        // Once cluster centers have been sorted by 3D color cube distance, emit "centers.png"
        
        Mat sortedQtableOutputMat = Mat(numActualClusters, 1, CV_8UC3);
        sortedQtableOutputMat = (Scalar) 0;
        
        vector<uint32_t> sortedColortable;
        
        for (int i = 0; i < numActualClusters; i++) {
          int si = (int) sortedOffsets[i];
          uint32_t pixel = colortable[si];
          Vec3b vec = PixelToVec3b(pixel);
          sortedQtableOutputMat.at<Vec3b>(i, 0) = vec;
          
          sortedColortable.push_back(pixel);
        }
        
        // Generate histogram based on the sorted quant pixels
        
        {
          unordered_map<uint32_t, uint32_t> pixelToQuantCountTable;
          
          generatePixelHistogram(tmpResultImg, pixelToQuantCountTable);
          
          for ( uint32_t pixel : sortedColortable ) {
            uint32_t count = pixelToQuantCountTable[pixel];
            uint32_t pixelNoAlpha = pixel & 0x00FFFFFF;
            fprintf(stdout, "0x%08X (%8d) -> %5d\n", pixelNoAlpha, pixelNoAlpha, count);
          }
          fprintf(stdout, "done\n");
        }
        
        if (debugDumpImages)
        {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_quant_table_sorted" << ".png";
          string filename = fnameStream.str();
          
          char *outQuantTableFilename = (char*) filename.c_str();
          imwrite(outQuantTableFilename, sortedQtableOutputMat);
          cout << "wrote " << outQuantTableFilename << endl;
        }
        
        // Map pixels to sorted colortable offset
        
        unordered_map<uint32_t, uint32_t> pixel_to_sorted_offset;
        
        assert(numActualClusters <= 256);
        
        for (int i = 0; i < numActualClusters; i++) {
          int si = (int) sortedOffsets[i];
          uint32_t pixel = colortable[si];
          pixel_to_sorted_offset[pixel] = si;
        }
        
        Mat sortedQuantOutputMat = inputImg.clone();
        sortedQuantOutputMat = Scalar(0,0,0xFF);
        
        for ( int i = 0; i < numPixels; i++ ) {
          Coord c = regionCoords[i];
          uint32_t pixel = outPixels[i];
          
          assert(pixel_to_sorted_offset.count(pixel) > 0);
          uint32_t offset = pixel_to_sorted_offset[pixel];
          
          if ((debug)) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "for (%4d,%4d) pixel is %d -> offset %d\n", c.x, c.y, pixel, offset);
            cout << buffer;
          }
          
          assert(offset <= 256);
          uint32_t grayscalePixel = (offset << 16) | (offset << 8) | offset;
          
          Vec3b vec = PixelToVec3b(grayscalePixel);
          sortedQuantOutputMat.at<Vec3b>(c.y, c.x) = vec;
        }
        
        if (debugDumpImages)
        {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_quant_table_offsets" << ".png";
          string filename = fnameStream.str();
          
          char *outQuantFilename = (char*)filename.c_str();
          imwrite(outQuantFilename, sortedQuantOutputMat);
          cout << "wrote " << outQuantFilename << endl;
        }
      }
      
      // Determine which cluster center is nearest to the peak pixels and use
      // that info to generate new cluster centers that are exactly at the
      // peak value. This means that the peak pixels will quant exactly and the
      // nearby cluster value will get the nearby but not exactly on pixels.
      // This should clearly separate the flat pixels from the gradient pixels.
      
      {
        unordered_map<uint32_t, uint32_t> pixelToQuantCountTable;
        
        for (int i = 0; i < numActualClusters; i++) {
          uint32_t pixel = colortable[i];
          pixel = pixel & 0x00FFFFFF;
          pixelToQuantCountTable[pixel] = i;
        }
        
        for ( uint32_t pixel : peakPixels ) {
          pixel = pixel & 0x00FFFFFF;
          pixelToQuantCountTable[pixel] = 0;
        }
        
        int numColors = (int)pixelToQuantCountTable.size();
        uint32_t *colortable = new uint32_t[numColors];
        
        {
          int i = 0;
          for ( auto &pair : pixelToQuantCountTable ) {
            uint32_t key = pair.first;
            assert(key == (key & 0x00FFFFFF)); // verify alpha is zero
            colortable[i] = key;
            i++;
          }
        }
        
        if (debugDumpImages)
        {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_quant_table2" << ".png";
          string fname = fnameStream.str();
          
          dumpQuantTableImage(fname, inputImg, colortable, numColors);
        }
        
        map_colors_mps(inPixels, numPixels, outPixels, colortable, numColors);
        
        // Dump quant output, each pixel is replaced by color in colortable
        
        tmpResultImg = Scalar(0,0,0xFF);
        
        for ( int i = 0; i < numPixels; i++ ) {
          Coord c = regionCoords[i];
          uint32_t pixel = outPixels[i];
          Vec3b vec;
          // vec = PixelToVec3b(pixel);
          if (pixel == 0x0) {
            vec = PixelToVec3b(pixel);
          } else {
            vec = PixelToVec3b(0xFFFFFFFF);
          }
          tmpResultImg.at<Vec3b>(c.y, c.x) = vec;
          
          if (pixel == 0x0) {
            // No-op when pixel is not on
          } else {
            outBlockMask.at<uint8_t>(c.y, c.x) = 0xFF;
          }
        }
        
        if (debugDumpImages)
        {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_quant_output2" << ".png";
          string fname = fnameStream.str();
          
          imwrite(fname, tmpResultImg);
          cout << "wrote " << fname << endl;
        }
        
      }
      
      // dealloc
      
      delete [] inPixels;
      delete [] outPixels;
      delete [] colortable;
      
    }
    
  }
  
  if (debug) {
    cout << "return captureRegionMask" << endl;
  }
  
  return true;
}


