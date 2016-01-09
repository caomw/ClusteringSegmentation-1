// A superpixel image is a matrix that contains N superpixels and N superpixel edges between superpixels.
// A superpixel image is typically parsed from a source of tags, modified, and then written as a new tags
// image.

#include "MergeSuperpixelImage.h"

#include "Superpixel.h"

#include "SuperpixelEdge.h"

#include "SuperpixelEdgeTable.h"

#include "Util.h"

#include "OpenCVUtil.h"

#include <iomanip>      // setprecision

#include "SuperpixelEdgeFuncs.h"

const int MaxSmallNumPixelsVal = 10;

void parse3DHistogram(Mat *histInputPtr,
                      Mat *histPtr,
                      Mat *backProjectInputPtr,
                      Mat *backProjectPtr,
                      int conversion,
                      int numBins);

bool pos_sample_within_bound(vector<float> &weights, float currentWeight);

void writeSuperpixelMergeMask(SuperpixelImage &spImage, Mat &resultImg, vector<int32_t> merges, vector<float> weights, unordered_map<int32_t, bool> *lockedTablePtr);

void generateStaticColortable(Mat &inputImg, SuperpixelImage &spImage);

// Compare method for CompareNeighborTuple type, in the case of a tie the second column
// is sorted in terms of decreasing int values.

static
bool CompareNeighborTupleFunc (CompareNeighborTuple &elem1, CompareNeighborTuple &elem2) {
  double hcmp1 = get<0>(elem1);
  double hcmp2 = get<0>(elem2);
  if (hcmp1 == hcmp2) {
    int numPixels1 = get<1>(elem1);
    int numPixels2 = get<1>(elem2);
    return (numPixels1 > numPixels2);
  }
  return (hcmp1 < hcmp2);
}

// Sort tuple (UNUSED, UID, SIZE) by decreasing SIZE values

static
bool CompareNeighborTupleSortByDecreasingLargestNumCoordsFunc (CompareNeighborTuple &elem1, CompareNeighborTuple &elem2) {
  int numPixels1 = get<2>(elem1);
  int numPixels2 = get<2>(elem2);
  return (numPixels1 > numPixels2);
}

// Sort into decreasing order in terms of the float value in the first element of the tuple.
// In the case of a tie then sort by decreasing superpixel size.

static
bool CompareNeighborTupleDecreasingFunc (CompareNeighborTuple &elem1, CompareNeighborTuple &elem2) {
  double hcmp1 = get<0>(elem1);
  double hcmp2 = get<0>(elem2);
  if (hcmp1 == hcmp2) {
    int numPixels1 = get<1>(elem1);
    int numPixels2 = get<1>(elem2);
    return (numPixels1 > numPixels2);
  }
  return (hcmp1 > hcmp2);
}

// This method is invoked with a superpixel tag to generate a vector of tuples that compares
// the superpixel to all of the neighbor superpixels.
//
// TUPLE (BHATTACHARYYA N_PIXELS NEIGHBOR_TAG)
// BHATTACHARYYA : double
// N_PIXELS      : int32_t
// NEIGHBOR_TAG  : int32_t

void
MergeSuperpixelImage::compareNeighborSuperpixels(Mat &inputImg,
                                            int32_t tag,
                                            vector<CompareNeighborTuple> &results,
                                            unordered_map<int32_t, bool> *lockedTablePtr,
                                            int32_t step) {
  const bool debug = false;
  const bool debugShowSorted = false;
  const bool debugDumpSuperpixels = false;
  
  Mat srcSuperpixelMat;
  Mat srcSuperpixelHist;
  Mat srcSuperpixelBackProjection;
  
  // Read RGB pixel data from main image into matrix for this one superpixel and then gen histogram.
  
  fillMatrixFromCoords(inputImg, tag, srcSuperpixelMat);
  
  parse3DHistogram(&srcSuperpixelMat, &srcSuperpixelHist, NULL, NULL, 0, -1);
  
  if (debugDumpSuperpixels) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "superpixel_" << tag << ".png";
    } else {
      stringStream << "superpixel_step_" << step << "_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelMat.cols << " x " << srcSuperpixelMat.rows << " )" << endl;
    imwrite(filename, srcSuperpixelMat);
  }
  
  if (!results.empty()) {
    results.erase (results.begin(), results.end());
  }
  
  for ( int32_t neighborTag : edgeTable.getNeighborsSet(tag) ) {
    // Generate histogram for the neighbor and then compare to neighbor
    
    if (lockedTablePtr && (lockedTablePtr->count(neighborTag) != 0)) {
      // If a locked down table is provided then do not consider a neighbor that appears
      // in the locked table.
      
      if (debug) {
        cout << "skipping consideration of locked neighbor " << neighborTag << endl;
      }
      
      continue;
    }
    
    Mat neighborSuperpixelMat;
    Mat neighborSuperpixelHist;
    Mat neighborBackProjection;
    
    fillMatrixFromCoords(inputImg, neighborTag, neighborSuperpixelMat);
    
    parse3DHistogram(&neighborSuperpixelMat, &neighborSuperpixelHist, NULL, NULL, 0, -1);
    
    if (debugDumpSuperpixels) {
      std::ostringstream stringStream;
      stringStream << "superpixel_" << neighborTag << ".png";
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      cout << "write " << filename << " ( " << neighborSuperpixelMat.cols << " x " << neighborSuperpixelMat.rows << " )" << endl;
      imwrite(filename, neighborSuperpixelMat);
    }
    
    assert(srcSuperpixelHist.dims == neighborSuperpixelHist.dims);
    
    double compar_bh = cv::compareHist(srcSuperpixelHist, neighborSuperpixelHist, CV_COMP_BHATTACHARYYA);
    
    if (debug) {
    cout << "BHATTACHARYYA " << compar_bh << endl;
    }
    
    CompareNeighborTuple tuple = make_tuple(compar_bh, neighborSuperpixelMat.cols, neighborTag);
    
    results.push_back(tuple);
  }
  
  if (debug) {
    cout << "unsorted tuples from src superpixel " << tag << endl;
    
    for (auto it = results.begin(); it != results.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
               get<0>(tuple), get<1>(tuple), get<2>(tuple));
      cout << (char*)buffer << endl;
    }
  }
  
  // Sort tuples by BHATTACHARYYA value
  
  if (results.size() > 1) {
    sort(results.begin(), results.end(), CompareNeighborTupleFunc);
  }
  
  if (debug || debugShowSorted) {
    cout << "sorted tuples from src superpixel " << tag << endl;

    for (auto it = results.begin(); it != results.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
               get<0>(tuple), get<1>(tuple), get<2>(tuple));
      cout << (char*)buffer << endl;
    }
  }
  
  return;
}

// This method is invoked to do a histogram based backprojection to return alikeness
// info about the neighbors of the superpixel. This method uses a histogram based compare
// to return a list sorted by decreasing normalized value determined by averaging the
// backprojection percentages for each pixel.
//
// inputImg : image pixels to read from (assumed to be BGR)
// tag      : superpixel tag that neighbors will be looked up from
// results  : list of neighbors that fit the accept percentage
// locked   : table of superpixel tags already locked
// step     : count of number of steps used for debug output
// conversion: colorspace to convert to before hist calculations
// numPercentRanges : N to indicate the uniform breakdown of
//          percentage values. For example, if numPercentRanges=20
//          then the fill range of 0.0 -> 1.0 is treated as 20
//          ranges covering 5% prob each.
// numTopPercent: number of percentage slots that are acceptable.
//          For 20 ranges and 2 slots, each slot covers 5% so the
//          total acceptable range is then 10%.
// minGraylevel: A percentage value must be GTEQ this grayscale
//          prob value to be considered.
//
// Return tuples : (PERCENT NUM_COORDS TAG)

void
MergeSuperpixelImage::backprojectNeighborSuperpixels(SuperpixelImage &spImage,
                                                Mat &inputImg,
                                                int32_t tag,
                                                vector<CompareNeighborTuple> &results,
                                                unordered_map<int32_t, bool> *lockedTablePtr,
                                                int32_t step,
                                                int conversion,
                                                int numPercentRanges,
                                                int numTopPercent,
                                                bool roundPercent,
                                                int minGraylevel,
                                                int numBins)
{
  const bool debug = false;
  const bool debugDumpSuperpixels = false;
  const bool debugShowSorted = false;

  const bool debugDumpAllBackProjection = false;
  
  const bool debugDumpCombinedBackProjection = false;
  
  assert(lockedTablePtr);
  
  if (!results.empty()) {
    results.erase (results.begin(), results.end());
  }
  
  // Before parsing histogram and emitting intermediate images check for the case where a superpixel
  // has all locked neighbors and return early without doing anything in this case. The locked table
  // check is very fast and the number of histogram parses avoided is large.
  
  bool allNeighborsLocked = true;
  
  for ( int32_t neighborTag : spImage.edgeTable.getNeighborsSet(tag) ) {
    if (lockedTablePtr->count(neighborTag) != 0) {
      // Neighbor is locked
    } else {
      // Neighbor is not locked
      allNeighborsLocked = false;
      break;
    }
  }
  
  if (allNeighborsLocked) {
    if (debug) {
      cout << "early return from backprojectNeighborSuperpixels since all neighbors are locked" << endl;
    }
    
    return;
  }
  
  Mat srcSuperpixelMat;
  Mat srcSuperpixelHist;
  Mat srcSuperpixelBackProjection;
  
  // Read RGB pixels for the largest superpixel identified by tag from the input image.
  // Gen histogram and then create a back projected output image that shows the percentage
  // values for each pixel in the connected neighbors.
  
  spImage.fillMatrixFromCoords(inputImg, tag, srcSuperpixelMat);
  
  if (debugDumpAllBackProjection == true) {
    // Create histogram and generate back projection for entire image
    parse3DHistogram(&srcSuperpixelMat, &srcSuperpixelHist, &inputImg, &srcSuperpixelBackProjection, conversion, numBins);
  } else {
    // Create histogram but do not generate back projection for entire image
    parse3DHistogram(&srcSuperpixelMat, &srcSuperpixelHist, NULL, NULL, conversion, numBins);
  }
  
  if (debugDumpSuperpixels) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "superpixel_" << tag << ".png";
    } else {
      stringStream << "superpixel_step_" << step << "_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelMat.cols << " x " << srcSuperpixelMat.rows << " )" << endl;
    imwrite(filename, srcSuperpixelMat);
  }
  
  if (debugDumpAllBackProjection) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "backproject_from" << tag << ".png";
    } else {
      stringStream << "backproject_step_" << step << "_from_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelBackProjection.cols << " x " << srcSuperpixelBackProjection.rows << " )" << endl;
    
    imwrite(filename, srcSuperpixelBackProjection);
  }
  
  if (debugDumpCombinedBackProjection) {
    // Use this image to fill in all back projected values for neighbors
    
    Scalar bg = Scalar(255,0,0); // Blue
    
    Mat origSize(inputImg.size(), CV_8UC(3), bg);
    srcSuperpixelBackProjection = origSize;
    
    Mat srcSuperpixelGreen = srcSuperpixelMat;
    srcSuperpixelGreen = Scalar(0,255,0);
    
    spImage.reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, srcSuperpixelBackProjection);
  }
  
  for ( int32_t neighborTag : spImage.edgeTable.getNeighborsSet(tag) ) {
    // Do back projection on neighbor pixels using histogram from biggest superpixel
    
    if (lockedTablePtr && (lockedTablePtr->count(neighborTag) != 0)) {
      // If a locked down table is provided then do not consider a neighbor that appears
      // in the locked table.
      
      if (debug) {
        cout << "skipping consideration of locked neighbor " << neighborTag << endl;
      }
      
      continue;
    }
    
    Mat neighborSuperpixelMat;
    //Mat neighborSuperpixelHist;
    Mat neighborBackProjection;
    
    // Back project using the 3D histogram parsed from the largest superpixel only
    
    spImage.fillMatrixFromCoords(inputImg, neighborTag, neighborSuperpixelMat);
      
    parse3DHistogram(NULL, &srcSuperpixelHist, &neighborSuperpixelMat, &neighborBackProjection, conversion, numBins);
    
    if (debugDumpSuperpixels) {
      std::ostringstream stringStream;
      stringStream << "superpixel_" << neighborTag << ".png";
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      cout << "write " << filename << " ( " << neighborSuperpixelMat.cols << " x " << neighborSuperpixelMat.rows << " )" << endl;
      imwrite(filename, neighborSuperpixelMat);
    }
    
    if (debugDumpAllBackProjection) {
      // BackProject prediction for just the pixels in the neighbor as compared to the src. Pass
      // the input image generated from the neighbor superpixel and then recreate the original
      // pixel layout by writing the pixels back to the output image in the same order.
      
      std::ostringstream stringStream;
      if (step == -1) {
        stringStream << "backproject_neighbor_" << neighborTag << "_from" << tag << ".png";
      } else {
        stringStream << "backproject_step_" << step << "_neighbor_" << neighborTag << "_from_" << tag << ".png";
      }
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      // The back projected input is normalized grayscale as a float value
      
      //cout << "neighborBackProjection:" << endl << neighborBackProjection << endl;
      
      Mat neighborBackProjectionGrayOrigSize(inputImg.size(), CV_8UC(3), (Scalar)0);
      
      spImage.reverseFillMatrixFromCoords(neighborBackProjection, true, neighborTag, neighborBackProjectionGrayOrigSize);
      
      cout << "write " << filename << " ( " << neighborBackProjectionGrayOrigSize.cols << " x " << neighborBackProjectionGrayOrigSize.rows << " )" << endl;
      
      imwrite(filename, neighborBackProjectionGrayOrigSize);
    }
    
    if (debugDumpCombinedBackProjection) {
      // Write combined back projection values to combined image.

      spImage.reverseFillMatrixFromCoords(neighborBackProjection, true, neighborTag, srcSuperpixelBackProjection);
    }
    
    // Threshold the neighbor pixels and then choose a path for expansion that considers all the neighbors
    // via a fill. Any value larger than 200 becomes 255 while any value below becomes zero
    
    /*
    
    threshold(neighborBackProjection, neighborBackProjection, 200.0, 255.0, THRESH_BINARY);
    
    if (debugDumpBackProjection) {
    
      std::ostringstream stringStream;
      if (step == -1) {
        stringStream << "backproject_threshold_neighbor_" << neighborTag << "_from" << tag << ".png";
      } else {
        stringStream << "backproject_threshold_step_" << step << "_neighbor_" << neighborTag << "_from_" << tag << ".png";
      }
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      cout << "write " << filename << " ( " << neighborBackProjection.cols << " x " << neighborBackProjection.rows << " )" << endl;
    
      imwrite(filename, neighborBackProjection);
    }
    
    */
     
    // If more than 95% of the back projection threshold values are on then treat this neighbor superpixel
    // as one that should be merged in this expansion step.
    
    if (1) {
      //const int minGraylevel = 200;
      //const float minPercent = 0.95f;
      
      float oneRange = (1.0f / numPercentRanges);
      float minPercent = 1.0f - (oneRange * numTopPercent);
      
      int count = 0;
      int N = neighborBackProjection.cols;
      
      assert(neighborBackProjection.rows == 1);
      for (int i = 0; i < N; i++) {
        uint8_t gray = neighborBackProjection.at<uchar>(0, i);
        if (gray >= minGraylevel) {
          count += 1;
        }
      }
      
      float per = ((double)count) / N;
      
      if (debug) {
        cout << setprecision(3); // 3.141
        cout << showpoint;
        cout << setw(10);
        
        cout << "for neighbor " << neighborTag << " found " << count << " non-zero out of " << N << " pixels : per " << per << endl;
      }
      
      if (per >= minPercent) {
        if (debug) {
          cout << "added neighbor to merge list" << endl;
        }
        
        // If roundPercent is true then round the percentage in terms of the width of percentage range.
        
        if (roundPercent) {
          float rounded = round(per / oneRange) * oneRange;
          
          if (debug) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "rounded per %0.4f to %0.4f", per, rounded);
            cout << (char*)buffer << endl;
          }
          
          per = rounded;
        }
        
        CompareNeighborTuple tuple(per, N, neighborTag);
        
        results.push_back(tuple);
      }
    }
    
  } // end neighbors loop
  
  if (debug) {
    cout << "unsorted tuples (N = " << results.size() << ") from src superpixel " << tag << endl;
    
    for (auto it = results.begin(); it != results.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
               get<0>(tuple), get<1>(tuple), get<2>(tuple));
      cout << (char*)buffer << endl;
    }
  }
  
  // Sort tuples by percent value

  if (results.size() > 1) {
    sort(results.begin(), results.end(), CompareNeighborTupleDecreasingFunc);
  }
  
  if (debug || debugShowSorted) {
    cout << "sorted tuples (N = " << results.size() << ") from src superpixel " << tag << endl;
    
    for (auto it = results.begin(); it != results.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
               get<0>(tuple), get<1>(tuple), get<2>(tuple));
      cout << (char*)buffer << endl;
    }
  }
  
  if (debugDumpCombinedBackProjection) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "backproject_combined_from" << tag << ".png";
    } else {
      stringStream << "backproject_combined_step_" << step << "_from_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelBackProjection.cols << " x " << srcSuperpixelBackProjection.rows << " )" << endl;
    
    imwrite(filename, srcSuperpixelBackProjection);
  }
  
  return;
}

