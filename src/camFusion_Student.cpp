
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{


	
	// Find the matches within current and previous boundingBox
    std::vector<double> kptMatchesInROI ;
    for(auto it = kptMatches.begin();it!= kptMatches.end();it++)
    {
		cv::KeyPoint curKeyPt = kptsCurr[it->trainIdx];
        cv::KeyPoint preKeyPt = kptsPrev[it->queryIdx];
        

        if(boundingBox.roi.contains(curKeyPt.pt))
        {
            kptMatchesInROI.push_back(cv::norm(curKeyPt.pt-preKeyPt.pt));
        }

    }
    //calculatig the average kptMatches within boundingBox
    double averageDist = std::accumulate(kptMatchesInROI.begin(),kptMatchesInROI.end(),0.0) / kptMatchesInROI.size();

    //pushback kptMatches for boundingBox within the threshold
    double threshold= averageDist+2.5;

    for(auto it = kptMatches.begin();it!= kptMatches.end();it++)
    {
		cv::KeyPoint curKeyPt = kptsCurr[it->trainIdx];
        cv::KeyPoint preKeyPt = kptsPrev[it->queryIdx];
        

        if(boundingBox.roi.contains(curKeyPt.pt))
        {
            double kptdist = cv::norm(curKeyPt.pt-preKeyPt.pt);

            if(kptdist < threshold)
            {
                boundingBox.kptMatches.push_back(*it);
            }
        }
    }
}


// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{


    // compute distance ratios between all matched keypoints
    vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (auto it1 = kptMatches.begin(); it1 != kptMatches.end() - 1; ++it1)
    { // outer kpt. loop

        // get current keypoint and its matched partner in the prev. frame
        cv::KeyPoint kpOuterCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(it1->queryIdx);

        for (auto it2 = kptMatches.begin() + 1; it2 != kptMatches.end(); ++it2)
        { // inner kpt.-loop

            double minDist = 100.0; // min. required distance

            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(it2->trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(it2->queryIdx);

            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);

            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            { // avoid division by zero
                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        }
    }

    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }

    std::sort(distRatios.begin(), distRatios.end());



    double medianDistRatio;
    if (distRatios.size()%2==1)
    {
        medianDistRatio = distRatios[distRatios.size()/2];
    }
    else
    {
        int index = distRatios.size()/2;
        medianDistRatio = (distRatios[index-1] + distRatios[index])/2;
    }

    TTC = - 1 / (frameRate * (1 - medianDistRatio));      //  -dT/(1 - medianDistRatio)

}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    vector<double> prevXPoints, currXPoints;
    double egoLaneWidth = 2.92;

    for (auto it = lidarPointsPrev.begin(); it!=lidarPointsPrev.end();it++)
    {
        if(abs(it->y) <= egoLaneWidth/2.0)
            prevXPoints.push_back(it->x);
    }

    for (auto it = lidarPointsCurr.begin(); it!=lidarPointsCurr.end();it++)
    {
        if(abs(it->y) <= egoLaneWidth/2.0)
            currXPoints.push_back(it->x);
    }

    double minXPrev = 0 , minXCurr = 0 ;

    if(prevXPoints.size() > 0 && currXPoints.size()>0)
    {
        for(auto x : prevXPoints)
            minXPrev += x ;
        minXPrev = minXPrev/prevXPoints.size();
        for(auto y : currXPoints)
            minXCurr += y ;
        minXCurr = minXCurr/currXPoints.size();
    }

    // calculate time to collision
    const double dT = 1.0 / frameRate;
    TTC = minXCurr * dT / (minXPrev - minXCurr);

}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    int curBoxCount = currFrame.boundingBoxes.size();
    int preBoxCount = prevFrame.boundingBoxes.size();
    int ptBoxCounts [curBoxCount][preBoxCount] = {};

    // Iterate over current and previous frame to find bounding boxes contains matches
    for (const cv::DMatch& match : matches)
    {
        for (const BoundingBox& bbCur : currFrame.boundingBoxes)
        {
            for (const BoundingBox& bbPre : prevFrame.boundingBoxes)
            {
                if (bbCur.roi.contains(currFrame.keypoints[match.trainIdx].pt) &&
                    bbPre.roi.contains(prevFrame.keypoints[match.queryIdx].pt))
                {
                    ptBoxCounts[bbCur.boxID][bbPre.boxID] += 1;
                }
            }
        }
    }


    // Find the highest number of counts from previous frame for each box in the current frame
    for (const BoundingBox& bbCur : currFrame.boundingBoxes)
    {
        int maxCount = 0;
        int maxId = 0;

        for (const BoundingBox& bbPre : prevFrame.boundingBoxes)
        {
            if (maxCount < ptBoxCounts[bbCur.boxID][bbPre.boxID])
            {
                maxCount = ptBoxCounts[bbCur.boxID][bbPre.boxID];
                maxId = bbPre.boxID;
            }
        }
        bbBestMatches.insert (std::pair<int,int>(maxId,bbCur.boxID));
    }

}