// This method will back project from a src superpixel and find all neighbor superpixels that contain
// non-zero back projection values. This back projection is like a flood fill except that it operates
// on histogram percentages. This method returns a list of superpixel ids gathered.

void
MergeSuperpixelImage::backprojectDepthFirstRecurseIntoNeighbors(Mat &inputImg,
                                                           int32_t tag,
                                                           vector<int32_t> &results,
                                                           unordered_map<int32_t, bool> *lockedTablePtr,
                                                           int32_t step,
                                                           int conversion,
                                                           int numPercentRanges,
                                                           int numTopPercent,
                                                           int minGraylevel,
                                                           int numBins)
{
  const bool debug = false;
  const bool debugDumpSuperpixels = false;
  
  const bool debugDumpAllBackProjection = false;
  
  const bool debugDumpCombinedBackProjection = false;
  
  assert(lockedTablePtr);
  
  if (!results.empty()) {
    results.erase (results.begin(), results.end());
  }
  
  // Before parsing histogram and emitting intermediate images check for the case where a superpixel
  // has all locked neighbors and return early without doing anything in this case. The locked table
  // check is very fast and the number of histogram parses avoided is large.
  
  bool allNeighborsLocked = true;
  
  for ( int32_t neighborTag : edgeTable.getNeighborsSet(tag) ) {
    if (lockedTablePtr->count(neighborTag) != 0) {
      // Neighbor is locked
    } else {
      // Neighbor is not locked
      allNeighborsLocked = false;
      break;
    }
  }
  
  if (allNeighborsLocked) {
    if (debug) {
      cout << "early return from backprojectDepthFirstRecurseIntoNeighbors since all neighbors are locked" << endl;
    }
    
    return;
  }
  
  Mat srcSuperpixelMat;
  Mat srcSuperpixelHist;
  Mat srcSuperpixelBackProjection;
  
  // Read RGB pixels for the largest superpixel identified by tag from the input image.
  // Gen histogram and then create a back projected output image that shows the percentage
  // values for each pixel in the connected neighbors.
  
  fillMatrixFromCoords(inputImg, tag, srcSuperpixelMat);
  
  if (debugDumpAllBackProjection == true) {
    // Create histogram and generate back projection for entire image
    parse3DHistogram(&srcSuperpixelMat, &srcSuperpixelHist, &inputImg, &srcSuperpixelBackProjection, conversion, numBins);
  } else {
    // Create histogram but do not generate back projection for entire image
    parse3DHistogram(&srcSuperpixelMat, &srcSuperpixelHist, NULL, NULL, conversion, numBins);
  }
  
  if (debugDumpSuperpixels) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "superpixel_" << tag << ".png";
    } else {
      stringStream << "superpixel_step_" << step << "_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelMat.cols << " x " << srcSuperpixelMat.rows << " )" << endl;
    imwrite(filename, srcSuperpixelMat);
  }
  
  if (debugDumpAllBackProjection) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "backproject_from" << tag << ".png";
    } else {
      stringStream << "backproject_step_" << step << "_from_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelBackProjection.cols << " x " << srcSuperpixelBackProjection.rows << " )" << endl;
    
    imwrite(filename, srcSuperpixelBackProjection);
  }
  
  if (debugDumpCombinedBackProjection) {
    // Use this image to fill in all back projected values for neighbors
    
    Scalar bg = Scalar(255,0,0); // Blue
    
    Mat origSize(inputImg.size(), CV_8UC(3), bg);
    srcSuperpixelBackProjection = origSize;
    
    Mat srcSuperpixelGreen = srcSuperpixelMat;
    srcSuperpixelGreen = Scalar(0,255,0);
    
    reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, srcSuperpixelBackProjection);
  }

  // Table of superpixels already seen via DFS as compared to src superpixel.
  
  unordered_map<int32_t, bool> seenTable;
  
  seenTable[tag] = true;
  
  // Fill queue with initial neighbors of this superpixel
  
  vector<int32_t> queue;
  
  for ( int32_t neighborTag : edgeTable.getNeighborsSet(tag) ) {
    queue.push_back(neighborTag);
    seenTable[neighborTag] = true;
  }
  
  // This foreach logic must descend into neighbors and then neighbors of neighbors until the backprojection returns
  // zero for all pixels.
  
  for (; 1 ;) {
    if (debug) {
      cout << "pop off front of queue with " << queue.size() << " elements" << endl;
    }
    
    int sizeNow = (int) queue.size();
    if (sizeNow == 0) {
      if (debug) {
        cout << "queue empty, done DFS iteration" << endl;
      }

      break;
    }
    
    if (debug && 0) {
      cout << "queue:" << endl;
      
      for (auto it = queue.begin(); it != queue.end(); ++it) {
        int32_t neighborTag = *it;
        cout << neighborTag << endl;
      }
    }
    
    // Pop first element off queue
    
    int32_t neighborTag = queue[sizeNow-1];
    queue.erase(queue.end()-1);
    
#if defined(DEBUG)
    int sizeAfterPop = (int) queue.size();
    assert(sizeNow == sizeAfterPop+1 );
#endif // DEBUG

    if (debug) {
      cout << "popped neighbor tag " << neighborTag << endl;
    }
    
    if (lockedTablePtr->count(neighborTag) != 0) {
      // If a locked down table is provided then do not consider a neighbor that appears
      // in the locked table.
      
      if (debug) {
        cout << "skipping consideration of locked neighbor " << neighborTag << endl;
      }
      
      continue;
    }
    
    Mat neighborSuperpixelMat;
    //Mat neighborSuperpixelHist;
    Mat neighborBackProjection;
    
    // Back project using the 3D histogram parsed from the largest superpixel only
    
    fillMatrixFromCoords(inputImg, neighborTag, neighborSuperpixelMat);
    
    parse3DHistogram(NULL, &srcSuperpixelHist, &neighborSuperpixelMat, &neighborBackProjection, conversion, numBins);
    
    if (debugDumpSuperpixels) {
      std::ostringstream stringStream;
      stringStream << "superpixel_" << neighborTag << ".png";
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      cout << "write " << filename << " ( " << neighborSuperpixelMat.cols << " x " << neighborSuperpixelMat.rows << " )" << endl;
      imwrite(filename, neighborSuperpixelMat);
    }
    
    if (debugDumpAllBackProjection) {
      // BackProject prediction for just the pixels in the neighbor as compared to the src. Pass
      // the input image generated from the neighbor superpixel and then recreate the original
      // pixel layout by writing the pixels back to the output image in the same order.
      
      std::ostringstream stringStream;
      if (step == -1) {
        stringStream << "backproject_neighbor_" << neighborTag << "_from" << tag << ".png";
      } else {
        stringStream << "backproject_step_" << step << "_neighbor_" << neighborTag << "_from_" << tag << ".png";
      }
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      // The back projected input is normalized grayscale as a float value
      
      //cout << "neighborBackProjection:" << endl << neighborBackProjection << endl;
      
      Mat neighborBackProjectionGrayOrigSize(inputImg.size(), CV_8UC(3), (Scalar)0);
      
      reverseFillMatrixFromCoords(neighborBackProjection, true, neighborTag, neighborBackProjectionGrayOrigSize);
      
      cout << "write " << filename << " ( " << neighborBackProjectionGrayOrigSize.cols << " x " << neighborBackProjectionGrayOrigSize.rows << " )" << endl;
      
      imwrite(filename, neighborBackProjectionGrayOrigSize);
    }
    
    if (debugDumpCombinedBackProjection) {
      // Write combined back projection values to combined image.
      
      reverseFillMatrixFromCoords(neighborBackProjection, true, neighborTag, srcSuperpixelBackProjection);
    }
        
    // If more than 95% of the back projection threshold values are on then treat this neighbor superpixel
    // as one that should be merged in this expansion step.
    
    if (1) {
      float oneRange = (1.0f / numPercentRanges);
      float minPercent = 1.0f - (oneRange * numTopPercent);
      
      int count = 0;
      int N = neighborBackProjection.cols;
      
      assert(neighborBackProjection.rows == 1);
      for (int i = 0; i < N; i++) {
        uint8_t gray = neighborBackProjection.at<uchar>(0, i);
        if (gray > minGraylevel) {
          count += 1;
        }
      }
      
      float per = ((double)count) / N;
      
      if (debug) {
        cout << setprecision(3); // 3.141
        cout << showpoint;
        cout << setw(10);
        
        cout << "for neighbor " << neighborTag << " found " << count << " above min graylevel out of " << N << " pixels : per " << per << endl;
      }
      
      if (per > minPercent) {
        if (debug) {
          cout << "added neighbor to merge list" << endl;
        }
        
        results.push_back(neighborTag);
        
        // Iterate over all neighbors of this neighbor and insert at the front of the queue
        
        if (debug) {
          cout << "cheking " << edgeTable.getNeighborsSet(neighborTag).size()  << " possible neighbors for addition to DFS queue" << endl;
        }
        
        for ( int32_t neighborTag : edgeTable.getNeighborsSet(neighborTag) ) {
          if (seenTable.count(neighborTag) == 0) {
            seenTable[neighborTag] = true;
            
            if (debug) {
              for (auto it = queue.begin(); it != queue.end(); ++it) {
                int32_t existingTag = *it;
                if (existingTag == neighborTag) {
                  assert(0);
                }
              }
            }
            
            queue.push_back(neighborTag);
            
            if (debug) {
              cout << "added unseen neighbor " << neighborTag << endl;
            }
          }
        }
      }
      
      // If this neighbor passed the threshold test then emit an image that shows the
      // backprojected prop grayscale over a blue background so that black can still
      // be seen.
      
      if (debugDumpCombinedBackProjection /*&& (per > minPercent)*/) {
        Scalar bg = Scalar(255,0,0); // Blue
        
        Mat dfsBack(inputImg.size(), CV_8UC(3), bg);
        
        Mat srcSuperpixelGreen = srcSuperpixelMat;
        srcSuperpixelGreen = Scalar(0,255,0);
        
        reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, dfsBack);
        reverseFillMatrixFromCoords(neighborBackProjection, true, neighborTag, dfsBack);
        
        std::ostringstream stringStream;
        if (step == -1) {
          stringStream << "backproject_dfs_thresh_neighbor_" << neighborTag << "_from_" << tag << ".png";
        } else {
          stringStream << "backproject_dfs_thresh_combined_step_" << step << "_neighbor_" << neighborTag << "_from_" << tag << ".png";
        }
        std::string str = stringStream.str();
        const char *filename = str.c_str();
        
        cout << "write " << filename << " ( " << dfsBack.cols << " x " << dfsBack.rows << " )" << endl;
        
        imwrite(filename, dfsBack);
      }
      
    }
    
  } // end queue not empty loop
  
  if (debugDumpCombinedBackProjection) {
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "backproject_combined_from" << tag << ".png";
    } else {
      stringStream << "backproject_combined_step_" << step << "_from_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelBackProjection.cols << " x " << srcSuperpixelBackProjection.rows << " )" << endl;
    
    imwrite(filename, srcSuperpixelBackProjection);
  }
  
  if (debugDumpCombinedBackProjection) {
    // Emit an image that shows the src superpixel as green, all from DFS as red, and unvisited as blue
    
    Scalar bg = Scalar(255,0,0); // Blue
    
    Mat dfsScope(inputImg.size(), CV_8UC(3), bg);
    
    Mat srcSuperpixelGreen = srcSuperpixelMat;
    srcSuperpixelGreen = Scalar(0,255,0);
    
    reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, dfsScope);
    
    // Iterate over each indicated neighbor to indicate scope

    for (auto it = results.begin(); it != results.end(); ++it) {
      int32_t resultTag = *it;
      
      Mat resultsSuperpixelMat;
      
      fillMatrixFromCoords(inputImg, resultTag, resultsSuperpixelMat);
      
      // Fill with Red
      resultsSuperpixelMat = Scalar(0,0,255);
      
      reverseFillMatrixFromCoords(resultsSuperpixelMat, false, resultTag, dfsScope);
    }
    
    std::ostringstream stringStream;
    if (step == -1) {
      stringStream << "backproject_dfs_scope_from" << tag << ".png";
    } else {
      stringStream << "backproject_dfs_scope_combined_step_" << step << "_from_" << tag << ".png";
    }
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    cout << "write " << filename << " ( " << srcSuperpixelBackProjection.cols << " x " << srcSuperpixelBackProjection.rows << " )" << endl;
    
    imwrite(filename, dfsScope);
    
  }
  
  return;
}

// Repeated merge of the largest superpixels up until the
// easily merged superpixels have been merged.

void MergeSuperpixelImage::mergeAlikeSuperpixels(Mat &inputImg)
{
  const bool debug = false;
  const bool dumpEachMergeStepImage = false;
  
  // Each iteration will examine the list of superpixels and pick the biggest one
  // that is not already locked. Then that superpixel will be expanded until
  // an edge is encountered. Note that the superpixels list can be modified
  // by a merge, so iterate to find the largest one and then use that value.
  
  bool allLocked = false;
  int mergeIter = 0;
  
  unordered_map<int32_t, bool> locked;
  unordered_map<int32_t, vector<float>> histWeights;
  
  while (!allLocked) {
    
    int maxThisIter = -1;
    int numChecked = 0;
    int32_t maxTag = -1;
  
    for (auto it = superpixels.begin(); it != superpixels.end(); ++it) {
      int32_t tag = *it;
     
      Superpixel *spPtr = getSuperpixelPtr(tag);
      
      int numCoords = (int) spPtr->coords.size();
      
      if (numCoords > maxThisIter && (locked.count(tag) == 0)) {
        maxThisIter = numCoords;
        maxTag = tag;
      }
      
      numChecked++;
    }
    
    if (maxTag == -1) {
      if (debug) {
        cout << "checked " << numChecked << " superpixels but all were locked" << endl;
      }
      
      allLocked = true;
      continue;
    }
    
    if (debug) {
      cout << "checked " << numChecked << " superpixels and found largest superpixel " << maxTag << " with N=" << maxThisIter << " pixels" << endl;
    }
    
    // Since this superpixel is the largest one currently, merging with another superpixel will always increase the size
    // of this one. The locked table depends on keeping track of the locked superpixel by tag id, so this logic makes
    // sure that the largest superpixel is not absorbed into another one up until it is locked.
    
    /*
    if (maxThisIter <= 20) {
      allLocked = true;
      continue;
    }
    */
    
    while ((locked.count(maxTag) == 0)){
      if (debug) {
        cout << "start iter step " << mergeIter << endl;
      }
      
      vector<CompareNeighborTuple> results;
      
      compareNeighborSuperpixels(inputImg, maxTag, results, &locked, mergeIter);
      
      // Get the neighbor with the min hist compare level, in case of a tie
      // sort by the number of pixels in the superpixel so that the largest
      // neighbor is always selected. Note that because neighbors could be
      // locked we need to check that the set of neighbors is not N=0.
      
      if (results.size() == 0) {
        if (debug) {
          cout << "no unlocked neighbors so marking this superpixel as locked also" << endl;
        }
        
        locked[maxTag] = true;
        break;
      }
      
      if (dumpEachMergeStepImage) {
        // Emit a mask image that shows Green for the superpixel being merged and a grayscale
        // level for neighbor superpixels being considered as merges. The default background
        // is black and the grey levels are inverted so that the most white superpixel is
        // the best merge.
        
        Mat resultImg = inputImg.clone();
        resultImg = Scalar(255, 0, 0); // Init unwritten pixels to Blue
        
        vector<int32_t> merges;
        vector<float> weights;
        
        merges.push_back(maxTag);
        weights.push_back(0.0f);
        
        for (auto it = results.begin(); it != results.end(); ++it) {
          CompareNeighborTuple minTuple = *it;
          float   minWeight   = (float) get<0>(minTuple); // Get BHATTACHARYYA
          int32_t minNeighbor = get<2>(minTuple); // Get NEIGHBOR_TAG
          
          merges.push_back(minNeighbor);
          weights.push_back(minWeight);
        }
        
        writeSuperpixelMergeMask(*this, resultImg, merges, weights, &locked);
        
        std::ostringstream stringStream;
        stringStream << "merge_mask_step_" << mergeIter << ".png";
        std::string str = stringStream.str();
        const char *filename = str.c_str();
        
        imwrite(filename, resultImg);
        
        cout << "wrote " << filename << endl;
      }
      
      CompareNeighborTuple minTuple = results[0];
      
      float   minWeight   = (float) get<0>(minTuple); // Get BHATTACHARYYA
      int32_t minNeighbor = get<2>(minTuple); // Get NEIGHBOR_TAG
      
      if (debug) {
      cout << "for superpixel " << maxTag << " min neighbor is " << minNeighbor << " with hist weight " << minWeight << endl;
      }
      
      // Note that a weight of 0.0 is not appended to the weight tables since it is the min and we really
      // only care about positive deltas when it comes to calculating a good bound for the next positive delta.
      
      // FIXME: what about weights from the superpixel to be merged? Would these previous weight values improve
      // the list of weights for the current superpixel ?

      vector<float> weights = histWeights[maxTag];
      
      if (minWeight > 0.0) {
      } else {
        if (debug) {
          cout << "ignored zero weight" << endl;
        }
      }
      
      // If the new weight is within the merge range then do an edge merge. Otherwise, lock the
      // largest superpixel and go back to the search loop to find the next largest unlocked superpixel.
      
      bool mergeThisEdge = pos_sample_within_bound(weights, minWeight);
      
      if (mergeThisEdge) {
        if (minWeight != 0.0f) {
          weights.push_back(minWeight);
          histWeights[maxTag] = weights;
        }
        
        SuperpixelEdge edge(maxTag, minNeighbor);
        
        if (debug) {
          cout << "will merge edge (" << edge.A << " " << edge.B << ")" << endl;
        }
        
        mergeEdge(edge);
        mergeIter += 1;
        
#if defined(DEBUG)
        // This must never fail since the merge should always consume the other superpixel
        assert(getSuperpixelPtr(maxTag) != NULL);
#endif // DEBUG
        
        if (dumpEachMergeStepImage) {
          Mat resultImg = inputImg.clone();
          resultImg = (Scalar) 0;
          
          writeTagsWithStaticColortable(*this, resultImg);
          
          std::ostringstream stringStream;
          stringStream << "merge_step_" << mergeIter << ".png";
          std::string str = stringStream.str();
          const char *filename = str.c_str();
          
          imwrite(filename, resultImg);
          
          cout << "wrote " << filename << endl;
        }
      } else {
        // Do with merges for this superpixel, mark it locked at this point.
        
        if (debug) {
          cout << "done merging edges with final weight list" << endl;
          
          for (auto it = weights.begin(); it != weights.end(); ++it) {
            float weight = *it;
            cout << weight << endl;
          }
          
          cout << "locked " << maxTag << endl;
        }
        
        // in merge iter so that merge number does not repeat (okay to skip on in the lock case)
        mergeIter += 1;
        
        locked[maxTag] = true;
      }
    } // end of while ! locked loop
    
  } // end of while (!allLocked) loop
  
  if (debug) {
    cout << "left allLocked loop with " << superpixels.size() << " merged superpixels" << endl;
  }
  
  return;
}

// Bredth first merge approach where the largest superpixel merges the next N neighbor
// superpixels that are of equal sameness as determined by a backproject fill on the
// immediate neighbors. Note that this method uses a threshold so that only superpixel
// neighbors that are very much alike will be merged. Also, this approach can be run
// on even very small sized superpixels since a merge will only happen when the neighbor
// is very much alike, so a merge of an oversegmented area will only combine the very
// alike portions.

int MergeSuperpixelImage::mergeBackprojectSuperpixels(SuperpixelImage &spImage, Mat &inputImg, int colorspace, int startStep, BackprojectRange range)
{
  const bool debug = true;
  const bool dumpEachMergeStepImage = false;
  
  // Each iteration will examine the list of superpixels and pick the biggest one
  // that is not already locked. Then that superpixel will be expanded until
  // an edge is encountered. Note that the superpixels list can be modified
  // by a merge, so iterate to find the largest one and then use that value.
  // The main loop will iterate until all superpixels are locked and then
  // the locks will be cleared and the iteration will start again using the
  // now larger superpixels. This will continue to merge
  
  bool done = false;
  int mergeIter = startStep;
  
  int numLockClear = 0;
  unordered_map<int32_t, bool> mergesSinceLockClear;
  
  unordered_map<int32_t, bool> locked;
  
  // Do initial sort of the superpixels list so that superpixels are ordered by
  // the number of coordinates in the superpixel. While the sort takes time
  // it means that looking for the largest superpixel can be done by simply
  // finding the next superpixel since superpixels will always be processed
  // from largest to smallest.
  
  vector<int32_t> sortedSuperpixels = spImage.sortSuperpixelsBySize();
  
  auto spIter = sortedSuperpixels.begin();
  int32_t maxTag = -1;
  
  while (!done) {
    // Get the next superpixel, it will be LTEQ the size of the current superpixel since
    // the superpixels list was sorted and then only deletes would happen via a merge.

#if defined(DEBUG)
    if (spIter != sortedSuperpixels.begin() && spIter != sortedSuperpixels.end()) {
      spIter--;
      int prevTag = *spIter;
      spIter++;
      assert(maxTag == prevTag);
    }
#endif // DEBUG
    
    if (spIter == sortedSuperpixels.end()) {
      // At end of superpixels list
      maxTag = -1;
    } else {
      // Find next unlocked superpixel

      maxTag = -1;
      while (spIter != sortedSuperpixels.end()) {
        int32_t nextTag = *spIter;
        
        Superpixel *spPtr = spImage.getSuperpixelPtr(nextTag);
        
        if (spPtr == NULL) {
          locked[nextTag] = true;
        }
        
        if (debug) {
          if (spPtr != NULL) {
            int numCoords = (int) spPtr->coords.size();
            cout << "next max superpixel " << nextTag << " N = " << numCoords << endl;
          }
        }
        
        spIter++;
        
        if (locked[nextTag]) {
          if (debug) {
            cout << "next max superpixel locked" << endl;
          }
        } else {
          // Not locked, use it now
          maxTag = nextTag;
          break;
        }
      }
      
#if defined(DEBUG)
      if (maxTag != -1) {
        bool isLocked = locked[maxTag];
        assert(isLocked == false);
      }
#endif // DEBUG
    }
    
    if (maxTag == -1) {
      if (debug) {
        cout << "checked superpixels but all were locked" << endl;
      }
      
      if (debug) {
        cout << "found that all superpixels are locked with " << sortedSuperpixels.size() << " superpixels" << endl;
        cout << "mergesSinceLockClear.size() " << mergesSinceLockClear.size() << " numLockClear " << numLockClear << endl;
      }
      
      if (mergesSinceLockClear.size() == 0) {
        done = true;
        continue;
      }
      
      // Delete lock only for superpixels that were expanded in a previous merge run. This avoids
      // having to recheck all superpixels that did not merge the first time while still checking
      // the superpixels that were expanded and could be ready to merge now.
      
      for (auto it = mergesSinceLockClear.begin(); it != mergesSinceLockClear.end(); ++it) {
        int32_t merged = it->first;

        if (locked.count(merged) == 0) {
          if (debug) {
            cout << "expanded superpixel has no lock entry to erase (it was merged into another superpixel) " << merged << endl;
          }
        } else {
          if (debug) {
            int sizeBefore = (int) locked.size();
            cout << "erase expanded superpixel lock " << merged << endl;
            locked.erase(merged);
            int sizeAfter = (int) locked.size();
            assert(sizeBefore == sizeAfter+1);
          } else {
            locked.erase(merged);
          }
        }
      }
      
      mergesSinceLockClear.clear();
      sortedSuperpixels = spImage.sortSuperpixelsBySize();
      spIter = sortedSuperpixels.begin();
      numLockClear++;
      continue;
    }
    
    if (debug) {
      Superpixel *spPtr = spImage.getSuperpixelPtr(maxTag);
      int numCoords = (int) spPtr->coords.size();
      cout << "found largest superpixel " << maxTag << " with N=" << numCoords << " pixels" << endl;
    }
    
    // Since this superpixel is the largest one currently, merging with another superpixel will always increase the size
    // of this one. The locked table by id logic depends on being able to track a stable UID applied to one specific
    // superpixel, so this approach of using the largest superpixel means that smaller superpixel will always be merged
    // into the current largest superpixel.
    
    while (true) {
      if (debug) {
        cout << "start iter step " << mergeIter << endl;
      }
      
      if (spImage.getSuperpixelPtr(maxTag) == NULL) {
        if (debug) {
          cout << "leave loop since max has been merged " << maxTag << endl;
        }
        
        break;
      }
      
      // Keep top 95% of sameness compare with gray=200 as min value. So, if > 95% of the pixels are a higher level
      // than 200 the superpixel is returned.
      
      vector<CompareNeighborTuple> resultTuples;
      
      if (range == BACKPROJECT_HIGH_FIVE) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 1, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_FIVE8) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 2, false, 200, 8);
      } else if (range == BACKPROJECT_HIGH_TEN) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 2, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_15) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 3, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_20) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 4, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_50) {
        backprojectNeighborSuperpixels(spImage, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 10, false, 128, 8);
      } else {
        assert(0);
      }
    
      // The back project logic here will return a list of neighbor pixels that are more alike
      // than a threshold and are not already locked. It is possible that all neighbors are
      // locked or are not alike enough and in that case 0 superpixel to merge could be returned.
      
      if (resultTuples.size() == 0) {
        if (debug) {
          cout << "no alike or unlocked neighbors so marking this superpixel as locked also" << endl;
        }
        
        locked[maxTag] = true;
        break;
      }
      
      // Merge each alike neighbor
      
      for (auto it = resultTuples.begin(); it != resultTuples.end(); ++it) {
        CompareNeighborTuple tuple = *it;
      
        int32_t mergeNeighbor = get<2>(tuple);
        
        SuperpixelEdge edge(maxTag, mergeNeighbor);
        
        if (debug) {
          cout << "will merge edge " << edge << endl;
        }
        
        spImage.mergeEdge(edge);
        mergeIter += 1;
        mergesSinceLockClear[maxTag] = true;
        
#if defined(DEBUG)
        // This must never fail since the merge should always consume the other superpixel
        assert(spImage.getSuperpixelPtr(maxTag) != NULL);
#endif // DEBUG
        
        if (dumpEachMergeStepImage) {
          Mat resultImg = inputImg.clone();
          resultImg = (Scalar) 0;
          
          writeTagsWithStaticColortable(spImage, resultImg);
          
          std::ostringstream stringStream;
          stringStream << "backproject_merge_step_" << mergeIter << ".png";
          std::string str = stringStream.str();
          const char *filename = str.c_str();
          
          imwrite(filename, resultImg);
          
          cout << "wrote " << filename << endl;
        }
      }
      
      if (debug) {
        cout << "done with merge of " << resultTuples.size() << " edges" << endl;
      }
      
    } // end of while true loop
    
  } // end of while (!done) loop
  
  if (debug) {
    cout << "left backproject loop with " << spImage.superpixels.size() << " merged superpixels and step " << mergeIter << endl;
  }
  
  return mergeIter;
}

// Recursive bredth first search to fully expand the largest superpixel in a BFS order
// and then lock the superpixel before expanding in terms of smaller superpixels. This
// logic looks for possible expansion using back projection but it keeps track of
// edge weights so that an edge will not be collapsed when it has a very high weight
// as compared to the other edge weights for this specific superpixel.

int MergeSuperpixelImage::mergeBredthFirstRecursive(Mat &inputImg, int colorspace, int startStep, vector<int32_t> *largeSuperpixelsPtr, int numBins)
{
  const bool debug = false;
  const bool dumpLockedSuperpixels = false;
  const bool dumpEachMergeStepImage = false;
  
  vector<int32_t> largeSuperpixels;
  
  if (largeSuperpixelsPtr != NULL) {
    largeSuperpixels = *largeSuperpixelsPtr;
  }
  
  if (debug) {
    cout << "large superpixels before BFS" << endl;
    
    for (auto it = largeSuperpixels.begin(); it != largeSuperpixels.end(); ++it) {
      int32_t tag = *it;
      cout << tag << endl;
    }
  }
  
  // Each iteration will examine the list of superpixels and pick the biggest one
  // that is not already locked. Then that superpixel will be expanded until
  // an edge is encountered. Note that the superpixels list can be modified
  // by a merge, so iterate to find the largest one and then use that value.
  // The main loop will iterate until all superpixels are locked and then
  // the locks will be cleared and the iteration will start again using the
  // now larger superpixels. This will continue to merge
  
  bool done = false;
  int mergeIter = startStep;
  
  int numLockClear = 0;
  unordered_map<int32_t, bool> mergesSinceLockClear;
  
  unordered_map<int32_t, bool> locked;
  
  // Lock each very large superpixel so that the BFS will expand outward towards the
  // largest superpixels but it will not merge contained superpixels into the existing
  // large ones.
  
  for (auto it = largeSuperpixels.begin(); it != largeSuperpixels.end(); ++it) {
    int32_t tag = *it;
    locked[tag] = true;
  }
  
  if (dumpLockedSuperpixels) {
    Mat lockedSuperpixelsMask(inputImg.size(), CV_8UC(1), Scalar(0));

    Mat outputTagsImg = inputImg.clone();
    outputTagsImg = (Scalar) 0;
    writeTagsWithStaticColortable(*this, outputTagsImg);
    
    for (auto it = largeSuperpixels.begin(); it != largeSuperpixels.end(); ++it) {
      int32_t tag = *it;
      
      // Write the largest superpixel tag as the value of the output image.
      
      Mat coordsMat;
      fillMatrixFromCoords(inputImg, tag, coordsMat);
      // Create grayscale version of matrix and set all pixels to 255
      Mat coordsGrayMat(coordsMat.size(), CV_8UC(1));
      coordsGrayMat = Scalar(255);
      reverseFillMatrixFromCoords(coordsGrayMat, true, tag, lockedSuperpixelsMask);
    }
    
    // Use mask to copy colortable image values for just the locked superpixels
    
    //imwrite("tags_colortable_before_mask.png", outputTagsImg);
    //imwrite("locked_mask.png", lockedSuperpixelsMask);
    
    Mat maskedOutput;
    
    outputTagsImg.copyTo(maskedOutput, lockedSuperpixelsMask);
    
    std::ostringstream stringStream;
    stringStream << "tags_locked_before_BFS_" << mergeIter << ".png";
    std::string str = stringStream.str();
    const char *filename = str.c_str();
    
    imwrite(filename, maskedOutput);
    
    cout << "wrote " << filename << endl;
  }
  
  // Do initial sort of the superpixels list so that superpixels are ordered by
  // the number of coordinates in the superpixel. While the sort takes time
  // it means that looking for the largest superpixel can be done by simply
  // finding the next superpixel since superpixels will always be processed
  // from largest to smallest.
  
  vector<int32_t> sortedSuperpixels = sortSuperpixelsBySize();
  
  auto spIter = sortedSuperpixels.begin();
  int32_t maxTag = -1;
  
  while (!done) {
    // Get the next superpixel, it will be LTEQ the size of the current superpixel since
    // the superpixels list was sorted and then only deletes would happen via a merge.
    
#if defined(DEBUG)
    if (spIter != sortedSuperpixels.begin() && spIter != sortedSuperpixels.end()) {
      spIter--;
      int prevTag = *spIter;
      spIter++;
      assert(maxTag == prevTag);
    }
#endif // DEBUG
    
    if (spIter == sortedSuperpixels.end()) {
      // At end of superpixels list
      maxTag = -1;
    } else {
      // Find next unlocked superpixel
      
      maxTag = -1;
      while (spIter != sortedSuperpixels.end()) {
        int32_t nextTag = *spIter;
        
        if (debug) {
          Superpixel *spPtr = getSuperpixelPtr(nextTag);
          int numCoords = (int) spPtr->coords.size();
          cout << "next max superpixel " << nextTag << " N = " << numCoords << endl;
        }
        
        spIter++;
        
        if (locked[nextTag]) {
          if (debug) {
            cout << "next max superpixel locked" << endl;
          }
        } else {
          // Not locked, use it now
          maxTag = nextTag;
          break;
        }
      }
      
#if defined(DEBUG)
      if (maxTag != -1) {
        bool isLocked = locked[maxTag];
        assert(isLocked == false);
      }
#endif // DEBUG
    }
    
    if (maxTag == -1) {
      if (debug) {
        cout << "all superpixels were locked" << endl;
      }
      
      if (debug) {
        cout << "found that all superpixels are locked with " << superpixels.size() << " superpixels" << endl;
        cout << "mergesSinceLockClear.size() " << mergesSinceLockClear.size() << " numLockClear " << numLockClear << endl;
      }
      
      if (1) {
        // Do not unlock and then rerun this logic once all superpixels are locked since the BFS
        // will expand a blob out as much as it can be safely expanded.

        if (debug) {
          cout << "skipping unlock and search again when all locked" << endl;
        }
        
        done = true;
        continue;
      }
      
      if (mergesSinceLockClear.size() == 0) {
        done = true;
        continue;
      }
      
      // Delete lock only for superpixels that were expanded in a previous merge run. This avoids
      // having to recheck all superpixels that did not merge the first time while still checking
      // the superpixels that were expanded and could be ready to merge now.
      
      for (auto it = mergesSinceLockClear.begin(); it != mergesSinceLockClear.end(); ++it) {
        int32_t merged = it->first;
        
        if (locked.count(merged) == 0) {
          if (debug) {
            cout << "expanded superpixel has no lock entry to erase (it was merged into another superpixel) " << merged << endl;
          }
        } else {
          if (debug) {
            int sizeBefore = (int) locked.size();
            cout << "erase expanded superpixel lock " << merged << endl;
            locked.erase(merged);
            int sizeAfter = (int) locked.size();
            assert(sizeBefore == sizeAfter+1);
          } else {
            locked.erase(merged);
          }
        }
      }
      
      mergesSinceLockClear.clear();
      sortedSuperpixels = sortSuperpixelsBySize();
      spIter = sortedSuperpixels.begin();
      numLockClear++;
      continue;
    }
    
    if (debug) {
      Superpixel *spPtr = getSuperpixelPtr(maxTag);
      int numCoords = (int) spPtr->coords.size();
      cout << "found largest superpixel " << maxTag << " with N=" << numCoords << " pixels" << endl;
    }
    
    // Since this superpixel is the largest one currently, merging with another superpixel will always increase the size
    // of this one. The locked table by id logic depends on being able to track a stable UID applied to one specific
    // superpixel, so this approach of using the largest superpixel means that smaller superpixel will always be merged
    // into the current largest superpixel.
    
    while (true) {
      if (debug) {
        cout << "start iter step " << mergeIter << " with largest superpixel " << maxTag << endl;
      }
      
      if (getSuperpixelPtr(maxTag) == NULL) {
        if (debug) {
          cout << "leave loop since max has been merged " << maxTag << endl;
        }
        
        break;
      }
      
      // Gather any neighbors that are at least 50% the same as determined by back projection.
      
      vector<CompareNeighborTuple> resultTuples;
      
      // 20 means slows of 5% percent each, 19 indicates that 95% of values is allowed to match
      
//      backprojectNeighborSuperpixels(inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 19, true, 64, numBins);
      
      backprojectNeighborSuperpixels(*this, inputImg, maxTag, resultTuples, &locked, mergeIter, colorspace, 20, 10, true, 128, numBins);
      
      if (debug) {
        cout << "backprojectNeighborSuperpixels() results for src superpixel " << maxTag << endl;
        
        for (auto it = resultTuples.begin(); it != resultTuples.end(); ++it) {
          CompareNeighborTuple tuple = *it;
          char buffer[1024];
          snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
                   get<0>(tuple), get<1>(tuple), get<2>(tuple));
          cout << (char*)buffer << endl;
        }
      }
      
      // Check for cached neighbor edge weights, this logic must be run each time a neighbor back projection
      // is done since a BFS merge can modify the list of neighbors.
      
      auto neighborsVec = edgeTable.getNeighbors(maxTag);
      
      vector<int32_t> *neighborsPtr = &neighborsVec;
      
      SuperpixelEdgeFuncs::checkNeighborEdgeWeights(*this, inputImg, maxTag, neighborsPtr, edgeTable.edgeStrengthMap, mergeIter);
      
      // The back project logic here will return a list of neighbor pixels that are more alike
      // than a threshold and are not already locked. It is possible that all neighbors are
      // locked or are not alike enough and in that case zero superpixels to merge could be returned.
      
      if (resultTuples.size() == 0) {
        if (debug) {
          cout << "no alike or unlocked neighbors so marking this superpixel as locked also" << endl;
        }
        
        // When a superpixel has no unlocked neighbors check for case where there
        // are no entries at all in the unmerged edge weights list. A superpixel
        // that has no neighbors it could possibly merge with can hit this condition.
        
        Superpixel *spPtr = getSuperpixelPtr(maxTag);
        
        if (spPtr->unmergedEdgeWeights.size() == 0) {
          vector<float> unmergedEdgeWeights;
          
          // Gather cached edge weights, this operation is fast since all the edge weights
          // have been cached already and edge weights are shared between superpixels.
          
          for (auto neighborIter = neighborsPtr->begin(); neighborIter != neighborsPtr->end(); ++neighborIter) {
            int32_t neighborTag = *neighborIter;
            SuperpixelEdge edge(maxTag, neighborTag);
            float edgeWeight = edgeTable.edgeStrengthMap[edge];
            unmergedEdgeWeights.push_back(edgeWeight);
          }
          
          if (debug) {
            if (unmergedEdgeWeights.size() > 0) {
              cout << "adding unmerged edge weights" << endl;
            }
            
            for (auto it = unmergedEdgeWeights.begin(); it != unmergedEdgeWeights.end(); ++it) {
              float edgeWeight = *it;              
              char buffer[1024];
              snprintf(buffer, sizeof(buffer), "%12.4f", edgeWeight);
              cout << (char*)buffer << endl;
            }
          }
          
          SuperpixelEdgeFuncs::addUnmergedEdgeWeights(*this, maxTag, unmergedEdgeWeights);
        }
        
        locked[maxTag] = true;
        break;
      }
      
      // Merge each alike neighbor, note that this merge happens in descending probability order
      // and that rounding is used so that bins of 5% each are treated as a group such that larger
      // superpixels in the same percentage bin get merged first.
      
      vector<vector<CompareNeighborTuple>> tuplesSplitIntoBins;
      int totalTuples = 0;
      
      int endIndex = (int) resultTuples.size() - 1;
      
      if (endIndex == 0) {
        // There is only 1 element in resultTuples
        
        vector<CompareNeighborTuple> currentBin;
        
        currentBin.push_back(resultTuples[0]);
        tuplesSplitIntoBins.push_back(currentBin);
        totalTuples += currentBin.size();
      } else {
        vector<CompareNeighborTuple> currentBin;
        
        for ( int i = 0; i < endIndex; i++ ) {
          if (debug && false) {
            cout << "check indexes " << i << " " << (i+1) << endl;
          }
          
          CompareNeighborTuple t0 = resultTuples[i];
          CompareNeighborTuple t1 = resultTuples[i+1];
          
          float currentPer = get<0>(t0);
          float nextPer = get<0>(t1);
          
          if (debug && false) {
            cout << "compare per " << currentPer << " " << nextPer << endl;
          }
          
          if (currentPer == nextPer) {
            currentBin.push_back(t0);
          } else {
            // When different, finish current bin and then clear so
            // that next iteration starts with an empty bin.
            
            currentBin.push_back(t0);
            tuplesSplitIntoBins.push_back(currentBin);
            totalTuples += currentBin.size();
            currentBin.clear();
          }
        }
        
        // Handle the last tuple
        
        CompareNeighborTuple t1 = resultTuples[endIndex];
        currentBin.push_back(t1);
        tuplesSplitIntoBins.push_back(currentBin);
        totalTuples += currentBin.size();
      }
      
      assert(totalTuples == resultTuples.size());
      
      int totalNeighbors = 0;
      if (debug) {
        totalNeighbors = (int) neighborsPtr->size();
      }
      int neighborsMerged = 0;
      
      if (1) {        
        // Get neighbor edge weights for all neighbors including locked neighbors so that the edge
        // weights can be included in stats for this superpixel. The "should edge be merged" logic
        // depends on knowing what the edge values for weak edges and strong edges are.
        
        // Any edges that will not merged because they were not returned in the resultTuples
        // list can be added to the list of edges that cannot be merged now. This has to be
        // done before comparing edges that can be merged.
        
        // FIXME: this is not quite right when dealing with locked neighbors (ones already processed)
        // since the edge weight could be small but the neighbor is not retuned by back projection
        // because it is locked. That does mean the neighbor cannot be merged but we may not want
        // to save the small edge weights since that would bring down the unmerged value quite a
        // bit.
        
        unordered_map<int32_t, bool> neighborsThatMightBeMergedTable;
        
        for (auto it = resultTuples.begin(); it != resultTuples.end(); ++it) {
          CompareNeighborTuple tuple = *it;
          int32_t mergeNeighbor = get<2>(tuple);
          neighborsThatMightBeMergedTable[mergeNeighbor] = true;
        }
        
        vector<float> unmergedEdgeWeights;
        
        // Iterate over each neighbor and lookup the cached edge weight
        
        for (auto neighborIter = neighborsPtr->begin(); neighborIter != neighborsPtr->end(); ++neighborIter) {
          int32_t neighborTag = *neighborIter;
          SuperpixelEdge edge(maxTag, neighborTag);
#if defined(DEBUG)
          assert(edgeTable.edgeStrengthMap.count(edge) > 0);
#endif // DEBUG
          float edgeWeight = edgeTable.edgeStrengthMap[edge];
          
          if (neighborsThatMightBeMergedTable.count(neighborTag) > 0) {
            // This neighbor might be merged, ignore for now
          } else {
            // This neighbor is known to not be a merge possibility
            
            unmergedEdgeWeights.push_back(edgeWeight);
            
            if (debug) {
              cout << "will add unmergable neighbor edge weight " << edgeWeight << " for neighbor " << neighborTag << endl;
            }
          }
        }
        
        if (unmergedEdgeWeights.size() > 0) {
          SuperpixelEdgeFuncs::addUnmergedEdgeWeights(*this, maxTag, unmergedEdgeWeights);
        }
      }
      
      // Iterate bin by bin. Note that because it is possible that an edge that cannot be merged due to an edge
      // strength being too strong. Continue to process all the bins but just add the edges to the list
      // of unmerged edge weights in that case. As soon as 1 unmerged edge weight is added to unmergedEdgeWeights
      // the the rest of the weights in all the bins are also added.
      
      vector<float> unmergedEdgeWeights;
      
      // Note that the bin by bin merge will merge N neighbors that have the same bin strength but then if there
      // are multiple bins stop so that another round of back projection will be done with the updated histogram
      // results that include the new pixels just merged.
      
      int binOffset = 0;

      for (auto binIter = tuplesSplitIntoBins.begin(); binIter != tuplesSplitIntoBins.end(); ++binIter) {
        vector<CompareNeighborTuple> currentBin = *binIter;
        
        if (debug) {
          cout << "will merge per bin" << endl;
          
          for (auto it = currentBin.begin(); it != currentBin.end(); ++it) {
            CompareNeighborTuple tuple = *it;
            
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
                     get<0>(tuple), get<1>(tuple), get<2>(tuple));
            cout << (char*)buffer << endl;
          }
        }
        
        if (binOffset > 0) {
          // Started processing second bin, exit the neighbor processing loop at this point
          // and do another back projection.
          
          if (debug) {
            cout << "leave bin processing loop in order to do another backprojection" << endl;
          }
          
          break;
        }
        
        binOffset++;
        
        // Edge edge were already computed for all neighbors so gather the
        // edge weights and sort by the edge weight to get the neighbors
        // for just this one bin in edge weight order. This sorting consumes
        // cycles but much of the time there will be few matches per bin so
        // sorting is a no-op.
        
        vector<CompareNeighborTuple> edgeWeightSortedTuples;
        
        for (auto it = currentBin.begin(); it != currentBin.end(); ++it) {
          CompareNeighborTuple tuple = *it;
          int32_t numCoords = get<1>(tuple);
          int32_t mergeNeighbor = get<2>(tuple);
          SuperpixelEdge edge(maxTag, mergeNeighbor);
#if defined(DEBUG)
          assert(edgeTable.edgeStrengthMap.count(edge) > 0);
#endif // DEBUG
          float edgeWeight = edgeTable.edgeStrengthMap[edge];
          CompareNeighborTuple edgeWeightTuple(edgeWeight, numCoords, mergeNeighbor);
          edgeWeightSortedTuples.push_back(edgeWeightTuple);
        }
        
        if (edgeWeightSortedTuples.size() > 1) {
          sort(edgeWeightSortedTuples.begin(), edgeWeightSortedTuples.end(), CompareNeighborTupleFunc);
        }
        
        if (debug) {
          cout << "edge weight ordered neighbors for this bin" << endl;
          
          for (auto it = edgeWeightSortedTuples.begin(); it != edgeWeightSortedTuples.end(); ++it) {
            CompareNeighborTuple tuple = *it;
            
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "(%12.4f, %5d, %5d)",
                     get<0>(tuple), get<1>(tuple), get<2>(tuple));
            cout << (char*)buffer << endl;
          }
        }
        
        for (auto it = edgeWeightSortedTuples.begin(); it != edgeWeightSortedTuples.end(); ++it) {
          CompareNeighborTuple tuple = *it;
          
          int32_t mergeNeighbor = get<2>(tuple);
          
          SuperpixelEdge edge(maxTag, mergeNeighbor);
          
          // Calc stats for unmerged vs successfully merged edges to determine if this specific edge is
          // a hard edge that should indicate where a large superpixel should stop expanding.
          
          float edgeWeight = get<0>(tuple);
          
          if (unmergedEdgeWeights.size() > 0) {
            if (debug) {
              cout << "continue to merge strong edge for neighbor " << mergeNeighbor << " from bins after strong edge found" << endl;
            }
            
            unmergedEdgeWeights.push_back(edgeWeight);
            continue;
          }
          
          bool shouldMerge = shouldMergeEdge(maxTag, edgeWeight);
          
          if (shouldMerge == false) {
            if (debug) {
              cout << "will not merge edge " << edge << endl;
            }
            
            // Once a strong edge is found that prevents a merge stop iterating over
            // percentage bin values. Be sure to include the rest of the edges in
            // the unmerged stats since later logic needs good stats to know what
            // values represent edges that should not be merged.
            
            if (debug) {
              cout << "superpixel " << maxTag << " found a strong edge, lock superpixel and collect strong edges" << endl;
            }
            
            unmergedEdgeWeights.push_back(edgeWeight);
              
            locked[maxTag] = true;
            
            continue;
          }
          
          if (debug) {
            cout << "will merge edge " << edge << endl;
          }
          
          SuperpixelEdgeFuncs::addMergedEdgeWeight(*this, maxTag, edgeWeight);
          
          mergeEdge(edge);
          mergeIter += 1;
          if (debug) {
          neighborsMerged += 1;
          }
          
          mergesSinceLockClear[maxTag] = true;
          
#if defined(DEBUG)
          // This must never fail since the merge should always consume the other superpixel
          assert(getSuperpixelPtr(maxTag) != NULL);
#endif // DEBUG
          
          if (dumpEachMergeStepImage) {
            Mat resultImg = inputImg.clone();
            resultImg = (Scalar) 0;
            
            writeTagsWithStaticColortable(*this, resultImg);
            
            std::ostringstream stringStream;
            stringStream << "backproject_merge_step_" << mergeIter << ".png";
            std::string str = stringStream.str();
            const char *filename = str.c_str();
            
            imwrite(filename, resultImg);
            
            cout << "wrote " << filename << endl;
          }
        }        
      } // end loop over bins
      
      if (debug) {
        cout << "done merging neighbors of " << maxTag << " : merged " << neighborsMerged << " of " << totalNeighbors << endl;
      }
      
      if (unmergedEdgeWeights.size() > 0) {
        // A series of edges was collected after a strong edge was found and merges stopped
        SuperpixelEdgeFuncs::addUnmergedEdgeWeights(*this, maxTag, unmergedEdgeWeights);
        break;
      }
    } // end of while true loop
    
  } // end of while (!done) loop
  
  if (debug) {
    cout << "left backproject loop with " << superpixels.size() << " merged superpixels and step " << mergeIter << endl;
  }
  
  return mergeIter;
}

// Bredth first merge approach where the smallest superpixel is iterated from on each loop and
// then merges are done as long as the back projection fits a given bound. Since this merge
// starts small and ends big, it means that small alike elements expand outward before larger ones.

int MergeSuperpixelImage::mergeBackprojectSmallestSuperpixels(Mat &inputImg, int colorspace, int startStep, BackprojectRange range)
{
  const bool debug = false;
  const bool dumpEachMergeStepImage = false;
  
  // Each iteration will examine the list of superpixels and pick the biggest one
  // that is not already locked. Then that superpixel will be expanded until
  // an edge is encountered. Note that the superpixels list can be modified
  // by a merge, so iterate to find the largest one and then use that value.
  // The main loop will iterate until all superpixels are locked and then
  // the locks will be cleared and the iteration will start again using the
  // now larger superpixels. This will continue to merge
  
  bool done = false;
  int mergeIter = startStep;
  
  int numLockClear = 0;
  unordered_map<int32_t, bool> mergesSinceLockClear;
  
  unordered_map<int32_t, bool> locked;

  // The largest superpixel in the entire list of superpixels is stored
  // so that the smaller inner pixels do not merge with the largest one.
  int maxNumCoords = -1;
  int32_t maxTag = -1;
  bool doLockMaxTag = true;
  
  while (!done) {
    
    uint32_t minThisIter = 0xFFFFFFFF;
    //int maxThisIter = -1;
    int numChecked = 0;
    int32_t minTag = -1;
    //int32_t maxTag = -1;
    
    for (auto it = superpixels.begin(); it != superpixels.end(); ++it) {
      int32_t tag = *it;
      
      Superpixel *spPtr = getSuperpixelPtr(tag);
      
      auto &coords = spPtr->coords;
      
      int numCoords = (int) coords.size();
      
      if (numCoords > maxNumCoords) {
        maxNumCoords = numCoords;
        maxTag = tag;
      }
      
      if (numCoords < minThisIter && (locked.count(tag) == 0)) {
        minThisIter = numCoords;
        minTag = tag;
      }
      
      numChecked++;
    }
    
    if (minTag == -1) {
      if (debug) {
        cout << "checked " << numChecked << " superpixels but all were locked" << endl;
      }
      
      if (debug) {
        cout << "found that all superpixels are locked with " << superpixels.size() << " superpixels" << endl;
        cout << "mergesSinceLockClear.size() " << mergesSinceLockClear.size() << " numLockClear " << numLockClear << endl;
      }
      
      if (mergesSinceLockClear.size() == 0) {
        done = true;
        continue;
      }
      
      // Delete lock only for superpixels that were expanded in a previous merge run. This avoids
      // having to recheck all superpixels that did not merge the first time while still checking
      // the superpixels that were expanded and could be ready to merge now.
      
      for (auto it = mergesSinceLockClear.begin(); it != mergesSinceLockClear.end(); ++it) {
        int32_t merged = it->first;
        
        if (locked.count(merged) == 0) {
          if (debug) {
            cout << "expanded superpixel has no lock entry to erase (it was merged into another superpixel) " << merged << endl;
          }
        } else {
          if (debug) {
            int sizeBefore = (int) locked.size();
            cout << "erase expanded superpixel lock " << merged << endl;
            locked.erase(merged);
            int sizeAfter = (int) locked.size();
            assert(sizeBefore == sizeAfter+1);
          } else {
            locked.erase(merged);
          }
        }
      }
      
      mergesSinceLockClear.clear();
      numLockClear++;
      continue;
    }
    
    if (debug) {
      cout << "checked " << numChecked << " superpixels and found largest superpixel " << minTag << " with N=" << minThisIter << " pixels" << endl;
    }
    
    if (doLockMaxTag) {
      // Do not let the search compare to the max tag since it contains a lot of pixels (typically the BG)
      locked[maxTag] = true;
      doLockMaxTag = false;
    }
    
    // Since this superpixel is the largest one currently, merging with another superpixel will always increase the size
    // of this one. The locked table by id logic depends on being able to track a stable UID applied to one specific
    // superpixel, so this approach of using the largest superpixel means that smaller superpixel will always be merged
    // into the current largest superpixel.
    
    // FIMME: only loop once since smallest will be merged into larger (or possible a tie)
    
    while ((locked.count(minTag) == 0) && (getSuperpixelPtr(minTag) != NULL)) {
      if (debug) {
        cout << "start iter step " << mergeIter << endl;
      }
      
      // Keep top 95% of sameness compare with gray=200 as min value. So, if > 95% of the pixels are a higher level
      // than 200 the superpixel is returned.
      
      vector<CompareNeighborTuple> resultTuples;
      
      if (range == BACKPROJECT_HIGH_FIVE) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 1, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_FIVE8) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 2, false, 200, 8);
      } else if (range == BACKPROJECT_HIGH_TEN) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 2, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_15) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 3, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_20) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 4, false, 200, 16);
      } else if (range == BACKPROJECT_HIGH_50) {
        backprojectNeighborSuperpixels(*this, inputImg, minTag, resultTuples, &locked, mergeIter, colorspace, 20, 10, false, 128, 8);
      } else {
        assert(0);
      }
      
      vector<int32_t> results;
      
      for (auto it = resultTuples.begin(); it != resultTuples.end(); ++it) {
        CompareNeighborTuple tuple = *it;
        results.push_back(get<2>(tuple));
      }
      
      // The back project logic here will return a list of neighbor pixels that are more alike
      // than a threshold and are not already locked. It is possible that all neighbors are
      // locked or are not alike enough and in that case 0 superpixel to merge could be returned.
      
      if (results.size() == 0) {
        if (debug) {
          cout << "no alike or unlocked neighbors so marking this superpixel as locked also" << endl;
        }
        
        locked[minTag] = true;
        break;
      }
      
      // Merge each alike neighbor
      
      for (auto it = results.begin(); it != results.end(); ++it) {
        int32_t mergeNeighbor = *it;
        
        SuperpixelEdge edge(minTag, mergeNeighbor);
        
        if (debug) {
          cout << "will merge edge (" << edge.A << " " << edge.B << ")" << endl;
        }
        
        mergeEdge(edge);
        mergeIter += 1;
        mergesSinceLockClear[mergeNeighbor] = true;
        
#if defined(DEBUG)
        // The assumption here is that the merge always goes to the larger neighbor.
        assert(getSuperpixelPtr(minTag) == NULL);
        assert(getSuperpixelPtr(mergeNeighbor) != NULL);
#endif // DEBUG
        
        if (dumpEachMergeStepImage) {
          Mat resultImg = inputImg.clone();
          resultImg = (Scalar) 0;
          
          writeTagsWithStaticColortable(*this, resultImg);
          
          std::ostringstream stringStream;
          stringStream << "backproject_merge_step_" << mergeIter << ".png";
          std::string str = stringStream.str();
          const char *filename = str.c_str();
          
          imwrite(filename, resultImg);
          
          cout << "wrote " << filename << endl;
        }
        
        if (true) {
          // A min merge kind of throws a wrench in the logic of checking all the neighbors
          // since the min side is always deleted.
          break;
        }
      }
      
      if (debug) {
        cout << "done with merge of " << results.size() << " edges" << endl;
      }
      
    } // end of while not locked loop
    
  } // end of while (!done) loop
  
  if (debug) {
    cout << "left backproject loop with " << superpixels.size() << " merged superpixels and step " << mergeIter << endl;
  }
  
  return mergeIter;
}

bool MergeSuperpixelImage::shouldMergeEdge(int32_t tag, float edgeWeight)
{
  Superpixel *spPtr = getSuperpixelPtr(tag);
  return spPtr->shouldMergeEdge(edgeWeight);
}

// Depth first "flood fill" like merge where a source superpixel is used to create a histogram that will
// then be used to do a depth first search for like superpixels. This logic depends on having initial
// superpixels that are alike enough that a depth first merge based on the pixels is actually useful
// and that means alike pixels need to have already been merged into larger superpixels.

int MergeSuperpixelImage::fillMergeBackprojectSuperpixels(Mat &inputImg, int colorspace, int startStep)
{
  const bool debug = false;
  const bool dumpEachMergeStepImage = false;
  
  // Each iteration will examine the list of superpixels and pick the biggest one
  // that is not already locked. Then that superpixel will be expanded until
  // an edge is encountered. Note that the superpixels list can be modified
  // by a merge, so iterate to find the largest one and then use that value.
  // The main loop will iterate until all superpixels are locked and then
  // the locks will be cleared and the iteration will start again using the
  // now larger superpixels. This will continue to merge
  
  bool done = false;
  int mergeIter = startStep;
  
  int numLockClear = 0;
  unordered_map<int32_t, bool> mergesSinceLockClear;
  
  unordered_map<int32_t, bool> locked;
  
  while (!done) {
    
    int maxThisIter = -1;
    int numChecked = 0;
    int32_t maxTag = -1;
    
    for (auto it = superpixels.begin(); it != superpixels.end(); ++it) {
      int32_t tag = *it;
      
      Superpixel *spPtr = getSuperpixelPtr(tag);
      
      auto &coords = spPtr->coords;
      
      int numCoords = (int) coords.size();
      
      if (numCoords > maxThisIter && (locked.count(tag) == 0)) {
        maxThisIter = numCoords;
        maxTag = tag;
      }
      
      numChecked++;
    }
    
    if (maxTag == -1) {
      if (debug) {
        cout << "checked " << numChecked << " superpixels but all were locked" << endl;
      }
      
      if (debug) {
        cout << "found that all superpixels are locked with " << superpixels.size() << " superpixels" << endl;
        cout << "mergesSinceLockClear.size() " << mergesSinceLockClear.size() << " numLockClear " << numLockClear << endl;
      }

      // Do not clear the locks table since once all merges are done then all superpixels would have been
      // DFS merged.
      
      if (1) {
        done = true;
        continue;
      }
      
//      if (mergesSinceLockClear.size() == 0) {
//        done = true;
//        continue;
//      }
      
      // Delete lock only for superpixels that were expanded in a previous merge run. This avoids
      // having to recheck all superpixels that did not merge the first time while still checking
      // the superpixels that were expanded and could be ready to merge now.
      
      for (auto it = mergesSinceLockClear.begin(); it != mergesSinceLockClear.end(); ++it) {
        int32_t merged = it->first;
        
        if (locked.count(merged) == 0) {
          if (debug) {
            cout << "expanded superpixel has no lock entry to erase (it was merged into another superpixel) " << merged << endl;
          }
        } else {
          if (debug) {
            int sizeBefore = (int) locked.size();
            cout << "erase expanded superpixel lock " << merged << endl;
            locked.erase(merged);
            int sizeAfter = (int) locked.size();
            assert(sizeBefore == sizeAfter+1);
          } else {
            locked.erase(merged);
          }
        }
      }
      
      mergesSinceLockClear.clear();
      numLockClear++;
      continue;
    }
    
    if (debug) {
      cout << "checked " << numChecked << " superpixels and found largest superpixel " << maxTag << " with N=" << maxThisIter << " pixels" << endl;
    }
    
    // Since this superpixel is the largest one currently, merging with another superpixel will always increase the size
    // of this one. The locked table by id logic depends on being able to track a stable UID applied to one specific
    // superpixel, so this approach of using the largest superpixel means that smaller superpixel will always be merged
    // into the current largest superpixel.
    
    while ((locked.count(maxTag) == 0)){
      if (debug) {
        cout << "start iter step " << mergeIter << endl;
      }
      
      vector<int32_t> results;

      // Recurse into neighbors via back projection to basically flood fill alike superpixels.
      // This invocation does a depth first fill until all histogram pixels are zero. This
      // setting indicates that 50% of a superpixel must be at 50% prob in order to continue
      // to expand the depth first search.
      
      backprojectDepthFirstRecurseIntoNeighbors(inputImg, maxTag, results, &locked, mergeIter, colorspace, 20, 10, 128, 16);
      
      // The back project logic here will return a list of neighbor superpixels that contain non-zero
      // back projection pixel values. Since the src superpixel contains only pixels with a 95%
      // confidence then this DFS returns only superpixels that are very alike the largest element.
      // Merge all the returned superpixels and lock the src superpixel so that it is no longer
      // considered in DFS searching.
      
      if (results.size() == 0) {
        if (debug) {
          cout << "no alike or unlocked neighbors so marking this superpixel as locked also" << endl;
        }
        
        locked[maxTag] = true;
        break;
      }
      
      // Merge each alike neighbor
      
      for (auto it = results.begin(); it != results.end(); ++it) {
        int32_t mergeNeighbor = *it;
        
        SuperpixelEdge edge(maxTag, mergeNeighbor);
        
        if (debug) {
          cout << "will merge edge (" << edge.A << " " << edge.B << ")" << endl;
        }
        
        mergeEdge(edge);
        mergeIter += 1;
        mergesSinceLockClear[maxTag] = true;
        
#if defined(DEBUG)
        // This must never fail since the merge should always consume the other superpixel
        assert(getSuperpixelPtr(maxTag) != NULL);
        assert(getSuperpixelPtr(mergeNeighbor) == NULL);
#endif // DEBUG
        
        if (dumpEachMergeStepImage) {
          Mat resultImg = inputImg.clone();
          resultImg = (Scalar) 0;
          
          writeTagsWithStaticColortable(*this, resultImg);
          
          std::ostringstream stringStream;
          stringStream << "backproject_merge_step_" << mergeIter << ".png";
          std::string str = stringStream.str();
          const char *filename = str.c_str();
          
          imwrite(filename, resultImg);
          
          cout << "wrote " << filename << endl;
        }
      }
      
      if (debug) {
        cout << "done with merge of " << results.size() << " edges" << endl;
      }
      
      locked[maxTag] = true;
      
    } // end of while not locked loop
    
  } // end of while (!done) loop
  
  if (debug) {
    cout << "left backproject fill loop with " << superpixels.size() << " merged superpixels and step " << mergeIter << endl;
  }
  
  return mergeIter;
}

// Given a superpixel uid scan the neighbors list and generate a stddev to determine if any of the neighbors
// is significantly larger than other neighbors. Return a vector that contains the large neighbors.

void MergeSuperpixelImage::filterOutVeryLargeNeighbors(int32_t tag, vector<int32_t> &largeNeighbors)
{
  const bool debug = false;
  
  if (debug) {
    cout << "filterOutVeryLargeNeighbors for superpixel " << tag << endl;
  }
  
  largeNeighbors.clear();

  vector<CompareNeighborTuple> tuples;
  
  for ( int32_t neighborTag : edgeTable.getNeighborsSet(tag) ) {
    Superpixel *spPtr = getSuperpixelPtr(neighborTag);
    assert(spPtr);
    
    int32_t numCoords = (int32_t) spPtr->coords.size();
    
    if (debug) {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "neighbor %10d has N = %10d coords", neighborTag, numCoords);
      cout << (char*)buffer << endl;
    }
    
    // Tuple: (UNUSED, UID, SIZE)
    
    CompareNeighborTuple tuple(0.0f, neighborTag, numCoords);
    tuples.push_back(tuple);
  }
  
  if (tuples.size() > 1) {
    sort(tuples.begin(), tuples.end(), CompareNeighborTupleSortByDecreasingLargestNumCoordsFunc);
  }
  
  // Sorted results are now in decreasing num coords order

  if (debug) {
    char buffer[1024];
    
    cout << "sorted tuples:" << endl;
    
    for (auto it = tuples.begin(); it != tuples.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      snprintf(buffer, sizeof(buffer), "neighbor %10d has N = %10d coords", get<1>(tuple), get<2>(tuple));
      cout << (char*)buffer << endl;
    }
  }

  vector<float> sizesVec;
  
  while (1) {
    // If there is only 1 element left in the tuples at this point, break right away since
    // there is no need to run stddev on one element.
    
    if (tuples.size() == 1) {
      if (debug) {
        cout << "exit stddev loop since only 1 tuple left" << endl;
      }
      
      break;
    }
    
    float mean, stddev;
  
    sizesVec.clear();
    
    for (auto it = tuples.begin(); it != tuples.end(); ++it) {
      CompareNeighborTuple tuple = *it;
      int numCoords = get<2>(tuple);
      sizesVec.push_back((float)numCoords);
    }
    
    if (debug) {
      char buffer[1024];
      
      cout << "stddev on " << tuples.size() << " tuples:" << endl;
      
      for (auto it = tuples.begin(); it != tuples.end(); ++it) {
        CompareNeighborTuple tuple = *it;
        snprintf(buffer, sizeof(buffer), "neighbor %10d has N = %10d coords", get<1>(tuple), get<2>(tuple));
        cout << (char*)buffer << endl;
      }
    }
    
    sample_mean(sizesVec, &mean);
    sample_mean_delta_squared_div(sizesVec, mean, &stddev);
  
    int32_t maxSize = sizesVec[0];
    
    // Larger than 1/2 stddev indicates size is larger than 68% of all others
    
    float stddevMin;

    if (stddev < 1.0f) {
      // A very small stddev means values are very close together or there
      // is only 1 value.
      stddevMin = maxSize;
    } else if (stddev < MaxSmallNumPixelsVal) {
      // A very very small stddev is found when all the values are very close
      // together. Set stddevMin to the max so that no neighbor is ignored.
      stddevMin = maxSize;
    } else {
      stddevMin = mean + (stddev * 0.5f);
    }
    
    if (debug) {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "mean      %10.2f, stddev %10.2f", mean, stddev);
      cout << (char*)buffer << endl;
      
      snprintf(buffer, sizeof(buffer), "stddevMin %10.2f, max N  %10d", stddevMin, maxSize);
      cout << (char*)buffer << endl;
    }
    
    if (maxSize > stddevMin) {
      // The current largest neighbor size is significantly larger than the others, ignore it by
      // removing the first element from the tuples vector.
      
      CompareNeighborTuple tuple = tuples[0];
      int32_t neighborTag = get<1>(tuple);
      largeNeighbors.push_back(neighborTag);
      
      tuples.erase(tuples.begin());
      
      if (debug) {
        cout << "erased first element in tuples, size now " << tuples.size() << endl;
      }
      
    } else {
      break; // break out of while loop
    }
  }
  
  if (debug) {
    cout << "filterOutVeryLargeNeighbors returning (count " << largeNeighbors.size() << ") for superpixel " << tag << endl;
    
    for (auto neighborIter = largeNeighbors.begin(); neighborIter != largeNeighbors.end(); ++neighborIter) {
      int32_t neighborTag = *neighborIter;
      cout << neighborTag << endl;
    }
  }
  
  return;
}

// Scan for small superpixels and merge away from largest neighbors.

int MergeSuperpixelImage::mergeSmallSuperpixels(Mat &inputImg, int colorspace, int startStep)
{
  const bool debug = false;
  
  const int maxSmallNum = MaxSmallNumPixelsVal;
  
  int mergeStep = startStep;
  
  vector<int32_t> smallSuperpixels;
  
  // First, scan for very small superpixels and treat them as edges automatically so that
  // edge pixels scanning need not consider these small pixels.
  
  for (auto it = superpixels.begin(); it != superpixels.end(); ++it) {
    int32_t tag = *it;
    Superpixel *spPtr = getSuperpixelPtr(tag);
    assert(spPtr);
    
    int numCoords = (int) spPtr->coords.size();
    
    if (numCoords < maxSmallNum) {
      // Treat small superpixels as edges
      smallSuperpixels.push_back(tag);
    }
  }
  
  if (debug) {
    cout << "found " << smallSuperpixels.size() << " very small superpixels" << endl;
  }
  
  for (auto it = smallSuperpixels.begin(); it != smallSuperpixels.end(); ) {
    int32_t tag = *it;
    
    Superpixel *spPtr = NULL;
    
    spPtr = getSuperpixelPtr(tag);
    if (spPtr == NULL) {
      // Check for the edge case of this superpixel being merged into a neighbor as a result
      // of a previous iteration.
      
      if (debug) {
        cout << "small superpixel " << tag << " was merged away already" << endl;
      }
      
      ++it;
      continue;
    }
    
    // If a superpixel was very small but it has been merged such that it is no longer small
    // then do not do another merge.
    
    if (spPtr->coords.size() >= maxSmallNum) {
      if (debug) {
        cout << "small superpixel " << tag << " is no longer small after merges : N = " << spPtr->coords.size() << endl;
      }
      
      ++it;
      continue;
    }
    
    // Filter out very large neighbors and then merge with most alike small neighbor.
    
    vector<int32_t> largeNeighbors;
    filterOutVeryLargeNeighbors(tag, largeNeighbors);
    
    unordered_map<int32_t, bool> locked;
    unordered_map<int32_t, bool> *lockedPtr = NULL;
    
    for (auto neighborIter = largeNeighbors.begin(); neighborIter != largeNeighbors.end(); ++neighborIter) {
      int32_t neighborTag = *neighborIter;
      locked[neighborTag] = true;
      
      if (debug) {
        cout << "marking significantly larger neighbor " << neighborTag << " as locked to merge away from larger BG" << endl;
      }
    }
    if (largeNeighbors.size() > 0) {
      lockedPtr = &locked;
    }
    
    // FIXME: use compareNeighborEdges here?
    
    vector<CompareNeighborTuple> results;
    compareNeighborSuperpixels(inputImg, tag, results, lockedPtr, mergeStep);
    
    // Get the neighbor with the min hist compare level, note that this
    // sort will not see a significantly larger neighbor if found.
    
    CompareNeighborTuple minTuple = results[0];
    
    int32_t minNeighbor = get<2>(minTuple); // Get NEIGHBOR_TAG
    
    // In case of a tie, choose the smallest of the ties (they are sorted in decreasing N order)
    
    if (results.size() > 1 && get<0>(minTuple) == get<0>(results[1])) {
      float tie = get<0>(minTuple);
      
      minNeighbor = get<2>(results[1]);
      
      if (debug) {
        cout << "choose smaller tie neighbor " << minNeighbor << endl;
      }
      
      for (int i = 2; i < results.size(); i++) {
        CompareNeighborTuple tuple = results[i];
        if (tie == get<0>(tuple)) {
          // Still tie
          minNeighbor = get<2>(tuple);
          
          if (debug) {
            cout << "choose smaller tie neighbor " << minNeighbor << endl;
          }
        } else {
          // Not a tie
          break;
        }
      }
    }
    
    if (debug) {
    cout << "for superpixel " << tag << " min neighbor is " << minNeighbor << endl;
    }
    
    SuperpixelEdge edge(tag, minNeighbor);
    
    mergeEdge(edge);
    
    mergeStep += 1;
    
    spPtr = getSuperpixelPtr(tag);
    
    if ((spPtr != NULL) && (spPtr->coords.size() < maxSmallNum)) {
      // nop to continue to continue combine with the same superpixel tag
      
      if (debug) {
        cout << "small superpixel " << tag << " was merged but it still contains only " << spPtr->coords.size() << " pixels" << endl;
      }
    } else {
      ++it;
    }
  }
  
  return mergeStep;
}

// Scan for "edgy" superpixels, these are identified as having a very high percentage of edge
// pixels as compare to non-edge pixels. These edgy pixels should be merged with other edgy
// superpixels so that edge between smooth regions get merged into one edgy region. This merge
// should not merge with the smooth region neighbors.

int MergeSuperpixelImage::mergeEdgySuperpixels(Mat &inputImg, int colorspace, int startStep, vector<int32_t> *largeSuperpixelsPtr)
{
  const bool debug = false;
  
  const bool debugDumpEdgeGrayValues = false;
  
  const bool debugDumpEdgySuperpixels = false;
  
  const bool dumpEachMergeStepImage = false;
  
  int mergeStep = startStep;
  
  // Lock passed in list of largest superpixels
  
  vector<int32_t> largeSuperpixels;
  if (largeSuperpixelsPtr != NULL) {
    largeSuperpixels = *largeSuperpixelsPtr;
  }
  
  unordered_map<int32_t, bool> largestLocked;
  
  if (largeSuperpixels.size() > 0) {
    // Lock each very large superpixel so that the BFS will expand outward towards the
    // largest superpixels but it will not merge contained superpixels into the existing
    // large ones.
    
    for (auto it = largeSuperpixels.begin(); it != largeSuperpixels.end(); ++it) {
      int32_t tag = *it;
      largestLocked[tag] = true;
    }
  }
  
  // Scan for superpixels that are mostly edges
  
  vector<int32_t> edgySuperpixels;
  
  Mat edgeGrayValues;
  
  if (debugDumpEdgeGrayValues) {
    edgeGrayValues.create(inputImg.rows, inputImg.cols, CV_8UC(3));
  }

  // Another idea would be to compare the size of the src superpixel to the size of
  // a neighbor superpixel. If the neighbor is a lot smaller than the src then the
  // src is unlikely to be an edge, since typically the edges have neighbors that
  // are larger or at least roughly the same size as the src.
  
  // First, scan each superpixel to discover each edge percentage. This is the
  // NUM_EDGE_PIXELS / NUM_PIXELS so that this normalized value will be 1.0
  // when every pixel is an edge pixel.
  
  for (auto it = superpixels.begin(); it != superpixels.end(); ++it) {
    int32_t tag = *it;
    Superpixel *spPtr = getSuperpixelPtr(tag);
    assert(spPtr);
    
    if (debugDumpEdgeGrayValues) {
      edgeGrayValues = Scalar(255, 0, 0); // Blue
    }
    
    int numSrcCoords = (int) spPtr->coords.size();
    
    if (largestLocked.count(tag) > 0) {
      if (debug) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "skipping %d since it is a largest locked superpixel with N = %d coords", tag, numSrcCoords);
        cout << (char*)buffer << endl;
      }
      
      continue;
    }
    
    set<int32_t> &neighbors = edgeTable.getNeighborsSet(tag);
    
    // FIXME: might be better to remove this contained in one superpixel check.
    
    if (neighbors.size() == 1) {
      // In the edge case where there is only 1 neighbor this means that the
      // superpixel is fully contained in another superpixel. Ignore this
      // kind of superpixel since often the one neighbor will be the largest
      // superpixel and this logic would always merge with it.
      
      if (debug) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "edgedetect skipping %d since only 1 neighbor", tag);
        cout << (char*)buffer << endl;
      }
      
      continue;
    }
    
    // Collect all coordinates identified as edge pixels from all the neighbors.
    
    vector<Coord> edgeCoordsVec;
    
    if (debug) {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "edgedetect %10d has N = %10d coords", tag, numSrcCoords);
      cout << (char*)buffer << endl;
    }
    
    for ( int32_t neighborTag : neighbors ) {
      Superpixel *neighborPtr = getSuperpixelPtr(neighborTag);
      assert(neighborPtr);
      
      // Gen edge pixels
      
      vector<Coord> edgeCoordsSrc;
      vector<Coord> edgeCoordsDst;
      
      Superpixel::filterEdgeCoords(spPtr, edgeCoordsSrc, neighborPtr, edgeCoordsDst);
      
      for (auto coordsIter = edgeCoordsSrc.begin(); coordsIter != edgeCoordsSrc.end(); ++coordsIter) {
        Coord coord = *coordsIter;
        edgeCoordsVec.push_back(coord);
      }
      
      int32_t numNeighborCoords = (int32_t) spPtr->coords.size();
      int32_t numSrcEdgeCoords = (int32_t) edgeCoordsSrc.size();
      
      float per = numSrcEdgeCoords / ((float) numSrcCoords);
      
      if (debug) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "neighbor %10d has N = %10d coords", neighborTag, numNeighborCoords);
        cout << (char*)buffer << endl;
        
        snprintf(buffer, sizeof(buffer), "neighbor shares N = %10d edge coords with src (%8.4f percent)", numSrcEdgeCoords, per);
        cout << (char*)buffer << endl;
      }
      
      if (debugDumpEdgeGrayValues) {
        Mat neighborSuperpixelGray(1, (int)edgeCoordsDst.size(), CV_8UC(3));
        uint8_t gray = int(round(per * 255.0f));
        neighborSuperpixelGray = Scalar(gray, gray, gray);
        Superpixel::reverseFillMatrixFromCoords(neighborSuperpixelGray, false, edgeCoordsDst, edgeGrayValues);
      }
    } // end of neighbors loop
    
    // Dedup list of coords
    
    sort(edgeCoordsVec.begin(), edgeCoordsVec.end());
    auto searchIter = unique(edgeCoordsVec.begin(), edgeCoordsVec.end());
    edgeCoordsVec.erase(searchIter, edgeCoordsVec.end());
    
    float per = ((int)edgeCoordsVec.size()) / ((float) numSrcCoords);
    
    if (debugDumpEdgeGrayValues) {
      std::ostringstream stringStream;
      stringStream << "edgedetect_" << tag << ".png";
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      if (0) {
        // Write Green pixels for src superpixel
        Mat srcSuperpixelGreen;
        fillMatrixFromCoords(inputImg, tag, srcSuperpixelGreen);
        srcSuperpixelGreen = Scalar(0,255,0);
        reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, edgeGrayValues);
      }

      if (1) {
        if (debug) {
          char buffer[1024];
          snprintf(buffer, sizeof(buffer), "unique edge coords N = %10d / %10d (%8.4f percent)", (int)edgeCoordsVec.size(), numSrcCoords, per);
          cout << (char*)buffer << endl;
        }
        
        Mat superpixelGray(1, (int)edgeCoordsVec.size(), CV_8UC(3));
        uint8_t gray = int(round(per * 255.0f));
        superpixelGray = Scalar(0, gray, 0);
        Superpixel::reverseFillMatrixFromCoords(superpixelGray, false, edgeCoordsVec, edgeGrayValues);
      }
      
      cout << "write " << filename << " ( " << edgeGrayValues.cols << " x " << edgeGrayValues.rows << " )" << endl;
      imwrite(filename, edgeGrayValues);
    }
    
    if (per > 0.90f) {
      edgySuperpixels.push_back(tag);
    }
  }
  
  if (debug) {
    cout << "found " << edgySuperpixels.size() << " edgy superpixel out of " << (int)superpixels.size() << " total superpixels" << endl;
  }
  
  if (debugDumpEdgySuperpixels) {
    for (auto it = edgySuperpixels.begin(); it != edgySuperpixels.end(); ++it ) {
      int32_t tag = *it;

      Mat edgyMat = inputImg.clone();
      edgyMat = Scalar(255, 0, 0); // Blue
      
      Mat srcSuperpixelGreen;
      fillMatrixFromCoords(inputImg, tag, srcSuperpixelGreen);
      srcSuperpixelGreen = Scalar(0,255,0);
      reverseFillMatrixFromCoords(srcSuperpixelGreen, false, tag, edgyMat);
      
      std::ostringstream stringStream;
      stringStream << "edgy_superpixel_" << tag << ".png";
      std::string str = stringStream.str();
      const char *filename = str.c_str();
      
      cout << "write " << filename << " ( " << edgyMat.cols << " x " << edgyMat.rows << " )" << endl;
      imwrite(filename, edgyMat);
    }
  }
  
  unordered_map<int32_t, bool> edgySuperpixelsTable;
  for (auto it = edgySuperpixels.begin(); it != edgySuperpixels.end(); ++it ) {
    int32_t tag = *it;
    edgySuperpixelsTable[tag] = true;
  }
  
  // Iterate over superpixels detected as edgy, since edgy superpixels will only be merged into
  // other edgy superpixels this logic can merge a specific edgy superpixel multiple times.
  // Looping is implemented by removing the first element from the edgySuperpixelsTable
  // until the table is empty.
  
  while (edgySuperpixelsTable.size() > 0) {
    auto it = edgySuperpixelsTable.begin();
    int32_t tag = it->first;
    
    if (debug) {
      cout << "first edgy table superpixel in table " << tag << endl;
    }
    
#if defined(DEBUG)
    {
      // If an edgy superpixel is in the table it should exist at this point
      Superpixel *spPtr = getSuperpixelPtr(tag);
      assert(spPtr);
    }
#endif // DEBUG
    
    // Filter out any neighbors that are not edgy superpixels. Note that this
    // implicitly ignores the very large superpixels.
    
    unordered_map<int32_t, bool> lockedNeighbors;
    
    for ( int32_t neighborTag : edgeTable.getNeighborsSet(tag) ) {
      if (edgySuperpixelsTable.count(neighborTag) == 0) {
        // Not an edgy superpixel
        lockedNeighbors[neighborTag] = true;
        
        if (debug) {
          cout << "edge weight search locked neighbor " << neighborTag << " since it is not an edgy superpixel" << endl;
        }
      }
    }
    
    unordered_map<int32_t, bool> *lockedPtr = &lockedNeighbors;
    
    vector<CompareNeighborTuple> results;
    
    SuperpixelEdgeFuncs::compareNeighborEdges(*this, inputImg, tag, results, lockedPtr, mergeStep, false);
    
    if (results.size() == 0) {
      // It is possible that an edgy superpixel has no neighbors that are
      // also edgy superpixels, just ignore this one.
  
      if (debug) {
        cout << "ignored edgy superpixel that has no other edgy superpixel neighbors" << endl;
      }
      
      edgySuperpixelsTable.erase(it);
      continue;
    }
    
    // Iterate from smallest edge weight to largest stopping if that edge weight should
    // not be merged or if the superpixel was merged into the neighbor.
    
    int mergeStepAtResultsStart = mergeStep;
    
    for (auto tupleIter = results.begin(); tupleIter != results.end(); ++tupleIter) {
      CompareNeighborTuple tuple = *tupleIter;
      
      float edgeWeight = get<0>(tuple);
      int32_t mergeNeighbor = get<2>(tuple);
      
      if (debug) {
        cout << "for superpixel " << tag << " merge neighbor is " << mergeNeighbor << " with edge wieght " << edgeWeight << endl;
      }
      
      // Bomb out if there are no unmerged values to compare an edge weight to. This should not
      // happen unless the recursive BFS did not find neighbor weights for a superpixel.
      
#if defined(DEBUG)
      {
      Superpixel *spPtr = getSuperpixelPtr(tag);
      assert(spPtr->unmergedEdgeWeights.size() > 0);
      }
#endif // DEBUG
      
      // Calc stats for unmerged vs successfully merged edges to determine if this specific edge is
      // a hard edge that should indicate where a large superpixel should stop expanding.
      
      bool shouldMerge = shouldMergeEdge(tag, edgeWeight);

      if (shouldMerge == false) {
        if (debug) {
          cout << "breaking out of merge loop since neighbor superpixel should not be merged" << endl;
        }
        
        break;
      }
      
      SuperpixelEdge edge(tag, mergeNeighbor);
      
      mergeEdge(edge);
      mergeStep += 1;
      
      if (dumpEachMergeStepImage) {
        Mat resultImg = inputImg.clone();
        resultImg = (Scalar) 0;
        
        writeTagsWithStaticColortable(*this, resultImg);
        
        std::ostringstream stringStream;
        stringStream << "merge_step_" << mergeStep << ".png";
        std::string str = stringStream.str();
        const char *filename = str.c_str();
        
        imwrite(filename, resultImg);
        
        cout << "wrote " << filename << endl;
      }
      
      // Determine if this superpixel was just merged away
      
      Superpixel *spPtr = getSuperpixelPtr(tag);
       
      if (spPtr == NULL) {
        if (debug) {
          cout << "breaking out of edge merge loop since superpixel was merged into larger one" << endl;
        }
        
        edgySuperpixelsTable.erase(it);
        break;
      }
      
      // If tag was merged info neighbor then would not get this far, so
      // neighbor must have been merged into tag. Remove neighbor from
      // the table in this case.
      
#if defined(DEBUG)
      {
        Superpixel *spPtr = getSuperpixelPtr(mergeNeighbor);
        assert(spPtr == NULL);
      }
#endif // DEBUG
      
      edgySuperpixelsTable.erase(mergeNeighbor);
    }
    
    // If the edgy superpixel was merged into another superpixel then the table key would
    // have been removed in the loop above. It is also possible that some merges were done
    // and then merges were stopped as a result of a should merge test failing. Check for
    // the case where no merges were done and then remove the key only in that case.
    
    if (mergeStep == mergeStepAtResultsStart) {
      if (debug) {
        cout << "removing edgy superpixel key since no merges were successful" << endl;
      }
      
      edgySuperpixelsTable.erase(it);
    }

  } // end (edgySuperpixelsTable > 0) loop
  
  return mergeStep;
}

// Iterate over superpixels starting from (0,0) and generate a "touching table" that
// maps superpixel UIDs to unique but small numerical values that should require
// less space to store as compared to a raw tags file.

void MergeSuperpixelImage::recurseTouchingSuperpixels(int32_t rootUID,
                                                 int32_t rootValue,
                                                 unordered_map<int32_t, int32_t> &touchingTable)
{
  const bool debug = true;
  
  if (debug) {
    cout << "recurseTouchingSuperpixels(" << rootUID << ", " << rootValue << ") with " << touchingTable.size() << " table entries" << endl;
  }
  
  assert(rootValue >= 0);
  
  Superpixel *rootPtr = getSuperpixelPtr(rootUID);
  assert(rootPtr);
  
  touchingTable[rootUID] = rootValue;
  
  // Iterate over all neighbors and generate a list of
  // neighbors that already have a mapping. Then select
  // the next lowest number that is not currently used.
  
  vector<int32_t> neighbors = edgeTable.getNeighbors(rootUID);
  
  vector<CompareNeighborTuple> alreadyInTouchingEntry;
  vector<CompareNeighborTuple> needsTouchingEntry;
  
  {
    CompareNeighborTuple tuple(1.0, rootUID, rootValue);
    alreadyInTouchingEntry.push_back(tuple);
  }
  
  for (auto it = begin(neighbors); it != end(neighbors); ++it) {
    int32_t neighborTag = *it;
    
    if (debug) {
      cout << "checking neighbor " << neighborTag << endl;
    }
    
    if (touchingTable.count(neighborTag) > 0) {
      CompareNeighborTuple tuple(1.0, neighborTag, touchingTable[neighborTag]);
      alreadyInTouchingEntry.push_back(tuple);
    } else {
      CompareNeighborTuple tuple(0.0, neighborTag, -1);
      needsTouchingEntry.push_back(tuple);
    }
  }
  
  // Sort alreadyInTouchingEntry so that N unused by neighbors ids
  // can be determined.
  
  if (debug) {
    cout << "sorted alreadyInTouchingEntry: " << endl;
    
    sort(begin(alreadyInTouchingEntry), end(alreadyInTouchingEntry),
         [](CompareNeighborTuple const &t1, CompareNeighborTuple const &t2) {
           return get<2>(t1) < get<2>(t2); // sort ascending
         });
    
    for (auto it = begin(alreadyInTouchingEntry); it != end(alreadyInTouchingEntry); ++it) {
      CompareNeighborTuple tuple = *it;
      
      if (debug) {
        cout << get<1>(tuple) << " -> " << get<2>(tuple) << endl;
      }
    }
    
    cout << "num needsTouchingEntry " << needsTouchingEntry.size() << endl;
  }
  
  if (needsTouchingEntry.size() == 0) {
    return;
  }
  
  unordered_map<int32_t, int32_t> isSet;
  
  for (auto it = begin(alreadyInTouchingEntry); it != end(alreadyInTouchingEntry); ++it) {
    CompareNeighborTuple tuple = *it;
    int32_t uid = get<1>(tuple);
    int32_t usedNum = get<2>(tuple);
    assert(usedNum >= 0);
    isSet[usedNum] = uid;
  }
  
  if (debug) {
    cout << "isSet: " << endl;
    
    for (auto it = begin(isSet); it != end(isSet); ++it) {
      if (debug) {
        cout << it->first << " -> " << it->second << endl;
      }
    }
  }
  
  vector<CompareNeighborTuple> toRecurseTuples;
  
  for (auto it = begin(needsTouchingEntry); it != end(needsTouchingEntry); ++it) {
    CompareNeighborTuple tuple = *it;
    
    if (debug) {
      cout << "needsTouchingEntry neighbor search " << get<1>(tuple) << endl;
    }
   
    // Get next smallest int not used by a neighbor
    
    for (int32_t i = 0; 1; i++) {
      if (isSet.count(i) > 0) {
        // This number is used by a neighbor, continue
        if (debug) {
          cout << "i value " << i << " is used by a neighbor" << endl;
        }
      } else {
        if (debug) {
          cout << "i value " << i << " is unused by a neighbor" << endl;
        }
        
        tuple = CompareNeighborTuple(1.0, get<1>(tuple), i);
        break;
      }
    }
    
    toRecurseTuples.push_back(tuple);
  }

  if (debug) {
    cout << "toRecurseTuples: " << endl;
    
    for (auto it = begin(toRecurseTuples); it != end(toRecurseTuples); ++it) {
      CompareNeighborTuple tuple = *it;
      
      if (debug) {
        cout << get<1>(tuple) << " -> " << get<2>(tuple) << endl;
      }
    }
  }
  
  // Recurse after coordinating int values to use, note that the recursion
  // could have already processed a specific superpixel so check for that
  // condition.
  
  for (auto it = begin(toRecurseTuples); it != end(toRecurseTuples); ++it) {
    CompareNeighborTuple tuple = *it;
    
    int32_t neighborTag = get<1>(tuple);
    
    if (touchingTable.count(neighborTag) == 0) {
      recurseTouchingSuperpixels(neighborTag, get<2>(tuple), touchingTable);
    }
  }
  
  return;
}

// Generate a 3D histogram and or a 3D back projection with the configured settings.
// If a histogram or projection is not wanted then pass NULL.

void parse3DHistogram(Mat *histInputPtr,
                      Mat *histPtr,
                      Mat *backProjectInputPtr,
                      Mat *backProjectPtr,
                      int conversion,
                      int numBins)
{
  const bool debug = false;
  
  if (backProjectPtr) {
    assert(backProjectInputPtr);
    assert(histPtr);
  }
      
  // show non-normalized counts for each bin
  const bool debugCounts = false;

  int imgCount = 1;
  
  Mat mask = Mat();
  
  const int channels[] = {0, 1, 2};
  
  int dims = 3;
  
  int binDim = numBins;
  if (binDim < 0) {
    binDim = 16;
  }
  
  int sizes[] = {binDim, binDim, binDim};
  
  // Range indicates the pixel range (0 <= val < 256)
  
  float rRange[] = {0, 256};
  float gRange[] = {0, 256};
  float bRange[] = {0, 256};
  
  const float *ranges[] = {rRange,gRange,bRange};
  
  bool uniform = true; bool accumulate = false;
  
  // Calculate histogram, note that it is possible to pass
  // in a previously calculated histogram and the result
  // is that back projection will be run with the existing
  // histogram when histInputPtr is passed as NULL
  
  if (histPtr != NULL && histInputPtr != NULL) {
    Mat src;
  
    if (conversion == 0) {
      // Use RGB pixels directly
      src = *histInputPtr;
    } else {
      // Convert input pixel to indicated colorspace before parsing histogram
      cvtColor(*histInputPtr, src, conversion);
    }
  
  assert(!src.empty());
  
  CV_Assert(src.type() == CV_8UC3);
  
  const Mat srcArr[] = {src};
  
  calcHist(srcArr, imgCount, channels, mask, *histPtr, dims, sizes, ranges, uniform, accumulate);
  
  Mat &hist = *histPtr;
    
  if (debug || debugCounts) {
    cout << "histogram:" << endl;
  }
  
  if (debug) {
    cout << "type "<< src.type() << "|" << hist.type() << "\n";
    cout << "rows "<< src.rows << "|" << hist.rows << "\n";
    cout << "columns "<< src.cols << "|" << hist.cols << "\n";
    cout << "channels "<< src.channels() << "|" << hist.channels() << "\n";
  }
  
  assert(histPtr->dims == 3); // binDim x binDim x binDim
  
  int numNonZero;
  int totalNumBins;
  
  numNonZero = 0;
  totalNumBins = 0;
  
  float maxValue = 1.0;
  
  for (int i = 0; i < (binDim * binDim * binDim); i++) {
    totalNumBins++;
    float v = hist.at<float>(0,0,i);
    if (debug) {
      cout << "bin[" << i << "] = " << v << endl;
    }
    if (v != 0.0) {
      if (debug || debugCounts) {
        cout << "bin[" << i << "] = " << v << endl;
      }
      numNonZero++;
      
      if (v > maxValue) {
        maxValue = v;
      }
    }
  }
  
  if (debug) {
    
    cout << "total of " << numNonZero << " non-zero values found in histogram" << endl;
    cout << "total num bins " << totalNumBins  << endl;
    cout << "max bin count val " << maxValue << endl;
    
    cout << "will normalize via mult by 1.0 / " << maxValue << endl;
    
  }
  
  assert(numNonZero > 0); // It should not be possible for no bins to have been filled
  
  hist *= (1.0 / maxValue);
  
  numNonZero = 0;
  
  if (debug) {
    
    for (int i = 0; i < (binDim * binDim * binDim); i++) {
      
      float v = hist.at<float>(0,0,i);
      if (v != 0.0) {
        cout << "nbin[" << i << "] = " << v << endl;
        numNonZero++;
      }
    }
    
    cout << "total of " << numNonZero << " normalized non-zero values found in histogram" << endl;
    
  }
    
  } // end histPtr != NULL
  
  if (backProjectPtr != NULL) {
    // Optionally calculate back projection image (grayscale that shows normalized histogram confidence )
  
    assert(histPtr); // Back projection depends on a histogram
    assert(backProjectInputPtr);
    
    Mat backProjectSrcArr[1];
    
    if (conversion == 0) {
      // Use RGB pixels directly
      backProjectSrcArr[0] = *backProjectInputPtr;
    } else {
      // Convert input pixel to LAB colorspace before parsing histogram
      Mat backProjectInputLab;
      cvtColor(*backProjectInputPtr, backProjectInputLab, conversion);
      backProjectSrcArr[0] = backProjectInputLab;
    }
    
    calcBackProject( backProjectSrcArr, imgCount, channels, *histPtr, *backProjectPtr, ranges, 255.0, uniform );
  }
  
  return;
}

// Given a set of weights that could show positive or negative deltas,
// calculate a bound and determine if the currentWeight falls within
// this bound. This method returns true if the expansion of a superpixel
// should continue, false if the bound has been exceeded.

bool pos_sample_within_bound(vector<float> &weights, float currentWeight) {
  const bool debug = false;

  if (weights.size() == 1 && weights[0] > 0.5) {
    return false;
  }
  
  if (weights.size() <= 2) {
    // If there are not at least 3 values then don't bother trying to generate
    // a positive deltas window.
    return true;
  }

  vector<float> deltaWeights = float_diffs(weights);
  // Always ignore first element
  deltaWeights.erase(deltaWeights.begin());
  
  assert(deltaWeights.size() >= 2);
  
  int numNonNegDeltas = 0;
  
  vector<float> useDeltas;
  
  for (auto it = deltaWeights.begin(); it != deltaWeights.end(); ++it) {
    float deltaWeight = *it;
    
    if (deltaWeight != 0.0f) {
      float absValue;
      if (deltaWeight > 0.0f) {
        absValue = deltaWeight;
        numNonNegDeltas += 1;
      } else {
        absValue = deltaWeight * -1;
      }
      useDeltas.push_back(absValue);
    }
  }
  
  if (debug) {
    cout << "abs deltas" << endl;
    for (auto it = useDeltas.begin(); it != useDeltas.end(); ++it) {
      float delta = *it;
      cout << delta << endl;
    }
  }
  
  if (numNonNegDeltas >= 3) {
    if (debug) {
      cout << "will calculate pos delta window from only positive deltas" << endl;
    }
    
    useDeltas.erase(useDeltas.begin(), useDeltas.end());
    
    vector<float> increasingWeights;
    
    float prev = 0.0f; // will always be set in first iteration
    
    for (auto it = weights.begin(); it != weights.end(); ++it) {
      if (it == weights.begin()) {
        prev = *it;
        continue;
      }
      
      float weight = *it;
      
      if (weight > prev) {
        // Only save weights that increase in value
        increasingWeights.push_back(weight);
        prev = weight;
      }
    }
    
    // FIXME: what happens if there is a big initial delte like 0.9 and then
    // the deltas after it are all smaller?
//
//    0.936088919
//    0.469772607
//    0.286601514
    
    assert(increasingWeights.size() > 0);
    
    if (debug) {
      cout << "increasingWeights" << endl;
      for (auto it = increasingWeights.begin(); it != increasingWeights.end(); ++it) {
        float weight = *it;
        cout << weight << endl;
      }
    }
    
    vector<float> increasingDeltas = float_diffs(increasingWeights);
    // Always ignore first element since it is a delta from zero
    increasingDeltas.erase(increasingDeltas.begin());
    
    if (debug) {
      cout << "increasingDeltas" << endl;
      for (auto it = increasingDeltas.begin(); it != increasingDeltas.end(); ++it) {
        float delta = *it;
        cout << delta << endl;
      }
    }
    
    // Save only positive weights and deltas
    
    weights = increasingWeights;
    useDeltas = increasingDeltas;
  } else {
    if (debug) {
      cout << "will calculate post delta window from abs deltas" << endl;
    }
  }
  
  float mean, stddev;
  
  sample_mean(useDeltas, &mean);
  sample_mean_delta_squared_div(useDeltas, mean, &stddev);
  
  float upperLimit = mean + (stddev * 2);
  
  float lastWeight = weights[weights.size()-1];
  
  float currentWeightDelta = currentWeight - lastWeight;

  if (debug) {
    cout << "mean " << mean << " stddev " << stddev << endl;
    
    cout << "1 stddev " << (mean + (stddev * 1)) << endl;
    cout << "2 stddev " << (mean + (stddev * 2)) << endl;
    cout << "3 stddev " << (mean + (stddev * 3)) << endl;
    
    cout << "last weight " << lastWeight << " currentWeight " << currentWeight << endl;
    cout << "currentWeightDelta " << currentWeightDelta << endl;
  }

  const float minStddev = 0.01f;

  if (stddev > minStddev && currentWeightDelta > 0.0f && currentWeightDelta > upperLimit) {
    
    if (debug) {
      cout << "stop expanding superpixel since currentWeightDelta > upperLimit : " << currentWeightDelta << " > " << upperLimit << endl;
    }
    
    return false;
  } else {
    
    if (debug) {
      if (stddev <= minStddev) {
        cout << "keep expanding superpixel since stddev <= minStddev : " << stddev << " <= " << minStddev << endl;
      } else {
        cout << "keep expanding superpixel since currentWeightDelta <= upperLimit : " << currentWeightDelta << " <= " << upperLimit << endl;
      }
    }
    
    return true;
  }
}

// Create a merge mask that shows the superpixel being considered and a graylevel of the neighbor
// superpixels to indicate the neighbor weigh. The first element of the merges vector indicates
// the superpixel ids considered in a merge step while the weights vector indicates the weights.

void writeSuperpixelMergeMask(SuperpixelImage &spImage, Mat &resultImg, vector<int32_t> merges, vector<float> weights, unordered_map<int32_t, bool> *lockedTablePtr)
{
  assert(merges.size() == weights.size());
  
  int wOffset = 0;
  
  // All locked superpixels as Red

  for (auto it = lockedTablePtr->begin(); it != lockedTablePtr->end(); ++it) {
    int32_t tag = it->first;
    
    Superpixel *spPtr = spImage.getSuperpixelPtr(tag);
    assert(spPtr);
    
    auto &coords = spPtr->coords;
    
    for (auto coordsIter = coords.begin(); coordsIter != coords.end(); ++coordsIter) {
      Coord coord = *coordsIter;
      int32_t X = coord.x;
      int32_t Y = coord.y;
      
      uint32_t pixel = 0xFFFF0000;
      
      Vec3b tagVec;
      tagVec[0] = pixel & 0xFF;
      tagVec[1] = (pixel >> 8) & 0xFF;
      tagVec[2] = (pixel >> 16) & 0xFF;
      resultImg.at<Vec3b>(Y, X) = tagVec;
    }
  }

  // Render weighted neighbors as grey values (inverted)
  
  for (auto it = merges.begin(); it != merges.end(); ++it) {
    int32_t tag = *it;
    
    bool isRootSperpixel = (it == merges.begin());
    float weight = weights[wOffset++];
    
    Superpixel *spPtr = spImage.getSuperpixelPtr(tag);
    assert(spPtr);
    
    auto &coords = spPtr->coords;
    
    for (auto coordsIter = coords.begin(); coordsIter != coords.end(); ++coordsIter) {
      Coord coord = *coordsIter;
      int32_t X = coord.x;
      int32_t Y = coord.y;
      
      uint32_t pixel;
      
      if (isRootSperpixel) {
        pixel = 0xFF00FF00;
      } else {
        uint32_t grey = int(round((1.0f - weight) * 255.0));
        pixel = (grey << 16) | (grey << 8) | grey;
      }
      
      Vec3b tagVec;
      tagVec[0] = pixel & 0xFF;
      tagVec[1] = (pixel >> 8) & 0xFF;
      tagVec[2] = (pixel >> 16) & 0xFF;
      resultImg.at<Vec3b>(Y, X) = tagVec;
    }
  }
}

